#include "rack.h"

#include "db.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QSqlError>
#include <QSqlQuery>

namespace {

const char kSchemaMessage[] =
    "nx_room, nx_stay, nx_reservation, and nx_label are required; apply "
    "next/dbdump/migrations/0002_nx_core.sql then 0003_nx_label.sql";

const int kMaxNights = 120;

bool missingTable(const QSqlError &error)
{
    return error.nativeErrorCode() == QLatin1String("1146");
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

QString isoDate(const QVariant &value)
{
    const QDate asDate = value.toDate();
    if (asDate.isValid())
        return asDate.toString(Qt::ISODate);
    return value.toString().left(10);
}

} // namespace

RackRange parseRackRange(const QString &fromText, const QString &toText)
{
    RackRange range;
    range.from = QDate::fromString(fromText.trimmed(), Qt::ISODate);
    range.to = QDate::fromString(toText.trimmed(), Qt::ISODate);
    if (!range.from.isValid() || !range.to.isValid() || range.to <= range.from)
        return range;
    const qint64 nights = range.from.daysTo(range.to);
    if (nights > kMaxNights) {
        range.httpStatus = 400;
        range.code = "range_too_long";
        range.message = "the rack window is at most 120 nights";
        return range;
    }
    range.ok = true;
    range.httpStatus = 200;
    range.code = nullptr;
    range.message = nullptr;
    return range;
}

ApiResult occupancyChart(const DatabaseTarget &target,
                         int connectTimeoutSec,
                         qint64 propertyId,
                         const QString &locale,
                         const QString &fromText,
                         const QString &toText)
{
    const RackRange range = parseRackRange(fromText, toText);
    if (!range.ok)
        return apiError(range.httpStatus, range.code, range.message);
    if (!target.configured)
        return databaseFailure(QStringLiteral("database_not_configured"), false);

    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened)
        return databaseFailure(connection.failure, false);

    const QString typeName = QStringLiteral(
        "COALESCE("
        "(SELECT l.name FROM nx_label l WHERE l.owner_table = :owner AND l.owner_id = t.id AND l.locale = :locale LIMIT 1),"
        "(SELECT l.name FROM nx_label l WHERE l.owner_table = :owner_ru AND l.owner_id = t.id AND l.locale = 'ru' LIMIT 1),"
        "NULLIF(t.name, ''),"
        "t.code)");
    const QString buildingExpr = QStringLiteral(
        "COALESCE("
        "(SELECT l.name FROM nx_label l WHERE l.owner_table = :owner_b AND l.owner_id = b.id AND l.locale = :locale_b LIMIT 1),"
        "(SELECT l.name FROM nx_label l WHERE l.owner_table = :owner_b_ru AND l.owner_id = b.id AND l.locale = 'ru' LIMIT 1),"
        "NULLIF(b.name, ''),"
        "b.code)");
    const QString roomSql = QStringLiteral(
                                "SELECT r.id, r.code, r.floor, r.status_code, %1, %2 "
                                "FROM nx_room r "
                                "INNER JOIN nx_room_type t ON t.id = r.room_type_id "
                                "LEFT JOIN nx_building b ON b.id = r.building_id "
                                "WHERE r.property_id = :property "
                                "ORDER BY r.floor, r.code, r.id")
                                .arg(typeName, buildingExpr);

    QSqlQuery rooms(connection.db);
    rooms.setForwardOnly(true);
    if (!rooms.prepare(roomSql))
        return databaseFailure(connection.failure, missingTable(rooms.lastError()));
    rooms.bindValue(QStringLiteral(":property"), propertyId);
    rooms.bindValue(QStringLiteral(":locale"), locale);
    rooms.bindValue(QStringLiteral(":owner"), QStringLiteral("nx_room_type"));
    rooms.bindValue(QStringLiteral(":owner_ru"), QStringLiteral("nx_room_type"));
    rooms.bindValue(QStringLiteral(":locale_b"), locale);
    rooms.bindValue(QStringLiteral(":owner_b"), QStringLiteral("nx_building"));
    rooms.bindValue(QStringLiteral(":owner_b_ru"), QStringLiteral("nx_building"));
    if (!rooms.exec())
        return databaseFailure(connection.failure, missingTable(rooms.lastError()));

