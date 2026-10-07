#include "dictionariespage.h"

#include "dictionarydialogs.h"
#include "uilanguage.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {

QString cellText(const QJsonValue &value)
{
    if (value.isNull() || value.isUndefined())
        return {};
    if (value.isDouble())
        return QString::number(value.toInt());
    return value.toString();
}

QString localeName(const QJsonObject &item, const char *locale)
{
    const QJsonValue value = item.value(QStringLiteral("names")).toObject().value(QLatin1String(locale));
    if (value.isNull() || value.isUndefined())
        return {};
    return value.toString();
}

} // namespace

DictionariesPage::DictionariesPage(ApiClient *api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    setObjectName(QStringLiteral("dictionariesPage"));

    const auto makePane = [this](Pane *pane, Kind kind, const QString &path, const QString &tableName, int columns) {
        pane->kind = kind;
        pane->path = path;
        auto *page = new QWidget(this);
        pane->status = new QLabel(page);
        pane->status->setWordWrap(true);
        pane->locked = new QLabel(page);
        pane->locked->setWordWrap(true);
        pane->add = new QPushButton(page);
        pane->edit = new QPushButton(page);
        pane->remove = new QPushButton(page);
        pane->table = new QTableWidget(0, columns, page);
        pane->table->setObjectName(tableName);
        pane->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        pane->table->setSelectionBehavior(QAbstractItemView::SelectRows);
        pane->table->setSelectionMode(QAbstractItemView::SingleSelection);
        pane->table->verticalHeader()->setVisible(false);
        pane->table->horizontalHeader()->setStretchLastSection(true);
        pane->table->setAlternatingRowColors(true);

        auto *buttons = new QHBoxLayout;
        buttons->addWidget(pane->add);
        buttons->addWidget(pane->edit);
        buttons->addWidget(pane->remove);
        buttons->addStretch(1);

        auto *layout = new QVBoxLayout(page);
        layout->addWidget(pane->status);
        layout->addWidget(pane->locked);
        layout->addLayout(buttons);
        layout->addWidget(pane->table, 1);

        connect(pane->add, &QPushButton::clicked, this, [this, pane]() { addRow(pane); });
        connect(pane->edit, &QPushButton::clicked, this, [this, pane]() { editRow(pane); });
        connect(pane->remove, &QPushButton::clicked, this, [this, pane]() { deleteRow(pane); });
        connect(pane->table, &QTableWidget::itemSelectionChanged, this, [this]() { updateButtons(); });
        connect(pane->table, &QTableWidget::itemDoubleClicked, this, [this, pane](QTableWidgetItem *) {
            if (m_commands)
                editRow(pane);
        });
        return page;
    };

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("dictionaryTabs"));
    m_tabs->addTab(makePane(&m_types, Kind::RoomType, QStringLiteral("/api/v1/room-types"), QStringLiteral("roomTypesTable"), 4),
                   QString());
    m_tabs->addTab(makePane(&m_buildings, Kind::Building, QStringLiteral("/api/v1/buildings"), QStringLiteral("buildingsTable"), 4),
                   QString());
    m_tabs->addTab(makePane(&m_rooms, Kind::Room, QStringLiteral("/api/v1/rooms"), QStringLiteral("roomsTable"), 5), QString());
    m_types.add->setObjectName(QStringLiteral("addRoomType"));
    m_types.edit->setObjectName(QStringLiteral("editRoomType"));
    m_types.remove->setObjectName(QStringLiteral("deleteRoomType"));
    m_buildings.add->setObjectName(QStringLiteral("addBuilding"));
    m_buildings.edit->setObjectName(QStringLiteral("editBuilding"));
    m_buildings.remove->setObjectName(QStringLiteral("deleteBuilding"));
    m_rooms.add->setObjectName(QStringLiteral("addRoom"));
    m_rooms.edit->setObjectName(QStringLiteral("editRoom"));
    m_rooms.remove->setObjectName(QStringLiteral("deleteRoom"));
    m_rooms.status->setObjectName(QStringLiteral("roomsStatus"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs, 1);

    connect(m_api, &ApiClient::responseFinished, this, [this](const ApiResponse &response) {
        if (!response.current)
            return;
        if (response.id == m_types.requestId)
            applyPane(&m_types, response);
        else if (response.id == m_buildings.requestId)
            applyPane(&m_buildings, response);
        else if (response.id == m_rooms.requestId)
            applyPane(&m_rooms, response);
    });

    retranslateUi();
    updateButtons();
}

void DictionariesPage::setCommandsAllowed(bool allowed)
{
    m_commands = allowed;
    updateButtons();
    retranslateUi();
}

void DictionariesPage::reload()
{
    if (!m_api->hasToken())
        return;
    reloadPane(&m_types);
    reloadPane(&m_buildings);
    reloadPane(&m_rooms);
}

