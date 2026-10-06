#include "dictionaries.h"

#include "db.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

namespace {

const char kSchemaMessage[] =
    "nx_room, nx_room_type, nx_building, and nx_label are required; apply "
    "next/dbdump/migrations/0002_nx_core.sql then 0003_nx_label.sql";

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

QString knownLocale(QString token)
{
    token = token.trimmed().toLower();
    const int semi = token.indexOf(QLatin1Char(';'));
    if (semi >= 0)
        token = token.left(semi).trimmed();
    const int dash = token.indexOf(QLatin1Char('-'));
    if (dash > 0)
        token = token.left(dash);
    const int under = token.indexOf(QLatin1Char('_'));
    if (under > 0)
        token = token.left(under);
    if (token == QLatin1String("hy") || token == QLatin1String("en") || token == QLatin1String("ru"))
        return token;
    return {};
}

// Requested locale, then ru, then the single name column, then code.
QString localizedNameSql(const QString &idExpr, const QString &nameExpr, const QString &codeExpr)
{
    return QStringLiteral(
               "COALESCE("
               "(SELECT l.name FROM nx_label l WHERE l.owner_table = :owner AND l.owner_id = %1 AND l.locale = :locale LIMIT 1),"
               "(SELECT l.name FROM nx_label l WHERE l.owner_table = :owner_ru AND l.owner_id = %1 AND l.locale = 'ru' LIMIT 1),"
               "NULLIF(%2, ''),"
               "%3)")
        .arg(idExpr, nameExpr, codeExpr);
}

} // namespace

QString localeFromRequest(const QString &langQuery, const QByteArray &acceptLanguage)
{
    const QString fromQuery = knownLocale(langQuery);
    if (!fromQuery.isEmpty())
        return fromQuery;
    const QStringList parts = QString::fromLatin1(acceptLanguage).split(QLatin1Char(','));
    for (const QString &part : parts) {
        const QString code = knownLocale(part);
        if (!code.isEmpty())
            return code;
    }
    return QStringLiteral("ru");
}

ApiResult listRoomStatuses(const QString &locale)
{
    static const char *kCodes[] = {
        "vacant_ready",
        "occupied",
        "vacant_dirty",
        "out_of_order",
        "house_use",
        "complimentary",
        "out_of_inventory",
    };
    QJsonArray items;
    for (const char *code : kCodes) {
        QJsonObject row;
        row.insert(QStringLiteral("code"), QLatin1String(code));
        items.append(row);
    }
    QJsonObject body;
    body.insert(QStringLiteral("lang"), locale);
    body.insert(QStringLiteral("items"), items);
    ApiResult result;
    result.httpStatus = 200;
    result.body = body;
    return result;
}

ApiResult listRoomTypes(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, const QString &locale)
{
    if (!target.configured)
        return databaseFailure(QStringLiteral("database_not_configured"), false);
    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened)
        return databaseFailure(connection.failure, false);

    const QString name = localizedNameSql(QStringLiteral("t.id"), QStringLiteral("t.name"), QStringLiteral("t.code"));
    const QString sql = QStringLiteral("SELECT t.id, t.code, %1 FROM nx_room_type t WHERE t.property_id = :property ORDER BY t.code")
                            .arg(name);
    QSqlQuery query(connection.db);
    query.setForwardOnly(true);
    if (!query.prepare(sql))
        return databaseFailure(connection.failure, missingTable(query.lastError()));
    query.bindValue(QStringLiteral(":property"), propertyId);
    query.bindValue(QStringLiteral(":locale"), locale);
    query.bindValue(QStringLiteral(":owner"), QStringLiteral("nx_room_type"));
    query.bindValue(QStringLiteral(":owner_ru"), QStringLiteral("nx_room_type"));
    if (!query.exec())
        return databaseFailure(connection.failure, missingTable(query.lastError()));

    QJsonArray items;
    while (query.next()) {
        QJsonObject row;
        row.insert(QStringLiteral("id"), QJsonValue(query.value(0).toLongLong()));
        row.insert(QStringLiteral("code"), query.value(1).toString());
        row.insert(QStringLiteral("name"), query.value(2).toString());
        items.append(row);
    }
    QJsonObject body;
    body.insert(QStringLiteral("lang"), locale);
    body.insert(QStringLiteral("items"), items);
    ApiResult result;
    result.httpStatus = 200;
    result.body = body;
    return result;
}

