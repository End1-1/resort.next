#include "reservations.h"

#include "db.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSqlError>
#include <QSqlQuery>

namespace {

const char kSchemaMessage[] =
    "nx_reservation audit columns are required; apply next/dbdump/migrations/0002_nx_core.sql, "
    "0003_nx_label.sql, then 0004_nx_audit.sql";

const char *kCreateStatuses[] = {"tentative", "confirmed", "guaranteed", "blocked"};
const char *kReservationStatuses[] = {"tentative", "confirmed", "guaranteed", "blocked", "canceled"};
const char *kBlockingStates[] = {"reserved", "in_house", "out_of_order", "out_of_inventory"};

bool missingSchema(const QSqlError &error)
{
    const QString code = error.nativeErrorCode();
    return code == QLatin1String("1146") || code == QLatin1String("1054");
}

ApiResult databaseFailure(const QString &failure, bool schemaMissing)
{
    if (schemaMissing)
        return apiError(503, "schema_outdated", kSchemaMessage);
    if (failure == QLatin1String("driver_not_loaded"))
        return apiError(503, "driver_not_loaded", "QMYSQL is not loaded");
    if (failure == QLatin1String("access_denied"))
        return apiError(503, "access_denied", "MariaDB refused the configured account");
    if (failure == QLatin1String("unknown_database"))
        return apiError(503, "unknown_database", "MariaDB database was not found");
    if (failure == QLatin1String("cannot_connect"))
        return apiError(503, "cannot_connect", "MariaDB did not accept the connection");
    if (failure == QLatin1String("database_not_configured")) {
        return apiError(503,
                        "database_not_configured",
                        "no database configured; set mysql_host and mysql_schema in hotel-api.ini");
    }
    return apiError(503, "database_unavailable", "MariaDB did not accept the query");
}

bool known(const char *const *table, int count, const QString &value)
{
    for (int i = 0; i < count; ++i) {
        if (value == QLatin1String(table[i]))
            return true;
    }
    return false;
}

bool reservationTransition(const QString &from, const QString &to)
{
    if (from == to)
        return true;
    if (from == QLatin1String("canceled"))
        return to == QLatin1String("tentative") || to == QLatin1String("confirmed") || to == QLatin1String("guaranteed")
            || to == QLatin1String("blocked");
    if (to == QLatin1String("canceled") || to == QLatin1String("tentative") || to == QLatin1String("confirmed")
        || to == QLatin1String("guaranteed") || to == QLatin1String("blocked")) {
        return known(kReservationStatuses, 5, from);
    }
    return false;
}

bool stayTransition(const QString &from, const QString &to)
{
    if (from == to)
        return true;
    if (from == QLatin1String("reserved"))
        return to == QLatin1String("in_house") || to == QLatin1String("canceled");
    if (from == QLatin1String("in_house"))
        return to == QLatin1String("checked_out");
    if (from == QLatin1String("canceled"))
        return to == QLatin1String("reserved");
    return false;
}

QString isoDate(const QVariant &value)
{
    const QDate date = value.toDate();
    if (date.isValid())
        return date.toString(Qt::ISODate);
    return value.toString().left(10);
}

QString isoDateTime(const QVariant &value)
{
    if (value.isNull())
        return {};
    const QDateTime dateTime = value.toDateTime();
    if (dateTime.isValid())
        return dateTime.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
    return value.toString();
}

QJsonValue dateTimeValue(const QVariant &value)
{
    if (value.isNull())
        return QJsonValue::Null;
    return isoDateTime(value);
}

struct StayDraft {
    qint64 roomId = 0;
    QDate arrival;
    QDate departure;
    int adults = 1;
    int children = 0;
    bool hasGuestId = false;
    qint64 guestId = 0;
    bool hasGuestName = false;
    QString firstName;
    QString lastName;
};

bool readDate(const QJsonValue &value, QDate *date)
{
    *date = QDate::fromString(value.toString(), Qt::ISODate);
    return date->isValid();
}

ApiResult invalid(const char *message)
{
    return apiError(400, "invalid_request", message);
}

bool parseStayFields(const QJsonObject &body, StayDraft *draft, bool requireCore, ApiResult *error)
{
    if (body.contains(QStringLiteral("room_id"))) {
        const QJsonValue value = body.value(QStringLiteral("room_id"));
        if (!value.isDouble() || value.toInteger() <= 0)
            *error = invalid("room_id must be a positive integer");
        else
            draft->roomId = value.toInteger();
    } else if (requireCore) {
        *error = invalid("room_id is required");
    }
    if (error->httpStatus == 400)
        return false;

    if (body.contains(QStringLiteral("arrival")) || requireCore) {
        if (!readDate(body.value(QStringLiteral("arrival")), &draft->arrival))
            *error = invalid("arrival must be YYYY-MM-DD");
    }
    if (error->httpStatus == 400)
        return false;
    if (body.contains(QStringLiteral("departure")) || requireCore) {
        if (!readDate(body.value(QStringLiteral("departure")), &draft->departure))
            *error = invalid("departure must be YYYY-MM-DD");
    }
    if (error->httpStatus == 400)
        return false;
    if (draft->arrival.isValid() && draft->departure.isValid() && draft->departure <= draft->arrival) {
        *error = invalid("departure must be after arrival");
        return false;
    }

    if (body.contains(QStringLiteral("adults"))) {
        const int adults = body.value(QStringLiteral("adults")).toInt(-1);
        if (adults < 0) {
            *error = invalid("adults must be zero or greater");
            return false;
        }
        draft->adults = adults;
    }
    if (body.contains(QStringLiteral("children"))) {
        const int children = body.value(QStringLiteral("children")).toInt(-1);
        if (children < 0) {
            *error = invalid("children must be zero or greater");
            return false;
        }
        draft->children = children;
    }
    if (body.contains(QStringLiteral("guest_id"))) {
        const qint64 guestId = body.value(QStringLiteral("guest_id")).toInteger();
        if (guestId <= 0) {
            *error = invalid("guest_id must be a positive integer");
            return false;
        }
        draft->hasGuestId = true;
        draft->guestId = guestId;
    }
    if (body.contains(QStringLiteral("guest"))) {
        const QJsonObject guest = body.value(QStringLiteral("guest")).toObject();
        draft->lastName = guest.value(QStringLiteral("last_name")).toString().trimmed();
        draft->firstName = guest.value(QStringLiteral("first_name")).toString().trimmed();
        if (draft->lastName.isEmpty()) {
            *error = invalid("guest.last_name is required");
            return false;
        }
        draft->hasGuestName = true;
    }
    if (requireCore && !draft->hasGuestId && !draft->hasGuestName) {
        *error = invalid("guest_id or guest.last_name is required");
        return false;
    }
    return true;
}

QString likePattern(QString text)
{
    text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    text.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    text.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    return QStringLiteral("%") + text + QStringLiteral("%");
}

struct Tx {
    QSqlDatabase &db;
    bool open = false;
    bool finished = false;
    bool begin()
    {
        open = db.transaction();
        return open;
    }
    bool commit()
    {
        finished = db.commit();
        return finished;
    }
    ~Tx()
    {
        if (open && !finished)
            db.rollback();
    }
};

ApiResult loadReservation(QSqlDatabase &db, qint64 propertyId, qint64 reservationId, QString *failure)
{
    QSqlQuery header(db);
    if (!header.prepare(QStringLiteral(
            "SELECT id, status_code, remarks, created_at, created_by, updated_at, updated_by "
            "FROM nx_reservation WHERE id = :id AND property_id = :property"))) {
        *failure = header.lastError().nativeErrorCode();
        return databaseFailure({}, missingSchema(header.lastError()));
    }
    header.bindValue(QStringLiteral(":id"), reservationId);
    header.bindValue(QStringLiteral(":property"), propertyId);
    if (!header.exec())
        return databaseFailure({}, missingSchema(header.lastError()));
    if (!header.next())
        return apiError(404, "reservation_not_found", "reservation was not found");

    QJsonObject body;
    body.insert(QStringLiteral("id"), QJsonValue(header.value(0).toLongLong()));
    body.insert(QStringLiteral("status_code"), header.value(1).toString());
    if (header.value(2).isNull())
        body.insert(QStringLiteral("remarks"), QJsonValue::Null);
    else
        body.insert(QStringLiteral("remarks"), header.value(2).toString());
    body.insert(QStringLiteral("created_at"), isoDateTime(header.value(3)));
    if (header.value(4).isNull())
        body.insert(QStringLiteral("created_by"), QJsonValue::Null);
    else
        body.insert(QStringLiteral("created_by"), QJsonValue(header.value(4).toLongLong()));
    body.insert(QStringLiteral("updated_at"), dateTimeValue(header.value(5)));
    if (header.value(6).isNull())
        body.insert(QStringLiteral("updated_by"), QJsonValue::Null);
    else
        body.insert(QStringLiteral("updated_by"), QJsonValue(header.value(6).toLongLong()));

    QSqlQuery stays(db);
    stays.prepare(QStringLiteral(
        "SELECT s.id, s.room_id, room.code, s.arrival, s.departure, s.state_code, s.adults, s.children, s.version, "
        "g.id, g.first_name, g.last_name "
        "FROM nx_stay s "
        "LEFT JOIN nx_room room ON room.id = s.room_id "
        "LEFT JOIN nx_stay_guest sg ON sg.stay_id = s.id AND sg.is_primary = 1 "
        "LEFT JOIN nx_guest g ON g.id = sg.guest_id "
        "WHERE s.reservation_id = :id ORDER BY s.arrival, s.id"));
    stays.bindValue(QStringLiteral(":id"), reservationId);
    if (!stays.exec())
        return databaseFailure({}, missingSchema(stays.lastError()));

    QJsonArray stayItems;
    while (stays.next()) {
        QJsonObject stay;
        stay.insert(QStringLiteral("id"), QJsonValue(stays.value(0).toLongLong()));
        if (stays.value(1).isNull())
            stay.insert(QStringLiteral("room_id"), QJsonValue::Null);
        else
            stay.insert(QStringLiteral("room_id"), QJsonValue(stays.value(1).toLongLong()));
        if (stays.value(2).isNull())
            stay.insert(QStringLiteral("room_code"), QJsonValue::Null);
        else
            stay.insert(QStringLiteral("room_code"), stays.value(2).toString());
        stay.insert(QStringLiteral("arrival"), isoDate(stays.value(3)));
        stay.insert(QStringLiteral("departure"), isoDate(stays.value(4)));
        stay.insert(QStringLiteral("state_code"), stays.value(5).toString());
        stay.insert(QStringLiteral("adults"), stays.value(6).toInt());
        stay.insert(QStringLiteral("children"), stays.value(7).toInt());
        stay.insert(QStringLiteral("version"), stays.value(8).toInt());
        if (stays.value(9).isNull()) {
            stay.insert(QStringLiteral("guest"), QJsonValue::Null);
        } else {
            QJsonObject guest;
            guest.insert(QStringLiteral("id"), QJsonValue(stays.value(9).toLongLong()));
            guest.insert(QStringLiteral("first_name"), stays.value(10).toString());
            guest.insert(QStringLiteral("last_name"), stays.value(11).toString());
            stay.insert(QStringLiteral("guest"), guest);
        }
        stayItems.append(stay);
    }
    body.insert(QStringLiteral("stays"), stayItems);
    ApiResult result;
    result.httpStatus = 200;
    result.body = body;
    return result;
}

bool lockRoom(QSqlDatabase &db, qint64 propertyId, qint64 roomId, ApiResult *error)
{
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
            "SELECT id FROM nx_room WHERE id = :id AND property_id = :property FOR UPDATE"))) {
        *error = databaseFailure({}, missingSchema(query.lastError()));
        return false;
    }
    query.bindValue(QStringLiteral(":id"), roomId);
    query.bindValue(QStringLiteral(":property"), propertyId);
    if (!query.exec()) {
        *error = databaseFailure({}, missingSchema(query.lastError()));
        return false;
    }
    if (!query.next()) {
        *error = apiError(404, "room_not_found", "room was not found");
        return false;
    }
    return true;
}