void DictionariesPage::reloadPane(Pane *pane)
{
    pane->loaded = true;
    if (pane->kind == Kind::RoomType)
        pane->status->setText(tr("Loading room types…"));
    else if (pane->kind == Kind::Building)
        pane->status->setText(tr("Loading buildings…"));
    else
        pane->status->setText(tr("Loading rooms…"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("lang"), HotelLocale::currentCode());
    pane->requestId = m_api->request(HttpVerb::Get, pane->path, query, QByteArray(), true);
}

void DictionariesPage::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
        if (m_types.loaded || m_buildings.loaded || m_rooms.loaded)
            reload();
    }
    QWidget::changeEvent(event);
}

void DictionariesPage::retranslateUi()
{
    m_tabs->setTabText(0, tr("Room types"));
    m_tabs->setTabText(1, tr("Buildings"));
    m_tabs->setTabText(2, tr("Rooms"));
    const auto labels = [](Pane *pane, const QString &add, const QString &edit, const QString &remove, const QStringList &headers) {
        pane->add->setText(add);
        pane->edit->setText(edit);
        pane->remove->setText(remove);
        pane->table->setHorizontalHeaderLabels(headers);
        pane->locked->setText(DictionariesPage::tr("You do not have permission to change dictionaries."));
        pane->locked->setVisible(!pane->add->isEnabled() && !pane->add->isHidden());
    };
    labels(&m_types,
           tr("Add"),
           tr("Edit"),
           tr("Delete"),
           {tr("Code"), tr("Armenian"), tr("English"), tr("Russian")});
    labels(&m_buildings,
           tr("Add"),
           tr("Edit"),
           tr("Delete"),
           {tr("Code"), tr("Armenian"), tr("English"), tr("Russian")});
    labels(&m_rooms,
           tr("Add"),
           tr("Edit"),
           tr("Delete"),
           {tr("Room"), tr("Floor"), tr("Type"), tr("Building"), tr("Status")});
    m_types.locked->setVisible(!m_commands);
    m_buildings.locked->setVisible(!m_commands);
    m_rooms.locked->setVisible(!m_commands);
    if (!m_types.loaded && m_types.status->text().isEmpty())
        m_types.status->setText(tr("The list loads after sign-in."));
    if (!m_buildings.loaded && m_buildings.status->text().isEmpty())
        m_buildings.status->setText(tr("The list loads after sign-in."));
    if (!m_rooms.loaded && m_rooms.status->text().isEmpty())
        m_rooms.status->setText(tr("The room list loads after sign-in."));
    for (int row = 0; row < m_rooms.table->rowCount(); ++row) {
        QTableWidgetItem *status = m_rooms.table->item(row, 4);
        if (!status)
            continue;
        const QString code = status->data(Qt::UserRole).toString();
        if (!code.isEmpty())
            status->setText(statusText(code));
    }
}

void DictionariesPage::updateButtons()
{
    const auto apply = [this](Pane *pane) {
        const bool selected = pane->table->currentRow() >= 0;
        pane->add->setEnabled(m_commands);
        pane->edit->setEnabled(m_commands && selected);
        pane->remove->setEnabled(m_commands && selected);
        const QString tip = m_commands ? QString() : tr("You do not have permission to change dictionaries.");
        pane->add->setToolTip(tip);
        pane->edit->setToolTip(tip);
        pane->remove->setToolTip(tip);
    };
    apply(&m_types);
    apply(&m_buildings);
    apply(&m_rooms);
}

QString DictionariesPage::statusText(const QString &code)
{
    if (code == QLatin1String("vacant_ready"))
        return tr("Ready");
    if (code == QLatin1String("occupied"))
        return tr("Occupied");
    if (code == QLatin1String("vacant_dirty"))
        return tr("Dirty");
    if (code == QLatin1String("out_of_order"))
        return tr("Out of order");
    if (code == QLatin1String("house_use"))
        return tr("House use");
    if (code == QLatin1String("complimentary"))
        return tr("Complimentary");
    if (code == QLatin1String("out_of_inventory"))
        return tr("Out of inventory");
    return code;
}

QJsonObject DictionariesPage::selected(const Pane &pane) const
{
    const int row = pane.table->currentRow();
    if (row < 0)
        return {};
    const QTableWidgetItem *item = pane.table->item(row, 0);
    if (!item)
        return {};
    return QJsonDocument::fromJson(item->data(Qt::UserRole).toByteArray()).object();
}

