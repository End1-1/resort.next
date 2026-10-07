#include "rackpage.h"

#include "uilanguage.h"

#include <QComboBox>
#include <QDateEdit>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <algorithm>

namespace {

struct FilterChoice {
    QString key;
    QString label;
};

QString floorKeyOf(const RackRoom &room)
{
    return room.hasFloor ? QString::number(room.floor) : QString();
}

void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

} // namespace

RackPage::RackPage(ApiClient *api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    setObjectName(QStringLiteral("rackPage"));
    m_title = new QLabel;
    m_title->setObjectName(QStringLiteral("rackTitle"));
    m_dateLabel = new QLabel;
    m_date = new QDateEdit(QDate::currentDate());
    m_date->setObjectName(QStringLiteral("rackDate"));
    m_date->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_date->setCalendarPopup(true);
    m_date->setMinimumDate(QDate(2000, 1, 1));
    m_date->setMaximumDate(QDate(2100, 12, 31));
    m_today = new QPushButton;
    m_today->setObjectName(QStringLiteral("rackToday"));
    m_prevDay = new QPushButton(QStringLiteral("◀"));
    m_prevDay->setObjectName(QStringLiteral("rackPrevDay"));
    m_prevDay->setAutoRepeat(true);
    m_nextDay = new QPushButton(QStringLiteral("▶"));
    m_nextDay->setObjectName(QStringLiteral("rackNextDay"));
    m_nextDay->setAutoRepeat(true);
    m_prevRoom = new QPushButton(QStringLiteral("▲"));
    m_prevRoom->setObjectName(QStringLiteral("rackPrevRoom"));
    m_prevRoom->setAutoRepeat(true);
    m_nextRoom = new QPushButton(QStringLiteral("▼"));
    m_nextRoom->setObjectName(QStringLiteral("rackNextRoom"));
    m_nextRoom->setAutoRepeat(true);
    m_roomQuery = new QLineEdit;
    m_roomQuery->setObjectName(QStringLiteral("rackRoomQuery"));
    m_roomQuery->setClearButtonEnabled(true);
    m_statusFilter = new QComboBox;
    m_statusFilter->setObjectName(QStringLiteral("rackStatusFilter"));
    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("rackStatus"));
    m_status->setWordWrap(true);

    m_filterHost = new QWidget;
    m_filterHost->setObjectName(QStringLiteral("rackFilterHost"));
    m_filterLayout = new QHBoxLayout(m_filterHost);
    m_filterLayout->setContentsMargins(0, 0, 0, 0);
    m_filterLayout->setSpacing(4);
    auto *filterScroll = new QScrollArea;
    filterScroll->setObjectName(QStringLiteral("rackFilters"));
    filterScroll->setWidget(m_filterHost);
    filterScroll->setWidgetResizable(true);
    filterScroll->setFrameShape(QFrame::NoFrame);
    filterScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    filterScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    filterScroll->setMinimumHeight(36);
    filterScroll->setMaximumHeight(48);

    m_chart = new RackChart;
    m_loadTimer = new QTimer(this);
    m_loadTimer->setSingleShot(true);
    m_loadTimer->setInterval(30);

    auto *bar = new QHBoxLayout;
    bar->addWidget(m_dateLabel);
    bar->addWidget(m_date);
    bar->addWidget(m_today);
    bar->addWidget(m_prevDay);
    bar->addWidget(m_nextDay);
    bar->addWidget(m_prevRoom);
    bar->addWidget(m_nextRoom);
    bar->addWidget(m_roomQuery);
    bar->addWidget(m_statusFilter);
    bar->addStretch(1);
    bar->addWidget(m_status);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_title);
    layout->addLayout(bar);
    layout->addWidget(filterScroll);
    layout->addWidget(m_chart, 1);

    connect(m_today, &QPushButton::clicked, this, [this]() { m_chart->setOrigin(QDate::currentDate()); });
    connect(m_prevDay, &QPushButton::clicked, this, [this]() { m_chart->scrollDays(-1); });
    connect(m_nextDay, &QPushButton::clicked, this, [this]() { m_chart->scrollDays(1); });
    connect(m_prevRoom, &QPushButton::clicked, this, [this]() { m_chart->scrollRooms(-1); });
    connect(m_nextRoom, &QPushButton::clicked, this, [this]() { m_chart->scrollRooms(1); });
    connect(m_date, &QDateEdit::dateChanged, this, [this](const QDate &date) { m_chart->setOrigin(date); });
    connect(m_roomQuery, &QLineEdit::textChanged, this, [this]() { applyFilter(); });
    connect(m_statusFilter, &QComboBox::currentIndexChanged, this, [this](int) { applyFilter(); });
    connect(m_chart, &RackChart::originChanged, this, [this](const QDate &date) {
        if (m_date->date() != date) {
            const QSignalBlocker blocker(m_date);
            m_date->setDate(date);
        }
        m_loadTimer->start();
    });
    connect(m_chart, &RackChart::viewportChanged, this, [this]() { m_loadTimer->start(); });
    connect(m_chart, &RackChart::navigationChanged, this, &RackPage::updateNav);
    connect(m_chart, &RackChart::reservationActivated, this, &RackPage::reservationActivated);
    connect(m_chart, &RackChart::createReservationRequested, this, &RackPage::createReservationRequested);
    connect(m_loadTimer, &QTimer::timeout, this, &RackPage::ensureData);
    connect(m_api, &ApiClient::responseFinished, this, [this](const ApiResponse &response) {
        if (!response.current || response.id != m_requestId)
            return;
        applyResponse(response);
    });
    retranslateUi();
}

