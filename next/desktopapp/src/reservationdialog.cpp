#include "reservationdialog.h"

#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {

QString stayText(const QString &code)
{
    if (code == QLatin1String("reserved"))
        return ReservationDialog::tr("Reserved");
    if (code == QLatin1String("in_house"))
        return ReservationDialog::tr("In house");
    if (code == QLatin1String("checked_out"))
        return ReservationDialog::tr("Checked out");
    if (code == QLatin1String("canceled"))
        return ReservationDialog::tr("Canceled");
    return code;
}

} // namespace

ReservationDialog::ReservationDialog(ApiClient *api, QWidget *parent)
    : QDialog(parent)
    , m_api(api)
{
    setObjectName(QStringLiteral("reservationDialog"));
    m_error = new QLabel;
    m_error->setObjectName(QStringLiteral("reservationError"));
    m_error->setWordWrap(true);
    m_lastName = new QLineEdit;
    m_lastName->setObjectName(QStringLiteral("reservationLastName"));
    m_firstName = new QLineEdit;
    m_firstName->setObjectName(QStringLiteral("reservationFirstName"));
    m_room = new QComboBox;
    m_room->setObjectName(QStringLiteral("reservationRoom"));
    m_arrival = new QDateEdit(QDate::currentDate());
    m_arrival->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_arrival->setCalendarPopup(true);
    m_departure = new QDateEdit(QDate::currentDate().addDays(1));
    m_departure->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_departure->setCalendarPopup(true);
    m_status = new QComboBox;
    m_status->setObjectName(QStringLiteral("reservationStatus"));
    m_remarks = new QLineEdit;
    m_stay = new QLabel;
    m_stay->setObjectName(QStringLiteral("reservationStayState"));

    m_checkIn = new QPushButton;
    m_checkOut = new QPushButton;
    m_cancelStay = new QPushButton;
    m_save = new QPushButton;
    m_save->setObjectName(QStringLiteral("reservationSave"));
    m_save->setDefault(true);

    auto *form = new QFormLayout;
    auto addRow = [form](const char *name, QWidget *field) {
        auto *label = new QLabel;
        label->setObjectName(QString::fromLatin1(name));
        form->addRow(label, field);
    };
    addRow("reservationLastNameLabel", m_lastName);
    addRow("reservationFirstNameLabel", m_firstName);
    addRow("reservationRoomLabel", m_room);
    addRow("reservationArrivalLabel", m_arrival);
    addRow("reservationDepartureLabel", m_departure);
    addRow("reservationStatusLabel", m_status);
    addRow("reservationRemarksLabel", m_remarks);
    addRow("reservationStayLabel", m_stay);

    auto *actions = new QHBoxLayout;
    actions->addWidget(m_checkIn);
    actions->addWidget(m_checkOut);
    actions->addWidget(m_cancelStay);
    actions->addStretch(1);
    actions->addWidget(m_save);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_error);
    layout->addLayout(form);
    layout->addLayout(actions);

    connect(m_save, &QPushButton::clicked, this, &ReservationDialog::save);
    connect(m_checkIn, &QPushButton::clicked, this, [this]() { sendState(QStringLiteral("in_house")); });
    connect(m_checkOut, &QPushButton::clicked, this, [this]() { sendState(QStringLiteral("checked_out")); });
    connect(m_cancelStay, &QPushButton::clicked, this, [this]() { sendStatus(QStringLiteral("canceled")); });
    retranslateUi();
}

void ReservationDialog::retranslateUi()
{
    auto setLabel = [this](const char *name, const QString &text) {
        if (auto *label = findChild<QLabel *>(QString::fromLatin1(name)))
            label->setText(text);
    };
    setLabel("reservationLastNameLabel", tr("Last name"));
    setLabel("reservationFirstNameLabel", tr("First name"));
    setLabel("reservationRoomLabel", tr("Room"));
    setLabel("reservationArrivalLabel", tr("Arrival"));
    setLabel("reservationDepartureLabel", tr("Departure"));
    setLabel("reservationStatusLabel", tr("Status"));
    setLabel("reservationRemarksLabel", tr("Remarks"));
    setLabel("reservationStayLabel", tr("Stay"));
    m_checkIn->setText(tr("Check in"));
    m_checkOut->setText(tr("Check out"));
    m_cancelStay->setText(tr("Cancel reservation"));
    m_save->setText(tr("Save"));
    const QString current = m_status->currentData().toString();
    m_status->clear();
    const QList<QPair<QString, QString>> statuses = {
        {QStringLiteral("tentative"), tr("Tentative")},
        {QStringLiteral("confirmed"), tr("Confirmed")},
        {QStringLiteral("guaranteed"), tr("Guaranteed")},
        {QStringLiteral("blocked"), tr("Blocked")},
        {QStringLiteral("canceled"), tr("Canceled")},
    };
    for (const auto &status : statuses)
        m_status->addItem(status.second, status.first);
    const int index = m_status->findData(current.isEmpty() ? QStringLiteral("confirmed") : current);
    m_status->setCurrentIndex(index < 0 ? 1 : index);
    if (!m_stayState.isEmpty())
        m_stay->setText(stayText(m_stayState));
}