void DictionariesPage::applyPane(Pane *pane, const ApiResponse &response)
{
    if (response.path != pane->path) {
        if (!response.ok) {
            pane->status->setText(response.error.userMessage.isEmpty()
                                      ? apiErrorMessage(response.error.httpStatus, response.error.code)
                                      : response.error.userMessage);
            return;
        }
        reload();
        return;
    }
    if (!response.ok || !response.json.isObject()) {
        pane->status->setText(response.error.userMessage.isEmpty() ? apiErrorMessage(response.error.httpStatus, response.error.code)
                                                                    : response.error.userMessage);
        pane->table->setRowCount(0);
        updateButtons();
        return;
    }

    const QJsonArray items = response.json.object().value(QStringLiteral("items")).toArray();
    pane->table->setRowCount(items.size());
    for (int row = 0; row < items.size(); ++row) {
        const QJsonObject item = items.at(row).toObject();
        QStringList cells;
        if (pane->kind == Kind::Room) {
            const QJsonObject type = item.value(QStringLiteral("room_type")).toObject();
            const QJsonValue buildingValue = item.value(QStringLiteral("building"));
            const QString building = buildingValue.isObject() ? buildingValue.toObject().value(QStringLiteral("name")).toString() : QString();
            const QString statusCode = item.value(QStringLiteral("status_code")).toString();
            cells = {item.value(QStringLiteral("code")).toString(),
                     cellText(item.value(QStringLiteral("floor"))),
                     type.value(QStringLiteral("name")).toString(),
                     building,
                     statusText(statusCode)};
        } else {
            cells = {item.value(QStringLiteral("code")).toString(),
                     localeName(item, "hy"),
                     localeName(item, "en"),
                     localeName(item, "ru")};
        }
        for (int column = 0; column < cells.size(); ++column) {
            auto *cell = new QTableWidgetItem(cells.at(column));
            if (column == 0)
                cell->setData(Qt::UserRole, QJsonDocument(item).toJson(QJsonDocument::Compact));
            if (pane->kind == Kind::Room && column == 4)
                cell->setData(Qt::UserRole, item.value(QStringLiteral("status_code")).toString());
            pane->table->setItem(row, column, cell);
        }
    }
    pane->table->resizeColumnsToContents();
    if (items.isEmpty()) {
        if (pane->kind == Kind::RoomType)
            pane->status->setText(tr("No room types."));
        else if (pane->kind == Kind::Building)
            pane->status->setText(tr("No buildings."));
        else
            pane->status->setText(tr("No rooms."));
    } else if (pane->kind == Kind::RoomType) {
        pane->status->setText(tr("%1 room types").arg(items.size()));
    } else if (pane->kind == Kind::Building) {
        pane->status->setText(tr("%1 buildings").arg(items.size()));
    } else {
        pane->status->setText(tr("%1 rooms").arg(items.size()));
    }
    updateButtons();
}

void DictionariesPage::addRow(Pane *pane)
{
    if (!m_commands)
        return;
    if (pane->kind == Kind::Room) {
        RoomEditorDialog dialog(m_api, this);
        dialog.setRecord(QJsonObject());
        if (dialog.exec() == QDialog::Accepted)
            reload();
        return;
    }
    NamedDictionaryDialog dialog(m_api,
                                 pane->kind == Kind::RoomType ? NamedDictionaryDialog::Kind::RoomType
                                                              : NamedDictionaryDialog::Kind::Building,
                                 this);
    dialog.setRecord(QJsonObject());
    if (dialog.exec() == QDialog::Accepted)
        reload();
}

void DictionariesPage::editRow(Pane *pane)
{
    if (!m_commands)
        return;
    const QJsonObject row = selected(*pane);
    if (row.isEmpty()) {
        pane->status->setText(tr("Select a row."));
        return;
    }
    if (pane->kind == Kind::Room) {
        RoomEditorDialog dialog(m_api, this);
        dialog.setRecord(row);
        if (dialog.exec() == QDialog::Accepted)
            reload();
        return;
    }
    NamedDictionaryDialog dialog(m_api,
                                 pane->kind == Kind::RoomType ? NamedDictionaryDialog::Kind::RoomType
                                                              : NamedDictionaryDialog::Kind::Building,
                                 this);
    dialog.setRecord(row);
    if (dialog.exec() == QDialog::Accepted)
        reload();
}

void DictionariesPage::deleteRow(Pane *pane)
{
    if (!m_commands)
        return;
    const QJsonObject row = selected(*pane);
    if (row.isEmpty()) {
        pane->status->setText(tr("Select a row."));
        return;
    }
    const QString code = row.value(QStringLiteral("code")).toString();
    const QString question = code.isEmpty() ? tr("Delete this record?") : tr("Delete %1?").arg(code);
    if (QMessageBox::question(this, tr("Delete"), question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    const qint64 id = row.value(QStringLiteral("id")).toInteger();
    pane->requestId = m_api->request(HttpVerb::Delete, pane->path + QStringLiteral("/%1").arg(id), QUrlQuery(), QByteArray(), true);
}