void RackPage::reload()
{
    m_force = true;
    m_loadTimer->start();
}

void RackPage::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
        if (m_loaded)
            reload();
    }
    QWidget::changeEvent(event);
}

void RackPage::retranslateUi()
{
    m_title->setText(tr("Rack"));
    m_dateLabel->setText(tr("Date"));
    m_today->setText(tr("Today"));
    m_prevDay->setToolTip(tr("Previous day"));
    m_nextDay->setToolTip(tr("Next day"));
    m_prevRoom->setToolTip(tr("Previous rooms"));
    m_nextRoom->setToolTip(tr("Next rooms"));
    m_roomQuery->setPlaceholderText(tr("Room"));
    const QString current = m_statusFilter->currentData().toString();
    const QSignalBlocker blocker(m_statusFilter);
    m_statusFilter->clear();
    m_statusFilter->addItem(tr("Any status"), QString());
    const QList<QPair<QString, QString>> statuses = {
        {QStringLiteral("vacant_ready"), roomStatusText(QStringLiteral("vacant_ready"))},
        {QStringLiteral("occupied"), roomStatusText(QStringLiteral("occupied"))},
        {QStringLiteral("vacant_dirty"), roomStatusText(QStringLiteral("vacant_dirty"))},
        {QStringLiteral("out_of_order"), roomStatusText(QStringLiteral("out_of_order"))},
        {QStringLiteral("house_use"), roomStatusText(QStringLiteral("house_use"))},
        {QStringLiteral("complimentary"), roomStatusText(QStringLiteral("complimentary"))},
        {QStringLiteral("out_of_inventory"), roomStatusText(QStringLiteral("out_of_inventory"))},
    };
    for (const auto &status : statuses)
        m_statusFilter->addItem(status.second, status.first);
    const int index = m_statusFilter->findData(current);
    m_statusFilter->setCurrentIndex(index < 0 ? 0 : index);
    m_filterSignature.clear();
    rebuildFilters();
    if (!m_loaded && m_status->text().isEmpty())
        m_status->setText(tr("The rack loads after sign-in."));
    else
        updateNav();
}

