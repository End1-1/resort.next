#include "dictionariespage.h"

#include "uilanguage.h"

#include <QEvent>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QTableWidget>
#include <QStringList>
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

} // namespace

DictionariesPage::DictionariesPage(ApiClient *api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    setObjectName(QStringLiteral("dictionariesPage"));

    m_title = new QLabel;
    m_title->setObjectName(QStringLiteral("dictionariesTitle"));

    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("dictionariesStatus"));
    m_status->setWordWrap(true);

    m_table = new QTableWidget(0, 5);
    m_table->setObjectName(QStringLiteral("roomsTable"));
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setAlternatingRowColors(true);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_title);
    layout->addWidget(m_status);
    layout->addWidget(m_table, 1);

    connect(m_api, &ApiClient::responseFinished, this, [this](const ApiResponse &response) {
        if (!response.current || response.id != m_requestId)
            return;
        applyResponse(response);
    });

    retranslateUi();
}

void DictionariesPage::reload()
{
    if (!m_api->hasToken())
        return;
    m_loaded = true;
    m_status->setText(tr("Loading rooms…"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("lang"), HotelLocale::currentCode());
    m_requestId = m_api->request(HttpVerb::Get, QStringLiteral("/api/v1/rooms"), query, QByteArray(), true);
}

void DictionariesPage::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
        if (m_loaded)
            reload();
    }
    QWidget::changeEvent(event);
}

void DictionariesPage::retranslateUi()
{
    m_title->setText(tr("Rooms"));
    m_table->setHorizontalHeaderLabels({tr("Room"), tr("Floor"), tr("Type"), tr("Building"), tr("Status")});
    if (!m_loaded && m_status->text().isEmpty())
        m_status->setText(tr("The room list loads after sign-in."));
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem *status = m_table->item(row, 4);
        if (!status)
            continue;
        const QString code = status->data(Qt::UserRole).toString();
        if (!code.isEmpty())
            status->setText(statusText(code));
    }
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

void DictionariesPage::applyResponse(const ApiResponse &response)
{
    if (!response.ok || !response.json.isObject()) {
        m_status->setText(response.error.userMessage.isEmpty() ? apiErrorMessage(response.error.httpStatus, response.error.code)
                                                                : response.error.userMessage);
        m_table->setRowCount(0);
        return;
    }

    const QJsonArray items = response.json.object().value(QStringLiteral("items")).toArray();
    m_table->setRowCount(items.size());
    for (int row = 0; row < items.size(); ++row) {
        const QJsonObject item = items.at(row).toObject();
        const QJsonObject type = item.value(QStringLiteral("room_type")).toObject();
        const QJsonValue buildingValue = item.value(QStringLiteral("building"));
        const QString building = buildingValue.isObject() ? buildingValue.toObject().value(QStringLiteral("name")).toString() : QString();
        const QString statusCode = item.value(QStringLiteral("status_code")).toString();
        const QStringList cells = {item.value(QStringLiteral("code")).toString(),
                                   cellText(item.value(QStringLiteral("floor"))),
                                   type.value(QStringLiteral("name")).toString(),
                                   building,
                                   statusText(statusCode)};
        for (int column = 0; column < cells.size(); ++column) {
            auto *cell = new QTableWidgetItem(cells.at(column));
            if (column == 4)
                cell->setData(Qt::UserRole, statusCode);
            m_table->setItem(row, column, cell);
        }
    }
    m_table->resizeColumnsToContents();
    if (items.isEmpty())
        m_status->setText(tr("No rooms."));
    else
        m_status->setText(tr("%1 rooms").arg(items.size()));
}
