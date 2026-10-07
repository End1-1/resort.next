#include "dictionaries.h"

#include "db.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QtGlobal>

namespace {

const char kSchemaMessage[] =
    "nx_room, nx_room_type, nx_building, nx_label, and dictionary version columns are required; apply "
    "next/dbdump/migrations/0002_nx_core.sql, 0003_nx_label.sql, then 0005_nx_dictionary_version.sql";

const char *kRoomStatuses[] = {
    "vacant_ready",
    "occupied",
    "vacant_dirty",
    "out_of_order",
    "house_use",
    "complimentary",
    "out_of_inventory",
};

struct NamedSpec {
    const char *table;
    const char *notFoundCode;
    const char *notFoundMessage;
    const char *usageSql;
};

const NamedSpec kRoomType = {
    "nx_room_type",
    "room_type_not_found",
    "room type was not found",
    "SELECT COUNT(*) FROM nx_room WHERE room_type_id = :id",
};

const NamedSpec kBuilding = {
    "nx_building",
    "building_not_found",
    "building was not found",
    "SELECT COUNT(*) FROM nx_room WHERE building_id = :id",
};

bool missingSchema(const QSqlError &error)
{
    const QString code = error.nativeErrorCode();
    return code == QLatin1String("1146") || code == QLatin1String("1054");
}

bool duplicateKey(const QSqlError &error)
{
    return error.nativeErrorCode() == QLatin1String("1062");
}

bool foreignKeyBlock(const QSqlError &error)
{
    const QString code = error.nativeErrorCode();
    return code == QLatin1String("1451") || code == QLatin1String("1452");
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

ApiResult invalid(const char *message)
{
    return apiError(400, "invalid_request", message);
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

// ownerTable and locale are fixed literals from this file, not request text.
QString labelColumn(const char *ownerTable, const QString &idExpr, const char *locale)
{
    return QStringLiteral(
               "(SELECT l.name FROM nx_label l WHERE l.owner_table = '%1' AND l.owner_id = %2 AND l.locale = '%3' LIMIT 1)")
        .arg(QLatin1String(ownerTable), idExpr, QLatin1String(locale));
}

QJsonValue nameOrNull(const QVariant &value)
{
    if (value.isNull())
        return QJsonValue::Null;
    const QString text = value.toString();
    if (text.isEmpty())
        return QJsonValue::Null;
    return text;
}

QJsonObject namesObject(const QVariant &hy, const QVariant &en, const QVariant &ru)
{
    QJsonObject names;
    names.insert(QStringLiteral("hy"), nameOrNull(hy));
    names.insert(QStringLiteral("en"), nameOrNull(en));
    names.insert(QStringLiteral("ru"), nameOrNull(ru));
    return names;
}

bool wholeNumber(const QJsonValue &value, qint64 *out)
{
    if (!value.isDouble())
        return false;
    const double raw = value.toDouble();
    if (!qIsFinite(raw))
        return false;
    if (raw > 9223372036854775807.0 || raw < -9223372036854775807.0)
        return false;
    const auto number = static_cast<qint64>(raw);
    if (static_cast<double>(number) != raw)
        return false;
    *out = number;
    return true;
}

bool readVersion(const QJsonObject &body, int *version, ApiResult *error)
{
    if (!body.contains(QStringLiteral("version"))) {
        *error = invalid("version is required");
        return false;
    }
    qint64 number = 0;
    if (!wholeNumber(body.value(QStringLiteral("version")), &number) || number < 0 || number > 2147483647) {
        *error = invalid("version must be zero or greater");
        return false;
    }
    *version = static_cast<int>(number);
    return true;
}

bool readCode(const QJsonObject &body, QString *code, ApiResult *error)
{
    const QJsonValue value = body.value(QStringLiteral("code"));
    if (!value.isString()) {
        *error = apiError(400, "code_required", "code is required");
        return false;
    }
    *code = value.toString().trimmed();
    if (code->isEmpty()) {
        *error = apiError(400, "code_required", "code is required");
        return false;
    }
    if (code->size() > 32) {
        *error = apiError(400, "code_too_long", "code is longer than 32 characters");
        return false;
    }
    return true;
}

bool readRequiredName(const QJsonObject &names, const char *key, QString *name, ApiResult *error)
{
    const QJsonValue value = names.value(QLatin1String(key));
    if (!value.isString()) {
        *error = apiError(400, "name_required", "names.hy, names.en, and names.ru are required");
        return false;
    }
    *name = value.toString().trimmed();
    if (name->isEmpty()) {
        *error = apiError(400, "name_required", "names.hy, names.en, and names.ru are required");
        return false;
    }
    if (name->size() > 128) {
        *error = apiError(400, "name_too_long", "a name is longer than 128 characters");
        return false;
    }
    return true;
}

class Transaction {
public:
    explicit Transaction(QSqlDatabase &db)
        : m_db(db)
        , m_open(db.transaction())
    {
    }

    ~Transaction()
    {
        if (m_open && !m_done)
            m_db.rollback();
    }

    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;

    bool beginOk() const { return m_open; }

    bool commit()
    {
        if (!m_open)
            return false;
        m_done = m_db.commit();
        return m_done;
    }

private:
    QSqlDatabase &m_db;
    bool m_open = false;
    bool m_done = false;
};

bool prepareQuery(QSqlQuery &query, const QString &sql, bool *schemaMissing, QSqlError *error)
{
    if (query.prepare(sql))
        return true;
    *schemaMissing = missingSchema(query.lastError());
    *error = query.lastError();
    return false;
}

QJsonObject namedBody(qint64 id, const NamedWrite &row, int version)
{
    QJsonObject names;
    names.insert(QStringLiteral("hy"), row.hy);
    names.insert(QStringLiteral("en"), row.en);
    names.insert(QStringLiteral("ru"), row.ru);
    QJsonObject body;
    body.insert(QStringLiteral("id"), QJsonValue(id));
    body.insert(QStringLiteral("code"), row.code);
    body.insert(QStringLiteral("name"), row.ru);
    body.insert(QStringLiteral("names"), names);
    body.insert(QStringLiteral("version"), version);
    return body;
}

bool writeLabels(QSqlDatabase &db, const QString &owner, qint64 id, const NamedWrite &row, bool *schemaMissing, QSqlError *error)
{
    QSqlQuery query(db);
    const QString sql = QStringLiteral(
        "INSERT INTO nx_label (owner_table, owner_id, locale, name) VALUES "
        "(:owner_hy, :id_hy, 'hy', :hy), "
        "(:owner_en, :id_en, 'en', :en), "
        "(:owner_ru, :id_ru, 'ru', :ru) "
        "ON DUPLICATE KEY UPDATE name = VALUES(name)");
    if (!prepareQuery(query, sql, schemaMissing, error))
        return false;
    query.bindValue(QStringLiteral(":owner_hy"), owner);
    query.bindValue(QStringLiteral(":id_hy"), id);
    query.bindValue(QStringLiteral(":hy"), row.hy);
    query.bindValue(QStringLiteral(":owner_en"), owner);
    query.bindValue(QStringLiteral(":id_en"), id);
    query.bindValue(QStringLiteral(":en"), row.en);
    query.bindValue(QStringLiteral(":owner_ru"), owner);
    query.bindValue(QStringLiteral(":id_ru"), id);
    query.bindValue(QStringLiteral(":ru"), row.ru);
    if (query.exec())
        return true;
    *schemaMissing = missingSchema(query.lastError());
    *error = query.lastError();
    return false;
}

ApiResult finishWriteError(const QString &failure, bool schemaMissing, const QSqlError &error)
{
    if (duplicateKey(error))
        return apiError(409, "duplicate_code", "that code is already used for this property");
    if (foreignKeyBlock(error))
        return apiError(409, "in_use", "the row is still referenced");
    return databaseFailure(failure, schemaMissing);
}

ApiResult createNamed(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, const QByteArray &raw, const NamedSpec &spec)
{
    const QJsonDocument document = QJsonDocument::fromJson(raw);
    if (!document.isObject())
        return invalid("body must be a JSON object");
    NamedWrite row;
    ApiResult error;
    if (!parseNamedWrite(document.object(), false, &row, &error))
        return error;
    if (!target.configured)
        return databaseFailure(QStringLiteral("database_not_configured"), false);

    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened)
        return databaseFailure(connection.failure, false);

    Transaction transaction(connection.db);
    if (!transaction.beginOk())
        return databaseFailure(connection.failure, false);

    bool schemaMissing = false;
    QSqlError sqlError;
    QSqlQuery insert(connection.db);
    const QString sql = QStringLiteral("INSERT INTO %1 (property_id, code, name, version) VALUES (:property, :code, :name, 0)")
                            .arg(QLatin1String(spec.table));
    if (!prepareQuery(insert, sql, &schemaMissing, &sqlError))
        return databaseFailure(connection.failure, schemaMissing);
    insert.bindValue(QStringLiteral(":property"), propertyId);
    insert.bindValue(QStringLiteral(":code"), row.code);
    insert.bindValue(QStringLiteral(":name"), row.ru);
    if (!insert.exec())
        return finishWriteError(connection.failure, missingSchema(insert.lastError()), insert.lastError());

    const qint64 id = insert.lastInsertId().toLongLong();
    if (id <= 0)
        return databaseFailure(connection.failure, false);
    if (!writeLabels(connection.db, QLatin1String(spec.table), id, row, &schemaMissing, &sqlError))
        return finishWriteError(connection.failure, schemaMissing, sqlError);
    if (!transaction.commit())
        return databaseFailure(connection.failure, false);

    ApiResult result;
    result.httpStatus = 201;
    result.body = namedBody(id, row, 0);
    return result;
}

ApiResult updateNamed(const DatabaseTarget &target,
                      int connectTimeoutSec,
                      qint64 propertyId,
                      qint64 id,
                      const QByteArray &raw,
                      const NamedSpec &spec)
{
    const QJsonDocument document = QJsonDocument::fromJson(raw);
    if (!document.isObject())
        return invalid("body must be a JSON object");
    NamedWrite row;
    ApiResult error;
    if (!parseNamedWrite(document.object(), true, &row, &error))
        return error;
    if (id <= 0)
        return apiError(404, spec.notFoundCode, spec.notFoundMessage);
    if (!target.configured)
        return databaseFailure(QStringLiteral("database_not_configured"), false);

    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened)
        return databaseFailure(connection.failure, false);

    Transaction transaction(connection.db);
    if (!transaction.beginOk())
        return databaseFailure(connection.failure, false);

    bool schemaMissing = false;
    QSqlError sqlError;
    QSqlQuery update(connection.db);
    const QString sql = QStringLiteral(
                            "UPDATE %1 SET code = :code, name = :name, version = version + 1 "
                            "WHERE id = :id AND property_id = :property AND version = :version")
                            .arg(QLatin1String(spec.table));
    if (!prepareQuery(update, sql, &schemaMissing, &sqlError))
        return databaseFailure(connection.failure, schemaMissing);
    update.bindValue(QStringLiteral(":code"), row.code);
    update.bindValue(QStringLiteral(":name"), row.ru);
    update.bindValue(QStringLiteral(":id"), id);
    update.bindValue(QStringLiteral(":property"), propertyId);
    update.bindValue(QStringLiteral(":version"), row.version);
    if (!update.exec())
        return finishWriteError(connection.failure, missingSchema(update.lastError()), update.lastError());

    if (update.numRowsAffected() < 1) {
        QSqlQuery probe(connection.db);
        const QString probeSql = QStringLiteral("SELECT version FROM %1 WHERE id = :id AND property_id = :property")
                                     .arg(QLatin1String(spec.table));
        if (!prepareQuery(probe, probeSql, &schemaMissing, &sqlError))
            return databaseFailure(connection.failure, schemaMissing);
        probe.bindValue(QStringLiteral(":id"), id);
        probe.bindValue(QStringLiteral(":property"), propertyId);
        if (!probe.exec())
            return databaseFailure(connection.failure, missingSchema(probe.lastError()));
        if (!probe.next())
            return apiError(404, spec.notFoundCode, spec.notFoundMessage);
        // numRowsAffected 0 means the WHERE missed. A negative count means the
        // driver did not say; only then is version+1 evidence that our UPDATE landed.
        const bool unknownCount = update.numRowsAffected() < 0;
        if (!unknownCount || probe.value(0).toInt() != row.version + 1)
            return apiError(409, "version_conflict", "the row was changed; reload and try again");
    }

    if (!writeLabels(connection.db, QLatin1String(spec.table), id, row, &schemaMissing, &sqlError))
        return finishWriteError(connection.failure, schemaMissing, sqlError);
    if (!transaction.commit())
        return databaseFailure(connection.failure, false);

    ApiResult result;
    result.httpStatus = 200;
    result.body = namedBody(id, row, row.version + 1);
    return result;
}

ApiResult deleteNamed(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, qint64 id, const NamedSpec &spec)
{
    if (id <= 0)
        return apiError(404, spec.notFoundCode, spec.notFoundMessage);
    if (!target.configured)
        return databaseFailure(QStringLiteral("database_not_configured"), false);

    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened)
        return databaseFailure(connection.failure, false);