ApiResponse ReservationDialog::waitFor(quint64 requestId)
{
    ApiResponse found;
    QEventLoop loop;
    const QMetaObject::Connection finished = connect(m_api, &ApiClient::responseFinished, &loop, [&](const ApiResponse &response) {
        if (response.id != requestId)
            return;
        found = response;
        loop.quit();
    });
    const QMetaObject::Connection rejected = connect(m_api, &ApiClient::sessionRejected, &loop, [&](const QString &) { loop.quit(); });
    loop.exec();
    disconnect(finished);
    disconnect(rejected);
    return found;
}

void ReservationDialog::loadNew(qint64 roomId, const QDate &arrival, const QDate &departure)
{
    m_hasPreset = true;
    m_presetRoomId = roomId;
    m_presetArrival = arrival;
    m_presetDeparture = departure;
    load(0);
}

void ReservationDialog::load(qint64 reservationId)
{
    m_id = reservationId;
    m_version = 0;
    m_stayId = 0;
    m_stayState.clear();
    if (reservationId > 0)
        m_hasPreset = false;
    retranslateUi();
    setWindowTitle(reservationId == 0 ? tr("New reservation") : tr("Reservation %1").arg(reservationId));
    const bool editing = reservationId > 0;
    m_checkIn->setVisible(editing);
    m_checkOut->setVisible(editing);
    m_cancelStay->setVisible(editing);
    m_stay->setVisible(editing);

    QUrlQuery roomsQuery;
    roomsQuery.addQueryItem(QStringLiteral("lang"), QStringLiteral("en"));
    const ApiResponse rooms = waitFor(m_api->request(HttpVerb::Get, QStringLiteral("/api/v1/rooms"), roomsQuery, QByteArray(), true));
    if (rooms.ok && rooms.json.isObject())
        fillRooms(rooms.json.object().value(QStringLiteral("items")).toArray());
    else
        showFailure(rooms);

    if (reservationId <= 0) {
        if (m_hasPreset) {
            if (m_presetArrival.isValid())
                m_arrival->setDate(m_presetArrival);
            if (m_presetDeparture.isValid())
                m_departure->setDate(m_presetDeparture);
            const int presetIndex = m_room->findData(m_presetRoomId);
            if (presetIndex >= 0)
                m_room->setCurrentIndex(presetIndex);
        }
        return;
    }
    const ApiResponse detail = waitFor(m_api->request(HttpVerb::Get,
                                                      QStringLiteral("/api/v1/reservations/%1").arg(reservationId),
                                                      QUrlQuery(),
                                                      QByteArray(),
                                                      true));
    if (!detail.ok || !detail.json.isObject()) {
        showFailure(detail);
        return;
    }
    applyDetail(detail.json.object());
}

void ReservationDialog::fillRooms(const QJsonArray &items)
{
    const qint64 selected = m_room->currentData().toLongLong();
    m_room->clear();
    for (const QJsonValue &value : items) {
        const QJsonObject room = value.toObject();
        m_room->addItem(room.value(QStringLiteral("code")).toString(), room.value(QStringLiteral("id")).toInteger());
    }
    const int index = m_room->findData(selected);
    if (index >= 0)
        m_room->setCurrentIndex(index);
}