bool roomIsFree(QSqlDatabase &db, qint64 roomId, qint64 exceptStayId, const QDate &arrival, const QDate &departure, ApiResult *error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT id FROM nx_stay WHERE room_id = :room AND id <> :self "
        "AND state_code IN ('reserved', 'in_house', 'out_of_order', 'out_of_inventory') "
        "AND arrival < :departure AND departure > :arrival LIMIT 1 FOR UPDATE"));
    query.bindValue(QStringLiteral(":room"), roomId);
    query.bindValue(QStringLiteral(":self"), exceptStayId);
    query.bindValue(QStringLiteral(":arrival"), arrival.toString(Qt::ISODate));
    query.bindValue(QStringLiteral(":departure"), departure.toString(Qt::ISODate));
    if (!query.exec()) {
        *error = databaseFailure({}, missingSchema(query.lastError()));
        return false;
    }
    if (query.next()) {
        *error = apiError(409, "overlap", "the room already has a stay on those nights");
        return false;
    }
    return true;
}

bool guestBelongs(QSqlDatabase &db, qint64 propertyId, qint64 guestId, ApiResult *error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT id FROM nx_guest WHERE id = :id AND property_id = :property"));
    query.bindValue(QStringLiteral(":id"), guestId);
    query.bindValue(QStringLiteral(":property"), propertyId);
    if (!query.exec()) {
        *error = databaseFailure({}, missingSchema(query.lastError()));
        return false;
    }
    if (!query.next()) {
        *error = apiError(404, "guest_not_found", "guest was not found");
        return false;
    }
    return true;
}

