#include "rackpage.h"

#include "uilanguage.h"

#include <QDate>
#include <QDateEdit>
#include <QEvent>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QToolTip>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <functional>

namespace {

constexpr int kLabelWidth = 108;
constexpr int kHeaderHeight = 34;
constexpr int kRowHeight = 28;
constexpr int kColumnWidth = 28;

struct RackBlock {
    qint64 stayId = 0;
    qint64 reservationId = 0;
    QString guestName;
    QString stateCode;
    QString reservationStatus;
    QDate arrival;
    QDate departure;
};

struct RackRoom {
    qint64 id = 0;
    QString code;
    QString typeName;
    QString statusCode;
    QVector<RackBlock> blocks;
};

QColor blockColor(const QString &state)
{
    if (state == QLatin1String("in_house"))
        return QColor(0x2e, 0x8b, 0x57);
    if (state == QLatin1String("checked_out"))
        return QColor(0x8a, 0x8f, 0x98);
    if (state == QLatin1String("out_of_order"))
        return QColor(0xc4, 0x7b, 0x2b);
    if (state == QLatin1String("out_of_inventory"))
        return QColor(0x5c, 0x65, 0x70);
    return QColor(0x3b, 0x6f, 0xd8);
}

QString stateText(const QString &state)
{
    if (state == QLatin1String("reserved"))
        return RackPage::tr("Reserved");
    if (state == QLatin1String("in_house"))
        return RackPage::tr("In house");
    if (state == QLatin1String("checked_out"))
        return RackPage::tr("Checked out");
    if (state == QLatin1String("out_of_order"))
        return RackPage::tr("Out of order");
    if (state == QLatin1String("out_of_inventory"))
        return RackPage::tr("Out of inventory");
    return state;
}

} // namespace

class ChartGrid : public QWidget {
public:
    explicit ChartGrid(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("rackGrid"));
        setMouseTracking(true);
    }

    void setChart(QDate from, QDate to, QVector<RackRoom> rooms)
    {
        m_from = from;
        m_to = to;
        m_rooms = std::move(rooms);
        const int nights = qMax(0, int(m_from.daysTo(m_to)));
        setMinimumSize(kLabelWidth + nights * kColumnWidth, kHeaderHeight + qMax(1, m_rooms.size()) * kRowHeight);
        update();
    }

    int roomCount() const { return m_rooms.size(); }

    std::function<void(qint64)> onReservation;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(0xfa, 0xfa, 0xf8));
        const int nights = qMax(0, int(m_from.daysTo(m_to)));
        const int rows = m_rooms.size();

        for (int day = 0; day < nights; ++day) {
            const QDate date = m_from.addDays(day);
            const QRect cell(kLabelWidth + day * kColumnWidth, 0, kColumnWidth, kHeaderHeight + rows * kRowHeight);
            if (date.dayOfWeek() == Qt::Saturday || date.dayOfWeek() == Qt::Sunday)
                painter.fillRect(cell, QColor(0xf0, 0xee, 0xe6));
            painter.setPen(QColor(0x66, 0x66, 0x66));
            painter.drawText(QRect(kLabelWidth + day * kColumnWidth, 0, kColumnWidth, kHeaderHeight),
                             Qt::AlignCenter,
                             QString::number(date.day()));
        }

        const QFontMetrics metrics(painter.font());
        for (int row = 0; row < rows; ++row) {
            const int top = kHeaderHeight + row * kRowHeight;
            painter.fillRect(QRect(0, top, kLabelWidth, kRowHeight), QColor(0xff, 0xff, 0xff));
            painter.setPen(QColor(0xe2, 0xe0, 0xd8));
            painter.drawLine(0, top + kRowHeight - 1, width(), top + kRowHeight - 1);
            painter.setPen(QColor(0x22, 0x22, 0x22));
            const QString label = metrics.elidedText(m_rooms.at(row).code, Qt::ElideRight, kLabelWidth - 8);
            painter.drawText(QRect(4, top, kLabelWidth - 8, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft, label);

            for (const RackBlock &block : m_rooms.at(row).blocks) {
                int start = int(m_from.daysTo(block.arrival));
                int end = int(m_from.daysTo(block.departure));
                start = qMax(0, start);
                end = qMin(nights, end);
                if (end <= start)
                    continue;
                const QRect bar(kLabelWidth + start * kColumnWidth + 1,
                                top + 4,
                                (end - start) * kColumnWidth - 2,
                                kRowHeight - 8);
                painter.setPen(Qt::NoPen);
                painter.setBrush(blockColor(block.stateCode));
                painter.drawRoundedRect(bar, 3, 3);
                painter.setPen(Qt::white);
                const QString name = block.guestName.isEmpty() ? RackPage::tr("No guest") : block.guestName;
                painter.drawText(bar.adjusted(4, 0, -4, 0),
                                 Qt::AlignVCenter | Qt::AlignLeft,
                                 metrics.elidedText(name, Qt::ElideRight, bar.width() - 8));
            }
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const RackBlock *block = blockAt(event->pos());
        if (!block) {
            QToolTip::hideText();
            return;
        }
        const QString guest = block->guestName.isEmpty() ? RackPage::tr("No guest") : block->guestName;
        const QString tip = RackPage::tr("%1\n%2 – %3\n%4")
                                .arg(guest,
                                     block->arrival.toString(Qt::ISODate),
                                     block->departure.toString(Qt::ISODate),
                                     stateText(block->stateCode));
        QToolTip::showText(event->globalPosition().toPoint(), tip, this);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        const RackBlock *block = blockAt(event->pos());
        if (block && onReservation)
            onReservation(block->reservationId);
    }

private:
    const RackBlock *blockAt(const QPoint &pos) const
    {
        if (pos.x() < kLabelWidth || pos.y() < kHeaderHeight)
            return nullptr;
        const int row = (pos.y() - kHeaderHeight) / kRowHeight;
        const int day = (pos.x() - kLabelWidth) / kColumnWidth;
        if (row < 0 || row >= m_rooms.size())
            return nullptr;
        const QDate date = m_from.addDays(day);
        for (const RackBlock &block : m_rooms.at(row).blocks) {
            if (date >= block.arrival && date < block.departure)
                return &block;
        }
        return nullptr;
    }

    QDate m_from;
    QDate m_to;
    QVector<RackRoom> m_rooms;
};