void ReservationDialog::applyDetail(const QJsonObject &body)
{
    const int statusIndex = m_status->findData(body.value(QStringLiteral("status_code")).toString());
    if (statusIndex >= 0)
        m_status->setCurrentIndex(statusIndex);
    m_remarks->setText(body.value(QStringLiteral("remarks")).toString());
    const QJsonArray stays = body.value(QStringLiteral("stays")).toArray();
    if (stays.isEmpty())
        return;
    const QJsonObject stay = stays.at(0).toObject();
    m_stayId = stay.value(QStringLiteral("id")).toInteger();
    m_version = stay.value(QStringLiteral("version")).toInt();
    m_stayState = stay.value(QStringLiteral("state_code")).toString();
    m_stay->setText(stayText(m_stayState));
    m_arrival->setDate(QDate::fromString(stay.value(QStringLiteral("arrival")).toString(), Qt::ISODate));
    m_departure->setDate(QDate::fromString(stay.value(QStringLiteral("departure")).toString(), Qt::ISODate));
    const int roomIndex = m_room->findData(stay.value(QStringLiteral("room_id")).toInteger());
    if (roomIndex >= 0)
        m_room->setCurrentIndex(roomIndex);
    const QJsonObject guest = stay.value(QStringLiteral("guest")).toObject();
    m_lastName->setText(guest.value(QStringLiteral("last_name")).toString());
    m_firstName->setText(guest.value(QStringLiteral("first_name")).toString());
    m_checkIn->setEnabled(m_stayState == QLatin1String("reserved"));
    m_checkOut->setEnabled(m_stayState == QLatin1String("in_house"));
    m_cancelStay->setEnabled(body.value(QStringLiteral("status_code")).toString() != QLatin1String("canceled"));
}

void ReservationDialog::showFailure(const ApiResponse &response)
{
    m_error->setText(response.error.userMessage.isEmpty() ? apiErrorMessage(response.error.httpStatus, response.error.code)
                                                           : response.error.userMessage);
}

void ReservationDialog::save()
{
    if (m_lastName->text().trimmed().isEmpty()) {
        m_error->setText(tr("Last name is required."));
        return;
    }
    if (m_room->currentIndex() < 0) {
        m_error->setText(tr("Choose a room."));
        return;
    }
    if (m_departure->date() <= m_arrival->date()) {
        m_error->setText(tr("The departure date must be after the arrival date."));
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("room_id"), m_room->currentData().toLongLong());
    body.insert(QStringLiteral("arrival"), m_arrival->date().toString(Qt::ISODate));
    body.insert(QStringLiteral("departure"), m_departure->date().toString(Qt::ISODate));
    body.insert(QStringLiteral("status_code"), m_status->currentData().toString());
    body.insert(QStringLiteral("remarks"), m_remarks->text());
    QJsonObject guest;
    guest.insert(QStringLiteral("last_name"), m_lastName->text().trimmed());
    guest.insert(QStringLiteral("first_name"), m_firstName->text().trimmed());
    body.insert(QStringLiteral("guest"), guest);
    ApiResponse response;
    if (m_id == 0) {
        response = waitFor(m_api->request(HttpVerb::Post,
                                          QStringLiteral("/api/v1/reservations"),
                                          QUrlQuery(),
                                          QJsonDocument(body).toJson(QJsonDocument::Compact),
                                          true));
    } else {
        body.insert(QStringLiteral("version"), m_version);
        if (m_stayId > 0)
            body.insert(QStringLiteral("stay_id"), m_stayId);
        response = waitFor(m_api->request(HttpVerb::Patch,
                                          QStringLiteral("/api/v1/reservations/%1").arg(m_id),
                                          QUrlQuery(),
                                          QJsonDocument(body).toJson(QJsonDocument::Compact),
                                          true));
    }
    if (!response.ok) {
        showFailure(response);
        return;
    }
    accept();
}

void ReservationDialog::sendState(const QString &stateCode)
{
    QJsonObject body;
    body.insert(QStringLiteral("version"), m_version);
    body.insert(QStringLiteral("state_code"), stateCode);
    const ApiResponse response = waitFor(m_api->request(HttpVerb::Patch,
                                                        QStringLiteral("/api/v1/reservations/%1").arg(m_id),
                                                        QUrlQuery(),
                                                        QJsonDocument(body).toJson(QJsonDocument::Compact),
                                                        true));
    if (!response.ok || !response.json.isObject()) {
        showFailure(response);
        return;
    }
    applyDetail(response.json.object());
    m_error->clear();
}

void ReservationDialog::sendStatus(const QString &statusCode)
{
    QJsonObject body;
    body.insert(QStringLiteral("version"), m_version);
    body.insert(QStringLiteral("status_code"), statusCode);
    const ApiResponse response = waitFor(m_api->request(HttpVerb::Patch,
                                                        QStringLiteral("/api/v1/reservations/%1").arg(m_id),
                                                        QUrlQuery(),
                                                        QJsonDocument(body).toJson(QJsonDocument::Compact),
                                                        true));
    if (!response.ok || !response.json.isObject()) {
        showFailure(response);
        return;
    }
    applyDetail(response.json.object());
    m_error->clear();
}