bool insertGuest(QSqlDatabase &db, qint64 propertyId, const QString &first, const QString &last, qint64 *guestId, ApiResult *error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO nx_guest (property_id, first_name, last_name) VALUES (:property, :first, :last)"));
    query.bindValue(QStringLiteral(":property"), propertyId);
    query.bindValue(QStringLiteral(":first"), first);
    query.bindValue(QStringLiteral(":last"), last);
    if (!query.exec()) {
        *error = databaseFailure({}, missingSchema(query.lastError()));
        return false;
    }
    *guestId = query.lastInsertId().toLongLong();
    return true;
}

bool linkPrimary(QSqlDatabase &db, qint64 stayId, qint64 guestId, ApiResult *error)
{
    QSqlQuery clear(db);
    clear.prepare(QStringLiteral("UPDATE nx_stay_guest SET is_primary = 0 WHERE stay_id = :stay"));
    clear.bindValue(QStringLiteral(":stay"), stayId);
    if (!clear.exec()) {
        *error = databaseFailure({}, missingSchema(clear.lastError()));
        return false;
    }
    QSqlQuery link(db);
    link.prepare(QStringLiteral(
        "INSERT INTO nx_stay_guest (stay_id, guest_id, is_primary) VALUES (:stay, :guest, 1) "
        "ON DUPLICATE KEY UPDATE is_primary = 1"));
    link.bindValue(QStringLiteral(":stay"), stayId);
    link.bindValue(QStringLiteral(":guest"), guestId);
    if (!link.exec()) {
        *error = databaseFailure({}, missingSchema(link.lastError()));
        return false;
    }
    return true;
}