RackPage::RackPage(ApiClient *api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    setObjectName(QStringLiteral("rackPage"));
    m_title = new QLabel;
    m_title->setObjectName(QStringLiteral("rackTitle"));
    m_fromLabel = new QLabel;
    m_toLabel = new QLabel;
    m_from = new QDateEdit(QDate::currentDate());
    m_from->setObjectName(QStringLiteral("rackFrom"));
    m_from->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_from->setCalendarPopup(true);
    m_to = new QDateEdit(QDate::currentDate().addDays(14));
    m_to->setObjectName(QStringLiteral("rackTo"));
    m_to->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_to->setCalendarPopup(true);
    m_show = new QPushButton;
    m_show->setObjectName(QStringLiteral("rackShow"));
    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("rackStatus"));
    m_status->setWordWrap(true);

    m_grid = new ChartGrid;
    m_grid->onReservation = [this](qint64 reservationId) { emit reservationActivated(reservationId); };
    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("rackScroll"));
    scroll->setWidget(m_grid);
    scroll->setWidgetResizable(false);

    auto *bar = new QHBoxLayout;
    bar->addWidget(m_fromLabel);
    bar->addWidget(m_from);
    bar->addWidget(m_toLabel);
    bar->addWidget(m_to);
    bar->addWidget(m_show);
    bar->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_title);
    layout->addLayout(bar);
    layout->addWidget(m_status);
    layout->addWidget(scroll, 1);

    connect(m_show, &QPushButton::clicked, this, &RackPage::reload);
    connect(m_api, &ApiClient::responseFinished, this, [this](const ApiResponse &response) {
        if (!response.current || response.id != m_requestId)
            return;
        applyResponse(response);
    });
    retranslateUi();
}

void RackPage::reload()
{
    if (!m_api->hasToken())
        return;
    if (m_to->date() <= m_from->date()) {
        m_status->setText(tr("The departure date must be after the arrival date."));
        return;
    }
    if (m_from->date().daysTo(m_to->date()) > 120) {
        m_status->setText(tr("The rack window is at most 120 nights."));
        return;
    }
    m_loaded = true;
    m_status->setText(tr("Loading the rack…"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("from"), m_from->date().toString(Qt::ISODate));
    query.addQueryItem(QStringLiteral("to"), m_to->date().toString(Qt::ISODate));
    query.addQueryItem(QStringLiteral("lang"), HotelLocale::currentCode());
    m_requestId = m_api->request(HttpVerb::Get, QStringLiteral("/api/v1/rack"), query, QByteArray(), true);
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
    m_fromLabel->setText(tr("From"));
    m_toLabel->setText(tr("To"));
    m_show->setText(tr("Show"));
    if (!m_loaded && m_status->text().isEmpty())
        m_status->setText(tr("The rack loads after sign-in."));
}

void RackPage::applyResponse(const ApiResponse &response)
{
    if (!response.ok || !response.json.isObject()) {
        m_status->setText(response.error.userMessage.isEmpty() ? apiErrorMessage(response.error.httpStatus, response.error.code)
                                                                : response.error.userMessage);
        m_grid->setChart(m_from->date(), m_to->date(), {});
        return;
    }
    const QJsonObject body = response.json.object();
    const QDate from = QDate::fromString(body.value(QStringLiteral("from")).toString(), Qt::ISODate);
    const QDate to = QDate::fromString(body.value(QStringLiteral("to")).toString(), Qt::ISODate);
    QVector<RackRoom> rooms;
    const QJsonArray items = body.value(QStringLiteral("rooms")).toArray();
    rooms.reserve(items.size());
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        RackRoom room;
        room.id = item.value(QStringLiteral("id")).toInteger();
        room.code = item.value(QStringLiteral("code")).toString();
        room.typeName = item.value(QStringLiteral("type_name")).toString();
        room.statusCode = item.value(QStringLiteral("status_code")).toString();
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
    m_grid->setChart(from.isValid() ? from : m_from->date(), to.isValid() ? to : m_to->date(), rooms);
    const int nights = from.isValid() && to.isValid() ? int(from.daysTo(to)) : 0;
    if (rooms.isEmpty())
        m_status->setText(tr("No rooms."));
    else
        m_status->setText(tr("%1 rooms, %2 nights").arg(rooms.size()).arg(nights));
}