    Transaction transaction(connection.db);
    if (!transaction.beginOk())
        return databaseFailure(connection.failure, false);

    bool schemaMissing = false;
    QSqlError sqlError;
    QSqlQuery probe(connection.db);
    const QString probeSql = QStringLiteral("SELECT id FROM %1 WHERE id = :id AND property_id = :property FOR UPDATE")
                                 .arg(QLatin1String(spec.table));
    if (!prepareQuery(probe, probeSql, &schemaMissing, &sqlError))
        return databaseFailure(connection.failure, schemaMissing);
    probe.bindValue(QStringLiteral(":id"), id);
    probe.bindValue(QStringLiteral(":property"), propertyId);
    if (!probe.exec())
        return databaseFailure(connection.failure, missingSchema(probe.lastError()));
    if (!probe.next())
        return apiError(404, spec.notFoundCode, spec.notFoundMessage);

    QSqlQuery used(connection.db);
    if (!prepareQuery(used, QLatin1String(spec.usageSql), &schemaMissing, &sqlError))
        return databaseFailure(connection.failure, schemaMissing);
    used.bindValue(QStringLiteral(":id"), id);
    if (!used.exec())
        return databaseFailure(connection.failure, missingSchema(used.lastError()));
    if (!used.next())
        return databaseFailure(connection.failure, false);
    if (used.value(0).toLongLong() > 0)
        return apiError(409, "in_use", "the row is still referenced");