void touchReservation(QSqlQuery *query, qint64 userId)
{
    query->bindValue(QStringLiteral(":user"), userId);
}

} // namespace

ApiResult listReservations(const DatabaseTarget &target,
                           int connectTimeoutSec,
                           qint64 propertyId,
                           const QString &fromText,
                           const QString &toText,
                           const QString &guest,
                           const QString &status,
                           const QString &roomCode,
                           const QString &roomIdText)
{
    const bool hasFrom = !fromText.trimmed().isEmpty();
    const bool hasTo = !toText.trimmed().isEmpty();
    QDate from;
    QDate to;
    if (hasFrom != hasTo)
        return invalid("from and to must be sent together");
    if (hasFrom) {
        from = QDate::fromString(fromText.trimmed(), Qt::ISODate);
        to = QDate::fromString(toText.trimmed(), Qt::ISODate);
        if (!from.isValid() || !to.isValid() || to <= from)
            return invalid("from and to must be YYYY-MM-DD dates with to after from");
    }
    if (!status.trimmed().isEmpty() && !known(kReservationStatuses, 5, status.trimmed()))
        return invalid("status is not a reservation status");
    qint64 roomId = 0;
    if (!roomIdText.trimmed().isEmpty()) {
        bool ok = false;
        roomId = roomIdText.trimmed().toLongLong(&ok);
        if (!ok || roomId <= 0)
            return invalid("room_id must be a positive integer");
    }
    if (!target.configured)
        return databaseFailure(QStringLiteral("database_not_configured"), false);

    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened)
        return databaseFailure(connection.failure, false);

    QString sql = QStringLiteral(
        "SELECT res.id, res.status_code, res.created_at, res.updated_at, "
        "MIN(s.arrival), MAX(s.departure), "
        "(SELECT room.code FROM nx_stay sx LEFT JOIN nx_room room ON room.id = sx.room_id "
        "WHERE sx.reservation_id = res.id ORDER BY sx.arrival, sx.id LIMIT 1), "
        "(SELECT sx.state_code FROM nx_stay sx WHERE sx.reservation_id = res.id ORDER BY sx.arrival, sx.id LIMIT 1), "
        "(SELECT TRIM(CONCAT(g.last_name, ' ', g.first_name)) FROM nx_stay sx "
        "JOIN nx_stay_guest sg ON sg.stay_id = sx.id AND sg.is_primary = 1 "
        "JOIN nx_guest g ON g.id = sg.guest_id WHERE sx.reservation_id = res.id "
        "ORDER BY sx.arrival, sx.id LIMIT 1) "
        "FROM nx_reservation res LEFT JOIN nx_stay s ON s.reservation_id = res.id "
        "WHERE res.property_id = :property");
    if (!status.trimmed().isEmpty())
        sql += QStringLiteral(" AND res.status_code = :status");
    if (hasFrom) {
        sql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM nx_stay ds WHERE ds.reservation_id = res.id "
            "AND ds.arrival < :to_date AND ds.departure > :from_date)");
    }
    if (!guest.trimmed().isEmpty()) {
        sql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM nx_stay gs "
            "JOIN nx_stay_guest sg ON sg.stay_id = gs.id JOIN nx_guest g ON g.id = sg.guest_id "
            "WHERE gs.reservation_id = res.id AND (g.last_name LIKE :guest ESCAPE '\\\\' "
            "OR g.first_name LIKE :guest ESCAPE '\\\\'))");
    }
    if (!roomCode.trimmed().isEmpty()) {
        sql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM nx_stay rs JOIN nx_room room ON room.id = rs.room_id "
            "WHERE rs.reservation_id = res.id AND room.code = :room_code)");
    }
    if (roomId > 0) {
        sql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM nx_stay rs WHERE rs.reservation_id = res.id AND rs.room_id = :room_id)");
    }
    sql += QStringLiteral(" GROUP BY res.id, res.status_code, res.created_at, res.updated_at ORDER BY MIN(s.arrival), res.id");

    QSqlQuery query(connection.db);
    query.setForwardOnly(true);
    if (!query.prepare(sql))
        return databaseFailure(connection.failure, missingSchema(query.lastError()));
    query.bindValue(QStringLiteral(":property"), propertyId);
    if (!status.trimmed().isEmpty())
        query.bindValue(QStringLiteral(":status"), status.trimmed());
    if (hasFrom) {
        query.bindValue(QStringLiteral(":from_date"), from.toString(Qt::ISODate));
        query.bindValue(QStringLiteral(":to_date"), to.toString(Qt::ISODate));
    }
    if (!guest.trimmed().isEmpty())
        query.bindValue(QStringLiteral(":guest"), likePattern(guest.trimmed()));
    if (!roomCode.trimmed().isEmpty())
        query.bindValue(QStringLiteral(":room_code"), roomCode.trimmed());
    if (roomId > 0)
        query.bindValue(QStringLiteral(":room_id"), roomId);
    if (!query.exec())
        return databaseFailure(connection.failure, missingSchema(query.lastError()));

    QJsonArray items;
    while (query.next()) {
        QJsonObject row;
        row.insert(QStringLiteral("id"), QJsonValue(query.value(0).toLongLong()));
        row.insert(QStringLiteral("status_code"), query.value(1).toString());
        row.insert(QStringLiteral("created_at"), isoDateTime(query.value(2)));
        row.insert(QStringLiteral("updated_at"), dateTimeValue(query.value(3)));
        if (query.value(4).isNull())
            row.insert(QStringLiteral("arrival"), QJsonValue::Null);
        else
            row.insert(QStringLiteral("arrival"), isoDate(query.value(4)));
        if (query.value(5).isNull())
            row.insert(QStringLiteral("departure"), QJsonValue::Null);
        else
            row.insert(QStringLiteral("departure"), isoDate(query.value(5)));
        if (query.value(6).isNull())
            row.insert(QStringLiteral("room_code"), QJsonValue::Null);
        else
            row.insert(QStringLiteral("room_code"), query.value(6).toString());
        if (query.value(7).isNull())
            row.insert(QStringLiteral("stay_state"), QJsonValue::Null);
        else
            row.insert(QStringLiteral("stay_state"), query.value(7).toString());
        if (query.value(8).isNull())
            row.insert(QStringLiteral("guest_name"), QString());
        else
            row.insert(QStringLiteral("guest_name"), query.value(8).toString());
        items.append(row);
    }
    QJsonObject body;
    body.insert(QStringLiteral("items"), items);
    ApiResult result;
    result.httpStatus = 200;
    result.body = body;
    return result;
}

