#include "reservationspage.h"

#include "reservationdialog.h"
#include "uilanguage.h"

#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDialog>
#include <QEvent>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QUrlQuery>
#include <QVBoxLayout>

ReservationsPage::ReservationsPage(ApiClient *api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    setObjectName(QStringLiteral("reservationsPage"));
    m_title = new QLabel;
    m_title->setObjectName(QStringLiteral("reservationsTitle"));
    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("reservationsStatus"));
    m_status->setWordWrap(true);
    m_from = new QDateEdit(QDate::currentDate());
    m_from->setObjectName(QStringLiteral("reservationsFrom"));
    m_from->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_from->setCalendarPopup(true);
    m_to = new QDateEdit(QDate::currentDate().addDays(14));
    m_to->setObjectName(QStringLiteral("reservationsTo"));
    m_to->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_to->setCalendarPopup(true);
    m_guest = new QLineEdit;
    m_guest->setObjectName(QStringLiteral("reservationsGuest"));
    m_room = new QLineEdit;
    m_room->setObjectName(QStringLiteral("reservationsRoom"));
    m_statusFilter = new QComboBox;
    m_statusFilter->setObjectName(QStringLiteral("reservationsStatusFilter"));
    m_show = new QPushButton;
    m_show->setObjectName(QStringLiteral("reservationsShow"));
    m_create = new QPushButton;
    m_create->setObjectName(QStringLiteral("reservationsCreate"));
    m_table = new QTableWidget(0, 6);
    m_table->setObjectName(QStringLiteral("reservationsTable"));
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);

    auto *filters = new QHBoxLayout;
    filters->addWidget(m_from);
    filters->addWidget(m_to);
    filters->addWidget(m_guest);
    filters->addWidget(m_room);
    filters->addWidget(m_statusFilter);
    filters->addWidget(m_show);
    filters->addWidget(m_create);
    filters->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_title);
    layout->addLayout(filters);
    layout->addWidget(m_status);
    layout->addWidget(m_table, 1);

    connect(m_show, &QPushButton::clicked, this, &ReservationsPage::reload);
    connect(m_create, &QPushButton::clicked, this, [this]() { openReservation(0); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const QTableWidgetItem *item = m_table->item(row, 0);
        if (!item)
            return;
        openReservation(item->data(Qt::UserRole).toLongLong());
    });
    connect(m_api, &ApiClient::responseFinished, this, [this](const ApiResponse &response) {
        if (!response.current || response.id != m_requestId)
            return;
        applyResponse(response);
    });
    retranslateUi();
}

void ReservationsPage::reload()
{
    if (!m_api->hasToken())
        return;
    if (m_to->date() <= m_from->date()) {
        m_status->setText(tr("The departure date must be after the arrival date."));
        return;
    }
    m_loaded = true;
    m_status->setText(tr("Loading reservations…"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("from"), m_from->date().toString(Qt::ISODate));
    query.addQueryItem(QStringLiteral("to"), m_to->date().toString(Qt::ISODate));
    if (!m_guest->text().trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("guest"), m_guest->text().trimmed());
    if (!m_room->text().trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("room"), m_room->text().trimmed());
    const QString status = m_statusFilter->currentData().toString();
    if (!status.isEmpty())
        query.addQueryItem(QStringLiteral("status"), status);
    query.addQueryItem(QStringLiteral("lang"), HotelLocale::currentCode());
    m_requestId = m_api->request(HttpVerb::Get, QStringLiteral("/api/v1/reservations"), query, QByteArray(), true);
}

void ReservationsPage::openReservation(qint64 reservationId)
{
    if (!m_api->hasToken())
        return;
    ReservationDialog dialog(m_api, this);
    dialog.load(reservationId);
    if (dialog.exec() == QDialog::Accepted) {
        reload();
        emit reservationsChanged();
    }
}

void ReservationsPage::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
        if (m_loaded)
            reload();
    }
    QWidget::changeEvent(event);
}

void ReservationsPage::retranslateUi()
{
    m_title->setText(tr("Reservations"));
    m_guest->setPlaceholderText(tr("Guest"));
    m_room->setPlaceholderText(tr("Room"));
    m_show->setText(tr("Show"));
    m_create->setText(tr("New reservation"));
    m_table->setHorizontalHeaderLabels({tr("Guest"), tr("Room"), tr("Arrival"), tr("Departure"), tr("Status"), tr("Stay")});
    const QString current = m_statusFilter->currentData().toString();
    m_statusFilter->clear();
    m_statusFilter->addItem(tr("Any status"), QString());
    const QList<QPair<QString, QString>> statuses = {
        {QStringLiteral("tentative"), tr("Tentative")},
        {QStringLiteral("confirmed"), tr("Confirmed")},
        {QStringLiteral("guaranteed"), tr("Guaranteed")},
        {QStringLiteral("blocked"), tr("Blocked")},
        {QStringLiteral("canceled"), tr("Canceled")},
    };
    for (const auto &status : statuses)
        m_statusFilter->addItem(status.second, status.first);
    const int index = m_statusFilter->findData(current);
    m_statusFilter->setCurrentIndex(index < 0 ? 0 : index);
    if (!m_loaded && m_status->text().isEmpty())
        m_status->setText(tr("Reservations load after sign-in."));
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (QTableWidgetItem *status = m_table->item(row, 4))
            status->setText(statusText(status->data(Qt::UserRole).toString()));
        if (QTableWidgetItem *stay = m_table->item(row, 5))
            stay->setText(stayText(stay->data(Qt::UserRole).toString()));
    }
}

QString ReservationsPage::statusText(const QString &code) const
{
    if (code == QLatin1String("tentative"))
        return tr("Tentative");
    if (code == QLatin1String("confirmed"))
        return tr("Confirmed");
    if (code == QLatin1String("guaranteed"))
        return tr("Guaranteed");
    if (code == QLatin1String("blocked"))
        return tr("Blocked");
    if (code == QLatin1String("canceled"))
        return tr("Canceled");
    return code;
}

QString ReservationsPage::stayText(const QString &code) const
{
    if (code == QLatin1String("reserved"))
        return tr("Reserved");
    if (code == QLatin1String("in_house"))
        return tr("In house");
    if (code == QLatin1String("checked_out"))
        return tr("Checked out");
    if (code == QLatin1String("canceled"))
        return tr("Canceled");
    return code;
}

void ReservationsPage::applyResponse(const ApiResponse &response)
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
        const QString statusCode = item.value(QStringLiteral("status_code")).toString();
        const QString stayCode = item.value(QStringLiteral("stay_state")).toString();
        const QStringList cells = {item.value(QStringLiteral("guest_name")).toString(),
                                   item.value(QStringLiteral("room_code")).toString(),
                                   item.value(QStringLiteral("arrival")).toString(),
                                   item.value(QStringLiteral("departure")).toString(),
                                   statusText(statusCode),
                                   stayText(stayCode)};
        for (int column = 0; column < cells.size(); ++column) {
            auto *cell = new QTableWidgetItem(cells.at(column));
            if (column == 0)
                cell->setData(Qt::UserRole, item.value(QStringLiteral("id")).toInteger());
            if (column == 4)
                cell->setData(Qt::UserRole, statusCode);
            if (column == 5)
                cell->setData(Qt::UserRole, stayCode);
            m_table->setItem(row, column, cell);
        }
    }
    m_table->resizeColumnsToContents();
    if (items.isEmpty())
        m_status->setText(tr("No reservations."));
    else
        m_status->setText(tr("%1 reservations").arg(items.size()));
}