    QSqlQuery labels(connection.db);
    if (!prepareQuery(labels,
                      QStringLiteral("DELETE FROM nx_label WHERE owner_table = :owner AND owner_id = :id"),
                      &schemaMissing,
                      &sqlError)) {
        return databaseFailure(connection.failure, schemaMissing);
    }
    labels.bindValue(QStringLiteral(":owner"), QLatin1String(spec.table));
    labels.bindValue(QStringLiteral(":id"), id);
    if (!labels.exec())
        return databaseFailure(connection.failure, missingSchema(labels.lastError()));

    QSqlQuery remove(connection.db);
    const QString removeSql = QStringLiteral("DELETE FROM %1 WHERE id = :id AND property_id = :property").arg(QLatin1String(spec.table));
    if (!prepareQuery(remove, removeSql, &schemaMissing, &sqlError))
        return databaseFailure(connection.failure, schemaMissing);
    remove.bindValue(QStringLiteral(":id"), id);
    remove.bindValue(QStringLiteral(":property"), propertyId);
    if (!remove.exec())
        return finishWriteError(connection.failure, missingSchema(remove.lastError()), remove.lastError());
    if (remove.numRowsAffected() == 0)
        return apiError(404, spec.notFoundCode, spec.notFoundMessage);
    if (!transaction.commit())
        return databaseFailure(connection.failure, false);