ApiResult getReservation(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, qint64 reservationId)
{
    if (reservationId <= 0)
        return apiError(404, "reservation_not_found", "reservation was not found");
    if (!target.configured)
        return databaseFailure(QStringLiteral("database_not_configured"), false);
    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened)
        return databaseFailure(connection.failure, false);
    QString failure;
    return loadReservation(connection.db, propertyId, reservationId, &failure);
}

ApiResult createReservation(const DatabaseTarget &target,
                            int connectTimeoutSec,
                            qint64 propertyId,
                            qint64 userId,
                            const QByteArray &bodyBytes)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bodyBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return invalid("body must be a JSON object");
    const QJsonObject body = document.object();
    QString status = body.value(QStringLiteral("status_code")).toString(QStringLiteral("confirmed"));
    if (!known(kCreateStatuses, 4, status))
        return invalid("status_code must be tentative, confirmed, guaranteed, or blocked");
    const QString remarks = body.value(QStringLiteral("remarks")).toString();
    if (remarks.size() > 4000)
        return invalid("remarks is too long");
    StayDraft draft;
    ApiResult error;
    if (!parseStayFields(body, &draft, true, &error))
        return error;
    if (!target.configured)
        return databaseFailure(QStringLiteral("database_not_configured"), false);

    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened)
        return databaseFailure(connection.failure, false);
    Tx tx{connection.db};
    if (!tx.begin())
        return databaseFailure(connection.failure, false);
    if (!lockRoom(connection.db, propertyId, draft.roomId, &error))
        return error;
    if (!roomIsFree(connection.db, draft.roomId, 0, draft.arrival, draft.departure, &error))
        return error;

    qint64 guestId = draft.guestId;
    if (draft.hasGuestId) {
        if (!guestBelongs(connection.db, propertyId, guestId, &error))
            return error;
    } else if (!insertGuest(connection.db, propertyId, draft.firstName, draft.lastName, &guestId, &error)) {
        return error;
    }

    QSqlQuery reservation(connection.db);
    reservation.prepare(QStringLiteral(
        "INSERT INTO nx_reservation (property_id, status_code, remarks, created_at, created_by) "
        "VALUES (:property, :status, :remarks, UTC_TIMESTAMP(), :user)"));
    reservation.bindValue(QStringLiteral(":property"), propertyId);
    reservation.bindValue(QStringLiteral(":status"), status);
    if (remarks.isEmpty())
        reservation.bindValue(QStringLiteral(":remarks"), QVariant());
    else
        reservation.bindValue(QStringLiteral(":remarks"), remarks);
    touchReservation(&reservation, userId);
    if (!reservation.exec())
        return databaseFailure(connection.failure, missingSchema(reservation.lastError()));
    const qint64 reservationId = reservation.lastInsertId().toLongLong();

    QSqlQuery stay(connection.db);
    stay.prepare(QStringLiteral(
        "INSERT INTO nx_stay (reservation_id, room_id, arrival, departure, state_code, adults, children, version, updated_by) "
        "VALUES (:reservation, :room, :arrival, :departure, 'reserved', :adults, :children, 0, :user)"));
    stay.bindValue(QStringLiteral(":reservation"), reservationId);
    stay.bindValue(QStringLiteral(":room"), draft.roomId);
    stay.bindValue(QStringLiteral(":arrival"), draft.arrival.toString(Qt::ISODate));
    stay.bindValue(QStringLiteral(":departure"), draft.departure.toString(Qt::ISODate));
    stay.bindValue(QStringLiteral(":adults"), draft.adults);
    stay.bindValue(QStringLiteral(":children"), draft.children);
    touchReservation(&stay, userId);
    if (!stay.exec())
        return databaseFailure(connection.failure, missingSchema(stay.lastError()));
    const qint64 stayId = stay.lastInsertId().toLongLong();
    if (!linkPrimary(connection.db, stayId, guestId, &error))
        return error;
    if (!tx.commit())
        return databaseFailure(connection.failure, false);

    QString failure;
    ApiResult created = loadReservation(connection.db, propertyId, reservationId, &failure);
    if (created.httpStatus == 200)
        created.httpStatus = 201;
    return created;
}