void RackPage::ensureData()
{
    if (!m_api->hasToken())
        return;
    if (m_chart->width() < kRackLabelWidth + kRackMinColumnWidth)
        return;
    const QDate from = m_chart->origin();
    const int days = qMax(1, m_chart->visibleDayCount());
    const QDate visibleEnd = from.addDays(days);
    int window = qMax(days + 21, 45);
    if (window > 120)
        window = 120;
    const QDate to = from.addDays(window);
    if (!m_force && m_haveData && from >= m_dataFrom && visibleEnd <= m_dataTo) {
        updateNav();
        return;
    }
    m_force = false;
    m_loaded = true;
    m_failed = false;
    if (!m_haveData)
        m_status->setText(tr("Loading the rack…"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("from"), from.toString(Qt::ISODate));
    query.addQueryItem(QStringLiteral("to"), to.toString(Qt::ISODate));
    query.addQueryItem(QStringLiteral("lang"), HotelLocale::currentCode());
    m_requestId = m_api->request(HttpVerb::Get, QStringLiteral("/api/v1/rack"), query, QByteArray(), true);
}

void RackPage::applyResponse(const ApiResponse &response)
{
    if (!response.ok || !response.json.isObject()) {
        m_failed = true;
        m_status->setText(response.error.userMessage.isEmpty() ? apiErrorMessage(response.error.httpStatus, response.error.code)
                                                                : response.error.userMessage);
        updateNav();
        return;
    }
    const QJsonObject body = response.json.object();
    m_dataFrom = QDate::fromString(body.value(QStringLiteral("from")).toString(), Qt::ISODate);
    m_dataTo = QDate::fromString(body.value(QStringLiteral("to")).toString(), Qt::ISODate);
    QVector<RackRoom> rooms;
    const QJsonArray items = body.value(QStringLiteral("rooms")).toArray();
    rooms.reserve(items.size());
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        RackRoom room;
        room.id = item.value(QStringLiteral("id")).toInteger();
        room.code = item.value(QStringLiteral("code")).toString();
        room.typeCode = item.value(QStringLiteral("type_code")).toString();
        room.typeName = item.value(QStringLiteral("type_name")).toString();
        const QJsonValue floor = item.value(QStringLiteral("floor"));
        room.hasFloor = !floor.isNull() && !floor.isUndefined();
        if (room.hasFloor)
            room.floor = floor.toInt();
        room.statusCode = item.value(QStringLiteral("status_code")).toString();
        const QJsonValue buildingCode = item.value(QStringLiteral("building_code"));
        if (!buildingCode.isNull() && !buildingCode.isUndefined())
            room.buildingCode = buildingCode.toString();
        const QJsonValue buildingName = item.value(QStringLiteral("building_name"));
        if (!buildingName.isNull() && !buildingName.isUndefined())
            room.buildingName = buildingName.toString();
        const QJsonArray rawBlocks = item.value(QStringLiteral("blocks")).toArray();
        for (const QJsonValue &raw : rawBlocks) {
            const QJsonObject block = raw.toObject();
            RackBlock parsed;
            parsed.stayId = block.value(QStringLiteral("stay_id")).toInteger();
            parsed.reservationId = block.value(QStringLiteral("reservation_id")).toInteger();
            parsed.guestName = block.value(QStringLiteral("guest_name")).toString();
            parsed.stateCode = block.value(QStringLiteral("state_code")).toString();
            parsed.reservationStatus = block.value(QStringLiteral("reservation_status")).toString();
            parsed.arrival = QDate::fromString(block.value(QStringLiteral("arrival")).toString(), Qt::ISODate);
            parsed.departure = QDate::fromString(block.value(QStringLiteral("departure")).toString(), Qt::ISODate);
            room.blocks.append(parsed);
        }
        rooms.append(room);
    }
    m_rooms = rooms;
    m_haveData = true;
    m_failed = false;
    m_chart->setRooms(m_rooms);
    rebuildFilters();
    updateNav();
}