    QHash<qint64, int> indexByRoom;
    QJsonArray roomItems;
    while (rooms.next()) {
        QJsonObject row;
        const qint64 id = rooms.value(0).toLongLong();
        row.insert(QStringLiteral("id"), QJsonValue(id));
        row.insert(QStringLiteral("code"), rooms.value(1).toString());
        if (rooms.value(2).isNull())
            row.insert(QStringLiteral("floor"), QJsonValue::Null);
        else
            row.insert(QStringLiteral("floor"), rooms.value(2).toInt());
        row.insert(QStringLiteral("status_code"), rooms.value(3).toString());
        row.insert(QStringLiteral("type_name"), rooms.value(4).toString());
        if (rooms.value(5).isNull())
            row.insert(QStringLiteral("building_name"), QJsonValue::Null);
        else
            row.insert(QStringLiteral("building_name"), rooms.value(5).toString());
        row.insert(QStringLiteral("blocks"), QJsonArray());
        indexByRoom.insert(id, roomItems.size());
        roomItems.append(row);
    }

    QSqlQuery stays(connection.db);
    stays.setForwardOnly(true);
    const QString staySql = QStringLiteral(
        "SELECT s.id, s.reservation_id, s.room_id, s.arrival, s.departure, s.state_code, res.status_code, "
        "(SELECT TRIM(CONCAT(g.last_name, ' ', g.first_name)) FROM nx_stay_guest sg "
        "JOIN nx_guest g ON g.id = sg.guest_id WHERE sg.stay_id = s.id "
        "ORDER BY sg.is_primary DESC, sg.id LIMIT 1) "
        "FROM nx_stay s "
        "INNER JOIN nx_reservation res ON res.id = s.reservation_id "
        "WHERE res.property_id = :property "
        "AND s.room_id IS NOT NULL "
        "AND s.arrival < :to_date AND s.departure > :from_date "
        "AND s.state_code <> 'canceled' AND res.status_code <> 'canceled' "
        "ORDER BY s.room_id, s.arrival, s.id");
    if (!stays.prepare(staySql))
        return databaseFailure(connection.failure, missingTable(stays.lastError()));
    stays.bindValue(QStringLiteral(":property"), propertyId);
    stays.bindValue(QStringLiteral(":from_date"), range.from.toString(Qt::ISODate));
    stays.bindValue(QStringLiteral(":to_date"), range.to.toString(Qt::ISODate));
    if (!stays.exec())
        return databaseFailure(connection.failure, missingTable(stays.lastError()));

    while (stays.next()) {
        const qint64 roomId = stays.value(2).toLongLong();
        const auto found = indexByRoom.constFind(roomId);
        if (found == indexByRoom.cend())
            continue;
        QJsonObject block;
        block.insert(QStringLiteral("stay_id"), QJsonValue(stays.value(0).toLongLong()));
        block.insert(QStringLiteral("reservation_id"), QJsonValue(stays.value(1).toLongLong()));
        if (stays.value(7).isNull())
            block.insert(QStringLiteral("guest_name"), QString());
        else
            block.insert(QStringLiteral("guest_name"), stays.value(7).toString());
        block.insert(QStringLiteral("state_code"), stays.value(5).toString());
        block.insert(QStringLiteral("reservation_status"), stays.value(6).toString());
        block.insert(QStringLiteral("arrival"), isoDate(stays.value(3)));
        block.insert(QStringLiteral("departure"), isoDate(stays.value(4)));

        QJsonObject room = roomItems.at(found.value()).toObject();
        QJsonArray blocks = room.value(QStringLiteral("blocks")).toArray();
        blocks.append(block);
        room.insert(QStringLiteral("blocks"), blocks);
        roomItems.replace(found.value(), room);
    }

    QJsonObject body;
    body.insert(QStringLiteral("lang"), locale);
    body.insert(QStringLiteral("from"), range.from.toString(Qt::ISODate));
    body.insert(QStringLiteral("to"), range.to.toString(Qt::ISODate));
    body.insert(QStringLiteral("rooms"), roomItems);
    ApiResult result;
    result.httpStatus = 200;
    result.body = body;
    return result;
}