    QJsonObject body;
    body.insert(QStringLiteral("id"), QJsonValue(id));
    body.insert(QStringLiteral("deleted"), true);
    ApiResult result;
    result.httpStatus = 200;
    result.body = body;
    return result;
}

QJsonObject roomFromQuery(const QSqlQuery &query)
{
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
    row.insert(QStringLiteral("version"), query.value(11).toInt());
    row.insert(QStringLiteral("do_not_disturb"), query.value(12).toInt() != 0);
    return row;
}

QString roomSelectSql()
{
    const QString typeName = localizedNameSql(QStringLiteral("t.id"), QStringLiteral("t.name"), QStringLiteral("t.code"));
    const QString buildingExpr = QStringLiteral(
        "COALESCE("
        "(SELECT l.name FROM nx_label l WHERE l.owner_table = :owner_b AND l.owner_id = b.id AND l.locale = :locale_b LIMIT 1),"
        "(SELECT l.name FROM nx_label l WHERE l.owner_table = :owner_b_ru AND l.owner_id = b.id AND l.locale = 'ru' LIMIT 1),"
        "NULLIF(b.name, ''),"
        "b.code)");
    return QStringLiteral(
               "SELECT r.id, r.code, r.floor, r.phone, r.status_code, "
               "t.id, t.code, %1, b.id, b.code, %2, r.version, r.do_not_disturb "
               "FROM nx_room r "
               "INNER JOIN nx_room_type t ON t.id = r.room_type_id "
               "LEFT JOIN nx_building b ON b.id = r.building_id ")
        .arg(typeName, buildingExpr);
}

void bindRoomNames(QSqlQuery &query, const QString &locale)
{
    query.bindValue(QStringLiteral(":locale"), locale);
    query.bindValue(QStringLiteral(":owner"), QStringLiteral("nx_room_type"));
    query.bindValue(QStringLiteral(":owner_ru"), QStringLiteral("nx_room_type"));
    query.bindValue(QStringLiteral(":locale_b"), locale);
    query.bindValue(QStringLiteral(":owner_b"), QStringLiteral("nx_building"));
    query.bindValue(QStringLiteral(":owner_b_ru"), QStringLiteral("nx_building"));
}

bool propertyRowExists(QSqlDatabase &db, const char *table, qint64 id, qint64 propertyId, bool *schemaMissing, bool *found)
{
    *found = false;
    QSqlQuery query(db);
    const QString sql = QStringLiteral("SELECT id FROM %1 WHERE id = :id AND property_id = :property").arg(QLatin1String(table));
    QSqlError error;
    if (!prepareQuery(query, sql, schemaMissing, &error))
        return false;
    query.bindValue(QStringLiteral(":id"), id);
    query.bindValue(QStringLiteral(":property"), propertyId);
    if (!query.exec()) {
        *schemaMissing = missingSchema(query.lastError());
        return false;
    }
    *found = query.next();
    return true;
}