ApiResult updateReservation(const DatabaseTarget &target,
                            int connectTimeoutSec,
                            qint64 propertyId,
                            qint64 userId,
                            qint64 reservationId,
                            const QByteArray &bodyBytes)
{
    if (reservationId <= 0)
        return apiError(404, "reservation_not_found", "reservation was not found");
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bodyBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return invalid("body must be a JSON object");
    const QJsonObject body = document.object();
    if (!body.contains(QStringLiteral("version")))
        return invalid("version is required");
    const int version = body.value(QStringLiteral("version")).toInt(-1);
    if (version < 0)
        return invalid("version must be zero or greater");
    if (body.contains(QStringLiteral("remarks")) && body.value(QStringLiteral("remarks")).toString().size() > 4000)
        return invalid("remarks is too long");
    if (body.contains(QStringLiteral("status_code"))
        && !known(kReservationStatuses, 5, body.value(QStringLiteral("status_code")).toString())) {
        return invalid("status_code is not a reservation status");
    }
    if (!target.configured)
        return databaseFailure(QStringLiteral("database_not_configured"), false);

    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened)
        return databaseFailure(connection.failure, false);
    Tx tx{connection.db};
    if (!tx.begin())
        return databaseFailure(connection.failure, false);

    QSqlQuery header(connection.db);
    header.prepare(QStringLiteral(
        "SELECT status_code FROM nx_reservation WHERE id = :id AND property_id = :property FOR UPDATE"));
    header.bindValue(QStringLiteral(":id"), reservationId);
    header.bindValue(QStringLiteral(":property"), propertyId);
    if (!header.exec())
        return databaseFailure(connection.failure, missingSchema(header.lastError()));
    if (!header.next())
        return apiError(404, "reservation_not_found", "reservation was not found");
    QString reservationStatus = header.value(0).toString();

    QSqlQuery stays(connection.db);
    stays.prepare(QStringLiteral(
        "SELECT id, room_id, arrival, departure, state_code, adults, children, version "
        "FROM nx_stay WHERE reservation_id = :id ORDER BY arrival, id FOR UPDATE"));
    stays.bindValue(QStringLiteral(":id"), reservationId);
    if (!stays.exec())
        return databaseFailure(connection.failure, missingSchema(stays.lastError()));

    struct StayRow {
        qint64 id = 0;
        qint64 roomId = 0;
        bool hasRoom = false;
        QDate arrival;
        QDate departure;
        QString state;
        int adults = 0;
        int children = 0;
        int version = 0;
    };
    QVector<StayRow> rows;
    while (stays.next()) {
        StayRow row;
        row.id = stays.value(0).toLongLong();
        row.hasRoom = !stays.value(1).isNull();
        row.roomId = stays.value(1).toLongLong();
        row.arrival = stays.value(2).toDate();
        row.departure = stays.value(3).toDate();
        row.state = stays.value(4).toString();
        row.adults = stays.value(5).toInt();
        row.children = stays.value(6).toInt();
        row.version = stays.value(7).toInt();
        rows.append(row);
    }
    if (rows.isEmpty())
        return apiError(409, "invalid_transition", "the reservation has no stay to edit");

    int index = 0;
    if (body.contains(QStringLiteral("stay_id"))) {
        const qint64 stayId = body.value(QStringLiteral("stay_id")).toInteger();
        index = -1;
        for (int i = 0; i < rows.size(); ++i) {
            if (rows.at(i).id == stayId)
                index = i;
        }
        if (index < 0)
            return apiError(404, "reservation_not_found", "stay was not found on this reservation");
    } else if (rows.size() != 1) {
        return invalid("stay_id is required when the reservation has several stays");
    }
    StayRow stay = rows.at(index);
    if (stay.version != version)
        return apiError(409, "version_conflict", "the stay was changed; reload and try again");

    const bool datesOrRoom = body.contains(QStringLiteral("arrival")) || body.contains(QStringLiteral("departure"))
        || body.contains(QStringLiteral("room_id"));
    if (datesOrRoom && stay.state != QLatin1String("reserved") && stay.state != QLatin1String("canceled"))
        return apiError(409, "stay_locked", "dates and room cannot change while the guest is in house or checked out");

    StayDraft draft;
    draft.roomId = stay.roomId;
    draft.arrival = stay.arrival;
    draft.departure = stay.departure;
    draft.adults = stay.adults;
    draft.children = stay.children;
    ApiResult error;
    if (!parseStayFields(body, &draft, false, &error))
        return error;
    if (body.contains(QStringLiteral("arrival")) != body.contains(QStringLiteral("departure"))
        && !(draft.arrival.isValid() && draft.departure.isValid())) {
        return invalid("arrival and departure must be sent together");
    }
    if (draft.departure <= draft.arrival)
        return invalid("departure must be after arrival");

    QString nextState = stay.state;
    if (body.contains(QStringLiteral("state_code"))) {
        nextState = body.value(QStringLiteral("state_code")).toString();
        if (!stayTransition(stay.state, nextState))
            return apiError(409, "invalid_transition", "that stay status change is not allowed");
        if (nextState == QLatin1String("reserved") && reservationStatus == QLatin1String("canceled")
            && !body.contains(QStringLiteral("status_code"))) {
            return apiError(409, "invalid_transition", "reinstate the reservation before the stay");
        }
    }

    QString nextStatus = reservationStatus;
    if (body.contains(QStringLiteral("status_code"))) {
        nextStatus = body.value(QStringLiteral("status_code")).toString();
        if (!reservationTransition(reservationStatus, nextStatus))
            return apiError(409, "invalid_transition", "that reservation status change is not allowed");
    }
    if (nextStatus == QLatin1String("canceled")) {
        for (const StayRow &row : rows) {
            const QString state = row.id == stay.id ? nextState : row.state;
            if (state == QLatin1String("in_house"))
                return apiError(409, "stay_in_house", "check the guest out before canceling the reservation");
        }
        if (stay.state == QLatin1String("reserved") || stay.state == QLatin1String("canceled"))
            nextState = QLatin1String("canceled");
    } else if (reservationStatus == QLatin1String("canceled") && stay.state == QLatin1String("canceled")
               && !body.contains(QStringLiteral("state_code"))) {
        nextState = QLatin1String("reserved");
    }

    const bool blocks = known(kBlockingStates, 4, nextState);
    if (blocks) {
        if (draft.roomId <= 0)
            return invalid("room_id is required while the stay holds the room");
        if (!lockRoom(connection.db, propertyId, draft.roomId, &error))
            return error;
        if (stay.hasRoom && stay.roomId != draft.roomId) {
            if (!lockRoom(connection.db, propertyId, stay.roomId, &error))
                return error;
        }
        if (!roomIsFree(connection.db, draft.roomId, stay.id, draft.arrival, draft.departure, &error))
            return error;
    }

    qint64 guestId = 0;
    if (draft.hasGuestId) {
        if (!guestBelongs(connection.db, propertyId, draft.guestId, &error))
            return error;
        guestId = draft.guestId;
    } else if (draft.hasGuestName) {
        QSqlQuery primary(connection.db);
        primary.prepare(QStringLiteral(
            "SELECT guest_id FROM nx_stay_guest WHERE stay_id = :stay AND is_primary = 1 ORDER BY id LIMIT 1"));
        primary.bindValue(QStringLiteral(":stay"), stay.id);
        if (!primary.exec())
            return databaseFailure(connection.failure, missingSchema(primary.lastError()));
        if (primary.next()) {
            guestId = primary.value(0).toLongLong();
            QSqlQuery rename(connection.db);
            rename.prepare(QStringLiteral(
                "UPDATE nx_guest SET first_name = :first, last_name = :last WHERE id = :id AND property_id = :property"));
            rename.bindValue(QStringLiteral(":first"), draft.firstName);
            rename.bindValue(QStringLiteral(":last"), draft.lastName);
            rename.bindValue(QStringLiteral(":id"), guestId);
            rename.bindValue(QStringLiteral(":property"), propertyId);
            if (!rename.exec())
                return databaseFailure(connection.failure, missingSchema(rename.lastError()));
        } else if (!insertGuest(connection.db, propertyId, draft.firstName, draft.lastName, &guestId, &error)) {
            return error;
        }
    }

    if (nextStatus == QLatin1String("canceled")) {
        QSqlQuery cancel(connection.db);
        cancel.prepare(QStringLiteral(
            "UPDATE nx_stay SET state_code = 'canceled', updated_at = UTC_TIMESTAMP(), updated_by = :user, "
            "version = version + 1 WHERE reservation_id = :id AND id <> :stay AND state_code = 'reserved'"));
        touchReservation(&cancel, userId);
        cancel.bindValue(QStringLiteral(":id"), reservationId);
        cancel.bindValue(QStringLiteral(":stay"), stay.id);
        if (!cancel.exec())
            return databaseFailure(connection.failure, missingSchema(cancel.lastError()));
    } else if (reservationStatus == QLatin1String("canceled") && nextStatus != QLatin1String("canceled")) {
        for (const StayRow &row : rows) {
            if (row.state != QLatin1String("canceled"))
                continue;
            const qint64 room = row.id == stay.id ? draft.roomId : row.roomId;
            const QDate arrival = row.id == stay.id ? draft.arrival : row.arrival;
            const QDate departure = row.id == stay.id ? draft.departure : row.departure;
            if (room <= 0)
                continue;
            if (!lockRoom(connection.db, propertyId, room, &error))
                return error;
            if (!roomIsFree(connection.db, room, row.id, arrival, departure, &error))
                return error;
        }
        QSqlQuery restore(connection.db);
        restore.prepare(QStringLiteral(
            "UPDATE nx_stay SET state_code = 'reserved', updated_at = UTC_TIMESTAMP(), updated_by = :user, "
            "version = version + 1 WHERE reservation_id = :id AND state_code = 'canceled' AND id <> :stay"));
        touchReservation(&restore, userId);
        restore.bindValue(QStringLiteral(":id"), reservationId);
        restore.bindValue(QStringLiteral(":stay"), stay.id);
        if (!restore.exec())
            return databaseFailure(connection.failure, missingSchema(restore.lastError()));
    }

    QSqlQuery updateStay(connection.db);
    updateStay.prepare(QStringLiteral(
        "UPDATE nx_stay SET room_id = :room, arrival = :arrival, departure = :departure, state_code = :state, "
        "adults = :adults, children = :children, version = version + 1, updated_at = UTC_TIMESTAMP(), updated_by = :user "
        "WHERE id = :id AND version = :version"));
    updateStay.bindValue(QStringLiteral(":room"), draft.roomId > 0 ? QVariant(draft.roomId) : QVariant());
    updateStay.bindValue(QStringLiteral(":arrival"), draft.arrival.toString(Qt::ISODate));
    updateStay.bindValue(QStringLiteral(":departure"), draft.departure.toString(Qt::ISODate));
    updateStay.bindValue(QStringLiteral(":state"), nextState);
    updateStay.bindValue(QStringLiteral(":adults"), draft.adults);
    updateStay.bindValue(QStringLiteral(":children"), draft.children);
    touchReservation(&updateStay, userId);
    updateStay.bindValue(QStringLiteral(":id"), stay.id);
    updateStay.bindValue(QStringLiteral(":version"), version);
    if (!updateStay.exec())
        return databaseFailure(connection.failure, missingSchema(updateStay.lastError()));
    if (updateStay.numRowsAffected() != 1)
        return apiError(409, "version_conflict", "the stay was changed; reload and try again");

    if (guestId > 0 && !linkPrimary(connection.db, stay.id, guestId, &error))
        return error;

    if (nextState == QLatin1String("in_house") && draft.roomId > 0) {
        QSqlQuery room(connection.db);
        room.prepare(QStringLiteral("UPDATE nx_room SET status_code = 'occupied' WHERE id = :id"));
        room.bindValue(QStringLiteral(":id"), draft.roomId);
        if (!room.exec())
            return databaseFailure(connection.failure, missingSchema(room.lastError()));
    }
    if (nextState == QLatin1String("checked_out") && draft.roomId > 0) {
        QSqlQuery room(connection.db);
        room.prepare(QStringLiteral("UPDATE nx_room SET status_code = 'vacant_dirty' WHERE id = :id"));
        room.bindValue(QStringLiteral(":id"), draft.roomId);
        if (!room.exec())
            return databaseFailure(connection.failure, missingSchema(room.lastError()));
    }

    QSqlQuery touch(connection.db);
    if (body.contains(QStringLiteral("remarks"))) {
        touch.prepare(QStringLiteral(
            "UPDATE nx_reservation SET status_code = :status, remarks = :remarks, updated_at = UTC_TIMESTAMP(), "
            "updated_by = :user WHERE id = :id"));
        const QString remarks = body.value(QStringLiteral("remarks")).toString();
        touch.bindValue(QStringLiteral(":remarks"), remarks.isEmpty() ? QVariant() : QVariant(remarks));
    } else {
        touch.prepare(QStringLiteral(
            "UPDATE nx_reservation SET status_code = :status, updated_at = UTC_TIMESTAMP(), updated_by = :user "
            "WHERE id = :id"));
    }
    touch.bindValue(QStringLiteral(":status"), nextStatus);
    touchReservation(&touch, userId);
    touch.bindValue(QStringLiteral(":id"), reservationId);
    if (!touch.exec())
        return databaseFailure(connection.failure, missingSchema(touch.lastError()));
    if (!tx.commit())
        return databaseFailure(connection.failure, false);

    QString failure;
    return loadReservation(connection.db, propertyId, reservationId, &failure);
}