void RackPage::rebuildFilters()
{
    QVector<FilterChoice> types;
    QVector<FilterChoice> buildings;
    QVector<FilterChoice> floors;
    QSet<QString> seenType;
    QSet<QString> seenBuilding;
    QSet<QString> seenFloor;
    for (const RackRoom &room : m_rooms) {
        if (!room.typeCode.isEmpty() && !seenType.contains(room.typeCode)) {
            seenType.insert(room.typeCode);
            types.append({room.typeCode, room.typeName.isEmpty() ? room.typeCode : room.typeName});
        }
        if (!seenBuilding.contains(room.buildingCode)) {
            seenBuilding.insert(room.buildingCode);
            const QString label = room.buildingCode.isEmpty()
                ? tr("No building")
                : (room.buildingName.isEmpty() ? room.buildingCode : room.buildingName);
            buildings.append({room.buildingCode, label});
        }
        const QString key = floorKeyOf(room);
        if (!seenFloor.contains(key)) {
            seenFloor.insert(key);
            floors.append({key, room.hasFloor ? tr("Floor %1").arg(room.floor) : tr("No floor")});
        }
    }
    std::sort(types.begin(), types.end(), [](const FilterChoice &left, const FilterChoice &right) {
        return left.key.localeAwareCompare(right.key) < 0;
    });
    auto emptyLast = [](const FilterChoice &left, const FilterChoice &right) {
        if (left.key.isEmpty() != right.key.isEmpty())
            return !left.key.isEmpty();
        return left.label.localeAwareCompare(right.label) < 0;
    };
    std::sort(buildings.begin(), buildings.end(), emptyLast);
    std::sort(floors.begin(), floors.end(), [](const FilterChoice &left, const FilterChoice &right) {
        if (left.key.isEmpty() != right.key.isEmpty())
            return !left.key.isEmpty();
        return left.key.toInt() < right.key.toInt();
    });

    QString signature = QString::number(types.size()) + QLatin1Char('|') + QString::number(buildings.size())
        + QLatin1Char('|') + QString::number(floors.size());
    for (const FilterChoice &choice : types)
        signature += choice.key + choice.label;
    for (const FilterChoice &choice : buildings)
        signature += choice.key + choice.label;
    for (const FilterChoice &choice : floors)
        signature += choice.key + choice.label;
    if (signature == m_filterSignature)
        return;
    m_filterSignature = signature;

    const auto previous = m_filterHost->findChildren<QPushButton *>();
    QStringList typeCodes;
    QStringList buildingCodes;
    QStringList floorCodes;
    for (QPushButton *button : previous) {
        if (!button->isChecked())
            continue;
        const QString kind = button->property("filterKind").toString();
        const QString key = button->property("filterKey").toString();
        if (kind == QLatin1String("type"))
            typeCodes.append(key);
        else if (kind == QLatin1String("building"))
            buildingCodes.append(key);
        else if (kind == QLatin1String("floor"))
            floorCodes.append(key);
    }

    m_adjusting = true;
    clearLayout(m_filterLayout);
    auto *all = new QPushButton(tr("All"));
    all->setObjectName(QStringLiteral("rackFilterAll"));
    m_filterLayout->addWidget(all);
    connect(all, &QPushButton::clicked, this, [this]() {
        m_adjusting = true;
        const auto buttons = m_filterHost->findChildren<QPushButton *>();
        for (QPushButton *button : buttons) {
            if (button->isCheckable())
                button->setChecked(false);
        }
        m_adjusting = false;
        applyFilter();
    });

    auto addGroup = [this](const QString &title, const QString &kind, const QVector<FilterChoice> &choices, const QStringList &checked) {
        if (choices.isEmpty())
            return;
        auto *line = new QFrame;
        line->setFrameShape(QFrame::VLine);
        m_filterLayout->addWidget(line);
        auto *label = new QLabel(title);
        m_filterLayout->addWidget(label);
        for (const FilterChoice &choice : choices) {
            auto *button = new QPushButton(choice.label);
            button->setCheckable(true);
            button->setProperty("filterKind", kind);
            button->setProperty("filterKey", choice.key);
            button->setStyleSheet(QStringLiteral("QPushButton:checked { background-color: #b2fab8; color: #14301a; }"));
            button->setMaximumWidth(160);
            button->blockSignals(true);
            button->setChecked(checked.contains(choice.key));
            button->blockSignals(false);
            connect(button, &QPushButton::toggled, this, [this](bool) { applyFilter(); });
            m_filterLayout->addWidget(button);
        }
    };
    addGroup(tr("Type"), QStringLiteral("type"), types, typeCodes);
    addGroup(tr("Building"), QStringLiteral("building"), buildings, buildingCodes);
    addGroup(tr("Floor"), QStringLiteral("floor"), floors, floorCodes);
    m_filterLayout->addStretch(1);
    m_adjusting = false;
    applyFilter();
}

void RackPage::applyFilter()
{
    if (m_adjusting || !m_chart)
        return;
    RackFilter filter;
    const auto buttons = m_filterHost->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (!button->isChecked())
            continue;
        const QString kind = button->property("filterKind").toString();
        const QString key = button->property("filterKey").toString();
        if (kind == QLatin1String("type"))
            filter.typeCodes.append(key);
        else if (kind == QLatin1String("building"))
            filter.buildingCodes.append(key);
        else if (kind == QLatin1String("floor"))
            filter.floors.append(key);
    }
    filter.roomQuery = m_roomQuery->text();
    const QString status = m_statusFilter->currentData().toString();
    if (!status.isEmpty())
        filter.statusCodes.append(status);
    m_chart->setFilter(filter);
    updateNav();
}

void RackPage::updateNav()
{
    m_prevRoom->setEnabled(m_chart->canScrollUp());
    m_nextRoom->setEnabled(m_chart->canScrollDown());
    if (!m_loaded || m_failed)
        return;
    const int rooms = m_chart->filteredRoomCount();
    const int days = m_chart->visibleDayCount();
    if (!m_haveData)
        return;
    if (m_rooms.isEmpty())
        m_status->setText(tr("No rooms."));
    else if (rooms == 0)
        m_status->setText(tr("No rooms match the filter."));
    else
        m_status->setText(tr("%1 rooms, %2 days").arg(rooms).arg(days));
}

QString RackPage::roomStatusText(const QString &status) const
{
    if (status == QLatin1String("vacant_ready"))
        return tr("Ready");
    if (status == QLatin1String("occupied"))
        return tr("Occupied");
    if (status == QLatin1String("vacant_dirty"))
        return tr("Dirty");
    if (status == QLatin1String("out_of_order"))
        return tr("Out of order");
    if (status == QLatin1String("house_use"))
        return tr("House use");
    if (status == QLatin1String("complimentary"))
        return tr("Complimentary");
    if (status == QLatin1String("out_of_inventory"))
        return tr("Out of inventory");
    return status;
}