ApiResult listBuildings(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, const QString &locale)
{
    if (!target.configured)
        return databaseFailure(QStringLiteral("database_not_configured"), false);
    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened)
        return databaseFailure(connection.failure, false);

    const QString name = localizedNameSql(QStringLiteral("b.id"), QStringLiteral("b.name"), QStringLiteral("b.code"));
    const QString sql = QStringLiteral("SELECT b.id, b.code, %1 FROM nx_building b WHERE b.property_id = :property ORDER BY b.code")
                            .arg(name);
    QSqlQuery query(connection.db);
    query.setForwardOnly(true);
    if (!query.prepare(sql))
        return databaseFailure(connection.failure, missingTable(query.lastError()));
    query.bindValue(QStringLiteral(":property"), propertyId);
    query.bindValue(QStringLiteral(":locale"), locale);
    query.bindValue(QStringLiteral(":owner"), QStringLiteral("nx_building"));
    query.bindValue(QStringLiteral(":owner_ru"), QStringLiteral("nx_building"));
    if (!query.exec())
        return databaseFailure(connection.failure, missingTable(query.lastError()));

    QJsonArray items;
    while (query.next()) {
        QJsonObject row;
        row.insert(QStringLiteral("id"), QJsonValue(query.value(0).toLongLong()));
        row.insert(QStringLiteral("code"), query.value(1).toString());
        row.insert(QStringLiteral("name"), query.value(2).toString());
        items.append(row);
    }
    QJsonObject body;
    body.insert(QStringLiteral("lang"), locale);
    body.insert(QStringLiteral("items"), items);
    ApiResult result;
    result.httpStatus = 200;
    result.body = body;
    return result;
}

ApiResult listRooms(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, const QString &locale)
{
    if (!target.configured)
        return databaseFailure(QStringLiteral("database_not_configured"), false);
    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened)
        return databaseFailure(connection.failure, false);

    const QString typeName = localizedNameSql(QStringLiteral("t.id"), QStringLiteral("t.name"), QStringLiteral("t.code"));
    const QString buildingName = localizedNameSql(QStringLiteral("b.id"), QStringLiteral("b.name"), QStringLiteral("b.code"));
    // Two owner tables in one statement. Bind the type owner as :owner and the building owner as :owner_b.
    // localizedNameSql uses :owner and :owner_ru for both, so build the building expression with its own placeholders.
    const QString buildingExpr = QStringLiteral(
        "COALESCE("
        "(SELECT l.name FROM nx_label l WHERE l.owner_table = :owner_b AND l.owner_id = b.id AND l.locale = :locale_b LIMIT 1),"
        "(SELECT l.name FROM nx_label l WHERE l.owner_table = :owner_b_ru AND l.owner_id = b.id AND l.locale = 'ru' LIMIT 1),"
        "NULLIF(b.name, ''),"
        "b.code)");
    const QString sql = QStringLiteral(
                            "SELECT r.id, r.code, r.floor, r.phone, r.status_code, "
                            "t.id, t.code, %1, b.id, b.code, %2 "
                            "FROM nx_room r "
                            "INNER JOIN nx_room_type t ON t.id = r.room_type_id "
                            "LEFT JOIN nx_building b ON b.id = r.building_id "
                            "WHERE r.property_id = :property "
                            "ORDER BY r.code")
                            .arg(typeName, buildingExpr);

    QSqlQuery query(connection.db);
    query.setForwardOnly(true);
    if (!query.prepare(sql))
        return databaseFailure(connection.failure, missingTable(query.lastError()));
    query.bindValue(QStringLiteral(":property"), propertyId);
    query.bindValue(QStringLiteral(":locale"), locale);
    query.bindValue(QStringLiteral(":owner"), QStringLiteral("nx_room_type"));
    query.bindValue(QStringLiteral(":owner_ru"), QStringLiteral("nx_room_type"));
    query.bindValue(QStringLiteral(":locale_b"), locale);
    query.bindValue(QStringLiteral(":owner_b"), QStringLiteral("nx_building"));
    query.bindValue(QStringLiteral(":owner_b_ru"), QStringLiteral("nx_building"));
    if (!query.exec())
        return databaseFailure(connection.failure, missingTable(query.lastError()));

    QJsonArray items;
    while (query.next()) {
        QJsonObject type;
        type.insert(QStringLiteral("id"), QJsonValue(query.value(5).toLongLong()));
        type.insert(QStringLiteral("code"), query.value(6).toString());
        type.insert(QStringLiteral("name"), query.value(7).toString());

        QJsonObject row;
        row.insert(QStringLiteral("id"), QJsonValue(query.value(0).toLongLong()));
        row.insert(QStringLiteral("code"), query.value(1).toString());
        if (query.value(2).isNull())
            row.insert(QStringLiteral("floor"), QJsonValue::Null);
        else
            row.insert(QStringLiteral("floor"), query.value(2).toInt());
        if (query.value(3).isNull())
            row.insert(QStringLiteral("phone"), QJsonValue::Null);
        else
            row.insert(QStringLiteral("phone"), query.value(3).toString());
        row.insert(QStringLiteral("status_code"), query.value(4).toString());
        row.insert(QStringLiteral("room_type"), type);
        if (query.value(8).isNull()) {
            row.insert(QStringLiteral("building"), QJsonValue::Null);
        } else {
            QJsonObject building;
            building.insert(QStringLiteral("id"), QJsonValue(query.value(8).toLongLong()));
            building.insert(QStringLiteral("code"), query.value(9).toString());
            building.insert(QStringLiteral("name"), query.value(10).toString());
            row.insert(QStringLiteral("building"), building);
        }
        items.append(row);
    }

    QJsonObject body;
    body.insert(QStringLiteral("lang"), locale);
    body.insert(QStringLiteral("items"), items);
    ApiResult result;
    result.httpStatus = 200;
    result.body = body;
    return result;
}