QJsonObject roomWrittenBody(qint64 id, const RoomWrite &row, int version)
{
    QJsonObject type;
    type.insert(QStringLiteral("id"), QJsonValue(row.roomTypeId));
    QJsonObject body;
    body.insert(QStringLiteral("id"), QJsonValue(id));
    body.insert(QStringLiteral("code"), row.code);
    body.insert(QStringLiteral("floor"), row.hasFloor ? QJsonValue(row.floor) : QJsonValue::Null);
    body.insert(QStringLiteral("phone"), row.hasPhone ? QJsonValue(row.phone) : QJsonValue::Null);
    body.insert(QStringLiteral("status_code"), row.statusCode);
    body.insert(QStringLiteral("room_type"), type);
    if (!row.hasBuilding)
        body.insert(QStringLiteral("building"), QJsonValue::Null);
    else {
        QJsonObject building;
        building.insert(QStringLiteral("id"), QJsonValue(row.buildingId));
        body.insert(QStringLiteral("building"), building);
    }
    body.insert(QStringLiteral("version"), version);
    body.insert(QStringLiteral("do_not_disturb"), row.doNotDisturb);
    return body;
}

bool bindRoomInsert(QSqlQuery &query, qint64 propertyId, const RoomWrite &row)
{
    query.bindValue(QStringLiteral(":property"), propertyId);
    query.bindValue(QStringLiteral(":type"), row.roomTypeId);
    if (row.hasBuilding)
        query.bindValue(QStringLiteral(":building"), row.buildingId);
    else
        query.bindValue(QStringLiteral(":building"), QVariant());
    query.bindValue(QStringLiteral(":code"), row.code);
    if (row.hasFloor)
        query.bindValue(QStringLiteral(":floor"), row.floor);
    else
        query.bindValue(QStringLiteral(":floor"), QVariant());
    if (row.hasPhone)
        query.bindValue(QStringLiteral(":phone"), row.phone);
    else
        query.bindValue(QStringLiteral(":phone"), QVariant());
    query.bindValue(QStringLiteral(":status"), row.statusCode);
    query.bindValue(QStringLiteral(":dnd"), row.doNotDisturb ? 1 : 0);
    return true;
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

bool knownRoomStatus(const QString &code)
{
    for (const char *status : kRoomStatuses) {
        if (code == QLatin1String(status))
            return true;
    }
    return false;
}

bool parseNamedWrite(const QJsonObject &body, bool requireVersion, NamedWrite *out, ApiResult *error)
{
    *out = NamedWrite();
    *error = ApiResult();
    if (requireVersion && !readVersion(body, &out->version, error))
        return false;
    if (!readCode(body, &out->code, error))
        return false;
    const QJsonValue namesValue = body.value(QStringLiteral("names"));
    if (!namesValue.isObject()) {
        *error = apiError(400, "name_required", "names.hy, names.en, and names.ru are required");
        return false;
    }
    const QJsonObject names = namesValue.toObject();
    return readRequiredName(names, "hy", &out->hy, error) && readRequiredName(names, "en", &out->en, error)
        && readRequiredName(names, "ru", &out->ru, error);
}

bool parseRoomWrite(const QJsonObject &body, bool requireVersion, RoomWrite *out, ApiResult *error)
{
    *out = RoomWrite();
    *error = ApiResult();
    if (requireVersion && !readVersion(body, &out->version, error))
        return false;
    if (!readCode(body, &out->code, error))
        return false;

    qint64 roomTypeId = 0;
    if (!wholeNumber(body.value(QStringLiteral("room_type_id")), &roomTypeId) || roomTypeId <= 0) {
        *error = invalid("room_type_id must be a positive integer");
        return false;
    }
    out->roomTypeId = roomTypeId;

    if (body.contains(QStringLiteral("building_id")) && !body.value(QStringLiteral("building_id")).isNull()) {
        qint64 buildingId = 0;
        if (!wholeNumber(body.value(QStringLiteral("building_id")), &buildingId) || buildingId <= 0) {
            *error = invalid("building_id must be a positive integer or null");
            return false;
        }
        out->hasBuilding = true;
        out->buildingId = buildingId;
    }

    if (body.contains(QStringLiteral("floor")) && !body.value(QStringLiteral("floor")).isNull()) {
        qint64 floor = 0;
        if (!wholeNumber(body.value(QStringLiteral("floor")), &floor) || floor < -32768 || floor > 32767) {
            *error = invalid("floor must be a whole number from -32768 to 32767, or null");
            return false;
        }
        out->hasFloor = true;
        out->floor = static_cast<int>(floor);
    }

    if (body.contains(QStringLiteral("phone")) && !body.value(QStringLiteral("phone")).isNull()) {
        if (!body.value(QStringLiteral("phone")).isString()) {
            *error = invalid("phone must be a string or null");
            return false;
        }
        const QString phone = body.value(QStringLiteral("phone")).toString().trimmed();
        if (!phone.isEmpty()) {
            if (phone.size() > 32) {
                *error = invalid("phone is longer than 32 characters");
                return false;
            }
            out->hasPhone = true;
            out->phone = phone;
        }
    }

    const QJsonValue status = body.value(QStringLiteral("status_code"));
    if (!status.isString() || !knownRoomStatus(status.toString())) {
        *error = apiError(400, "invalid_status", "status_code is not a room status");
        return false;
    }
    out->statusCode = status.toString();

    if (body.contains(QStringLiteral("do_not_disturb"))) {
        if (!body.value(QStringLiteral("do_not_disturb")).isBool()) {
            *error = invalid("do_not_disturb must be true or false");
            return false;
        }
        out->doNotDisturb = body.value(QStringLiteral("do_not_disturb")).toBool();
    }
    return true;
}

ApiResult listRoomStatuses(const QString &locale)
{
    QJsonArray items;
    for (const char *code : kRoomStatuses) {
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
    const QString sql = QStringLiteral(
                            "SELECT t.id, t.code, %1, t.version, %2, %3, %4 "
                            "FROM nx_room_type t WHERE t.property_id = :property ORDER BY t.code")
                            .arg(name,
                                 labelColumn("nx_room_type", QStringLiteral("t.id"), "hy"),
                                 labelColumn("nx_room_type", QStringLiteral("t.id"), "en"),
                                 labelColumn("nx_room_type", QStringLiteral("t.id"), "ru"));
    QSqlQuery query(connection.db);
    query.setForwardOnly(true);
    if (!query.prepare(sql))
        return databaseFailure(connection.failure, missingSchema(query.lastError()));
    query.bindValue(QStringLiteral(":property"), propertyId);
    query.bindValue(QStringLiteral(":locale"), locale);
    query.bindValue(QStringLiteral(":owner"), QStringLiteral("nx_room_type"));
    query.bindValue(QStringLiteral(":owner_ru"), QStringLiteral("nx_room_type"));
    if (!query.exec())
        return databaseFailure(connection.failure, missingSchema(query.lastError()));

    QJsonArray items;
    while (query.next()) {
        QJsonObject row;
        row.insert(QStringLiteral("id"), QJsonValue(query.value(0).toLongLong()));
        row.insert(QStringLiteral("code"), query.value(1).toString());
        row.insert(QStringLiteral("name"), query.value(2).toString());
        row.insert(QStringLiteral("version"), query.value(3).toInt());
        row.insert(QStringLiteral("names"), namesObject(query.value(4), query.value(5), query.value(6)));
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
    const QString sql = QStringLiteral(
                            "SELECT b.id, b.code, %1, b.version, %2, %3, %4 "
                            "FROM nx_building b WHERE b.property_id = :property ORDER BY b.code")
                            .arg(name,
                                 labelColumn("nx_building", QStringLiteral("b.id"), "hy"),
                                 labelColumn("nx_building", QStringLiteral("b.id"), "en"),
                                 labelColumn("nx_building", QStringLiteral("b.id"), "ru"));
    QSqlQuery query(connection.db);
    query.setForwardOnly(true);
    if (!query.prepare(sql))
        return databaseFailure(connection.failure, missingSchema(query.lastError()));
    query.bindValue(QStringLiteral(":property"), propertyId);
    query.bindValue(QStringLiteral(":locale"), locale);
    query.bindValue(QStringLiteral(":owner"), QStringLiteral("nx_building"));
    query.bindValue(QStringLiteral(":owner_ru"), QStringLiteral("nx_building"));
    if (!query.exec())
        return databaseFailure(connection.failure, missingSchema(query.lastError()));

    QJsonArray items;
    while (query.next()) {
        QJsonObject row;
        row.insert(QStringLiteral("id"), QJsonValue(query.value(0).toLongLong()));
        row.insert(QStringLiteral("code"), query.value(1).toString());
        row.insert(QStringLiteral("name"), query.value(2).toString());
        row.insert(QStringLiteral("version"), query.value(3).toInt());
        row.insert(QStringLiteral("names"), namesObject(query.value(4), query.value(5), query.value(6)));
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

    const QString sql = roomSelectSql() + QStringLiteral(" WHERE r.property_id = :property ORDER BY r.code");
    QSqlQuery query(connection.db);
    query.setForwardOnly(true);
    if (!query.prepare(sql))
        return databaseFailure(connection.failure, missingSchema(query.lastError()));
    query.bindValue(QStringLiteral(":property"), propertyId);
    bindRoomNames(query, locale);
    if (!query.exec())
        return databaseFailure(connection.failure, missingSchema(query.lastError()));

    QJsonArray items;
    while (query.next())
        items.append(roomFromQuery(query));

    QJsonObject body;
    body.insert(QStringLiteral("lang"), locale);
    body.insert(QStringLiteral("items"), items);
    ApiResult result;
    result.httpStatus = 200;
    result.body = body;
    return result;
}

ApiResult createRoomType(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, const QByteArray &body)
{
    return createNamed(target, connectTimeoutSec, propertyId, body, kRoomType);
}

ApiResult updateRoomType(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, qint64 id, const QByteArray &body)
{
    return updateNamed(target, connectTimeoutSec, propertyId, id, body, kRoomType);
}

ApiResult deleteRoomType(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, qint64 id)
{
    return deleteNamed(target, connectTimeoutSec, propertyId, id, kRoomType);
}

ApiResult createBuilding(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, const QByteArray &body)
{
    return createNamed(target, connectTimeoutSec, propertyId, body, kBuilding);
}

ApiResult updateBuilding(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, qint64 id, const QByteArray &body)
{
    return updateNamed(target, connectTimeoutSec, propertyId, id, body, kBuilding);
}

ApiResult deleteBuilding(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, qint64 id)
{
    return deleteNamed(target, connectTimeoutSec, propertyId, id, kBuilding);
}

namespace {

bool checkRoomForeignKeys(QSqlDatabase &db, qint64 propertyId, const RoomWrite &row, bool *schemaMissing, ApiResult *error)
{
    bool found = false;
    if (!propertyRowExists(db, "nx_room_type", row.roomTypeId, propertyId, schemaMissing, &found))
        return false;
    if (!found) {
        *error = apiError(404, "room_type_not_found", "room type was not found");
        return true;
    }
    if (row.hasBuilding) {
        if (!propertyRowExists(db, "nx_building", row.buildingId, propertyId, schemaMissing, &found))
            return false;
        if (!found) {
            *error = apiError(404, "building_not_found", "building was not found");
            return true;
        }
    }
    *error = ApiResult();
    error->httpStatus = 0;
    return true;
}

ApiResult preferLoadedRoom(QSqlDatabase &db, qint64 propertyId, qint64 id, const QJsonObject &fallback, int status)
{
    QSqlQuery query(db);
    const QString sql = roomSelectSql() + QStringLiteral(" WHERE r.property_id = :property AND r.id = :id");
    bool schemaMissing = false;
    QSqlError error;
    if (!prepareQuery(query, sql, &schemaMissing, &error)) {
        ApiResult result;
        result.httpStatus = status;
        result.body = fallback;
        return result;
    }
    query.bindValue(QStringLiteral(":property"), propertyId);
    query.bindValue(QStringLiteral(":id"), id);
    bindRoomNames(query, QStringLiteral("ru"));
    ApiResult result;
    result.httpStatus = status;
    if (!query.exec() || !query.next())
        result.body = fallback;
    else
        result.body = roomFromQuery(query);
    return result;
}

} // namespace

ApiResult createRoom(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, const QByteArray &raw)
{
    const QJsonDocument document = QJsonDocument::fromJson(raw);
    if (!document.isObject())
        return invalid("body must be a JSON object");
    RoomWrite row;
    ApiResult error;
    if (!parseRoomWrite(document.object(), false, &row, &error))
        return error;
    if (!target.configured)
        return databaseFailure(QStringLiteral("database_not_configured"), false);

    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened)
        return databaseFailure(connection.failure, false);

    Transaction transaction(connection.db);
    if (!transaction.beginOk())
        return databaseFailure(connection.failure, false);

    bool schemaMissing = false;
    ApiResult foreign;
    if (!checkRoomForeignKeys(connection.db, propertyId, row, &schemaMissing, &foreign))
        return databaseFailure(connection.failure, schemaMissing);
    if (foreign.httpStatus != 0)
        return foreign;

    QSqlQuery insert(connection.db);
    QSqlError sqlError;
    const QString sql = QStringLiteral(
        "INSERT INTO nx_room (property_id, room_type_id, building_id, code, floor, phone, status_code, do_not_disturb, version) "
        "VALUES (:property, :type, :building, :code, :floor, :phone, :status, :dnd, 0)");
    if (!prepareQuery(insert, sql, &schemaMissing, &sqlError))
        return databaseFailure(connection.failure, schemaMissing);
    bindRoomInsert(insert, propertyId, row);
    if (!insert.exec())
        return finishWriteError(connection.failure, missingSchema(insert.lastError()), insert.lastError());
    const qint64 id = insert.lastInsertId().toLongLong();
    if (id <= 0)
        return databaseFailure(connection.failure, false);
    if (!transaction.commit())
        return databaseFailure(connection.failure, false);
    return preferLoadedRoom(connection.db, propertyId, id, roomWrittenBody(id, row, 0), 201);
}

ApiResult updateRoom(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, qint64 id, const QByteArray &raw)
{
    const QJsonDocument document = QJsonDocument::fromJson(raw);
    if (!document.isObject())
        return invalid("body must be a JSON object");
    RoomWrite row;
    ApiResult error;
    if (!parseRoomWrite(document.object(), true, &row, &error))
        return error;
    if (id <= 0)
        return apiError(404, "room_not_found", "room was not found");
    if (!target.configured)
        return databaseFailure(QStringLiteral("database_not_configured"), false);

    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened)
        return databaseFailure(connection.failure, false);

    Transaction transaction(connection.db);
    if (!transaction.beginOk())
        return databaseFailure(connection.failure, false);

    bool schemaMissing = false;
    ApiResult foreign;
    if (!checkRoomForeignKeys(connection.db, propertyId, row, &schemaMissing, &foreign))
        return databaseFailure(connection.failure, schemaMissing);
    if (foreign.httpStatus != 0)
        return foreign;

    QSqlQuery update(connection.db);
    QSqlError sqlError;
    const QString sql = QStringLiteral(
        "UPDATE nx_room SET room_type_id = :type, building_id = :building, code = :code, floor = :floor, "
        "phone = :phone, status_code = :status, do_not_disturb = :dnd, version = version + 1 "
        "WHERE id = :id AND property_id = :property AND version = :version");
    if (!prepareQuery(update, sql, &schemaMissing, &sqlError))
        return databaseFailure(connection.failure, schemaMissing);
    bindRoomInsert(update, propertyId, row);
    update.bindValue(QStringLiteral(":id"), id);
    update.bindValue(QStringLiteral(":version"), row.version);
    if (!update.exec())
        return finishWriteError(connection.failure, missingSchema(update.lastError()), update.lastError());

    if (update.numRowsAffected() < 1) {
        QSqlQuery probe(connection.db);
        if (!prepareQuery(probe,
                          QStringLiteral("SELECT version FROM nx_room WHERE id = :id AND property_id = :property"),
                          &schemaMissing,
                          &sqlError)) {
            return databaseFailure(connection.failure, schemaMissing);
        }
        probe.bindValue(QStringLiteral(":id"), id);
        probe.bindValue(QStringLiteral(":property"), propertyId);
        if (!probe.exec())
            return databaseFailure(connection.failure, missingSchema(probe.lastError()));
        if (!probe.next())
            return apiError(404, "room_not_found", "room was not found");
        const bool unknownCount = update.numRowsAffected() < 0;
        if (!unknownCount || probe.value(0).toInt() != row.version + 1)
            return apiError(409, "version_conflict", "the row was changed; reload and try again");
    }

    if (!transaction.commit())
        return databaseFailure(connection.failure, false);
    return preferLoadedRoom(connection.db, propertyId, id, roomWrittenBody(id, row, row.version + 1), 200);
}

ApiResult deleteRoom(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, qint64 id)
{
    if (id <= 0)
        return apiError(404, "room_not_found", "room was not found");
    if (!target.configured)
        return databaseFailure(QStringLiteral("database_not_configured"), false);

    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened)
        return databaseFailure(connection.failure, false);

    Transaction transaction(connection.db);
    if (!transaction.beginOk())
        return databaseFailure(connection.failure, false);

    bool schemaMissing = false;
    QSqlError sqlError;
    QSqlQuery probe(connection.db);
    if (!prepareQuery(probe,
                      QStringLiteral("SELECT id FROM nx_room WHERE id = :id AND property_id = :property FOR UPDATE"),
                      &schemaMissing,
                      &sqlError)) {
        return databaseFailure(connection.failure, schemaMissing);
    }
    probe.bindValue(QStringLiteral(":id"), id);
    probe.bindValue(QStringLiteral(":property"), propertyId);
    if (!probe.exec())
        return databaseFailure(connection.failure, missingSchema(probe.lastError()));
    if (!probe.next())
        return apiError(404, "room_not_found", "room was not found");

    QSqlQuery used(connection.db);
    if (!prepareQuery(used, QStringLiteral("SELECT COUNT(*) FROM nx_stay WHERE room_id = :id"), &schemaMissing, &sqlError))
        return databaseFailure(connection.failure, schemaMissing);
    used.bindValue(QStringLiteral(":id"), id);
    if (!used.exec())
        return databaseFailure(connection.failure, missingSchema(used.lastError()));
    if (!used.next())
        return databaseFailure(connection.failure, false);
    if (used.value(0).toLongLong() > 0)
        return apiError(409, "in_use", "the row is still referenced");

    QSqlQuery remove(connection.db);
    if (!prepareQuery(remove,
                      QStringLiteral("DELETE FROM nx_room WHERE id = :id AND property_id = :property"),
                      &schemaMissing,
                      &sqlError)) {
        return databaseFailure(connection.failure, schemaMissing);
    }
    remove.bindValue(QStringLiteral(":id"), id);
    remove.bindValue(QStringLiteral(":property"), propertyId);
    if (!remove.exec())
        return finishWriteError(connection.failure, missingSchema(remove.lastError()), remove.lastError());
    if (remove.numRowsAffected() == 0)
        return apiError(404, "room_not_found", "room was not found");
    if (!transaction.commit())
        return databaseFailure(connection.failure, false);

    QJsonObject body;
    body.insert(QStringLiteral("id"), QJsonValue(id));
    body.insert(QStringLiteral("deleted"), true);
    ApiResult result;
    result.httpStatus = 200;
    result.body = body;
    return result;
}
