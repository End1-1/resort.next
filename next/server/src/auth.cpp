#include "auth.h"

#include "db.h"

#include <QCryptographicHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QtGlobal>

ApiResult apiError(int status, const char *code, const char *message)
{
    ApiResult result;
    result.httpStatus = status;
    result.body.insert(QStringLiteral("error"), QLatin1String(code));
    if (message && message[0] != '\0')
        result.body.insert(QStringLiteral("message"), QLatin1String(message));
    return result;
}

namespace {

const char kSchemaMessage[] =
    "nx_user and nx_session are required; apply next/dbdump/migrations/0002_nx_core.sql";

bool missingTable(const QSqlError &error)
{
    return error.nativeErrorCode() == QLatin1String("1146");
}

ApiResult databaseFailure(const QString &failure, bool schemaMissing)
{
    if (schemaMissing)
        return apiError(503, "session_store_unavailable", kSchemaMessage);
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

bool hexChar(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

} // namespace

AccessDecision decideAccess(bool found,
                            bool revoked,
                            bool expired,
                            bool disabled,
                            bool commandsAllowed,
                            bool requiresCommand)
{
    AccessDecision decision;
    if (!found || revoked) {
        decision.httpStatus = 401;
        decision.code = "unauthorized";
        return decision;
    }
    if (expired) {
        decision.httpStatus = 401;
        decision.code = "session_expired";
        return decision;
    }
    if (disabled) {
        decision.httpStatus = 401;
        decision.code = "user_disabled";
        return decision;
    }
    if (requiresCommand && !commandsAllowed) {
        decision.httpStatus = 403;
        decision.code = "commands_not_allowed";
        return decision;
    }
    decision.httpStatus = 200;
    decision.code = nullptr;
    return decision;
}

bool isSessionToken(const QByteArray &token)
{
    if (token.size() != 64)
        return false;
    for (char ch : token) {
        if (!hexChar(static_cast<unsigned char>(ch)))
            return false;
    }
    return true;
}

QByteArray bearerTokenFromHeader(const QByteArray &authorization)
{
    const QByteArray trimmed = authorization.trimmed();
    if (trimmed.size() < 8)
        return {};
    if (trimmed.left(6).toLower() != QByteArrayLiteral("bearer"))
        return {};
    if (trimmed.at(6) != ' ')
        return {};
    const QByteArray token = trimmed.mid(7).trimmed();
    if (!isSessionToken(token))
        return {};
    return token;
}

QByteArray sha256Hex(const QByteArray &token)
{
    return QCryptographicHash::hash(token, QCryptographicHash::Sha256).toHex();
}

AuthOutcome authenticate(const DatabaseTarget &target,
                         int connectTimeoutSec,
                         const QByteArray &authorization,
                         RouteAccess access)
{
    AuthOutcome outcome;
    const QByteArray token = bearerTokenFromHeader(authorization);
    if (token.isEmpty()) {
        outcome.result = apiError(401, "unauthorized", nullptr);
        return outcome;
    }
    if (!target.configured) {
        outcome.result = databaseFailure(QStringLiteral("database_not_configured"), false);
        return outcome;
    }

    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened) {
        qWarning("session check failed: %s", qPrintable(connection.failure));
        outcome.result = databaseFailure(connection.failure, false);
        return outcome;
    }

    QSqlQuery query(connection.db);
    query.setForwardOnly(true);
    const bool prepared = query.prepare(QStringLiteral(
        "SELECT s.id, s.user_id, s.role_id, s.commands_allowed, "
        "DATE_FORMAT(s.expires_at, '%Y-%m-%dT%H:%i:%sZ'), "
        "(s.revoked_at IS NOT NULL), (s.expires_at <= UTC_TIMESTAMP()), "
        "u.login, u.first_name, u.last_name, u.state, u.property_id "
        "FROM nx_session s "
        "INNER JOIN nx_user u ON u.id = s.user_id "
        "WHERE s.token_hash = :hash"));
    if (!prepared) {
        const bool schema = missingTable(query.lastError());
        outcome.result = databaseFailure(connection.failure, schema);
        qWarning("session check failed: %s", schema ? "session_store_unavailable" : "database_unavailable");
        return outcome;
    }

    query.bindValue(QStringLiteral(":hash"), QString::fromLatin1(sha256Hex(token)));
    if (!query.exec()) {
        const bool schema = missingTable(query.lastError());
        outcome.result = databaseFailure(connection.failure, schema);
        qWarning("session check failed: %s", schema ? "session_store_unavailable" : "database_unavailable");
        return outcome;
    }
    if (!query.next()) {
        const AccessDecision decision = decideAccess(false, false, false, false, false, false);
        outcome.result = apiError(decision.httpStatus, decision.code, nullptr);
        return outcome;
    }

    SessionPrincipal principal;
    principal.sessionId = query.value(0).toLongLong();
    principal.userId = query.value(1).toLongLong();
    principal.roleNull = query.value(2).isNull();
    principal.roleId = query.value(2).toLongLong();
    principal.commandsAllowed = query.value(3).toInt() != 0;
    principal.expiresAt = query.value(4).toString();
    const bool revoked = query.value(5).toInt() != 0;
    const bool expired = query.value(6).toInt() != 0;
    principal.login = query.value(7).toString();
    principal.name = (query.value(8).toString() + QLatin1Char(' ') + query.value(9).toString()).trimmed();
    const bool disabled = query.value(10).toString() != QLatin1String("active");
    principal.propertyId = query.value(11).toLongLong();

    const AccessDecision decision = decideAccess(true,
                                                 revoked,
                                                 expired,
                                                 disabled,
                                                 principal.commandsAllowed,
                                                 access == RouteAccess::Command);
    if (decision.httpStatus != 200) {
        outcome.result = apiError(decision.httpStatus, decision.code, nullptr);
        return outcome;
    }

    outcome.allowed = true;
    outcome.principal = principal;
    outcome.result.httpStatus = 200;
    return outcome;
}

ApiResult revokeSession(const DatabaseTarget &target, int connectTimeoutSec, qint64 sessionId)
{
    if (!target.configured)
        return databaseFailure(QStringLiteral("database_not_configured"), false);

    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened) {
        qWarning("session revoke failed: %s", qPrintable(connection.failure));
        return databaseFailure(connection.failure, false);
    }

    QSqlQuery query(connection.db);
    if (!query.prepare(QStringLiteral(
            "UPDATE nx_session SET revoked_at = UTC_TIMESTAMP() "
            "WHERE id = :id AND revoked_at IS NULL"))) {
        const bool schema = missingTable(query.lastError());
        qWarning("session revoke failed: prepare");
        return databaseFailure(connection.failure, schema);
    }
    query.bindValue(QStringLiteral(":id"), sessionId);
    if (!query.exec()) {
        const bool schema = missingTable(query.lastError());
        qWarning("session revoke failed: %s", schema ? "session_store_unavailable" : "database_unavailable");
        return databaseFailure(connection.failure, schema);
    }

    qInfo("session revoked id=%lld", static_cast<long long>(sessionId));
    ApiResult result;
    result.httpStatus = 200;
    result.body.insert(QStringLiteral("revoked"), true);
    return result;
}

QJsonObject currentSessionBody(const SessionPrincipal &principal)
{
    QJsonObject user;
    user.insert(QStringLiteral("id"), QJsonValue(principal.userId));
    user.insert(QStringLiteral("login"), principal.login);
    user.insert(QStringLiteral("name"), principal.name);
    if (principal.roleNull) {
        user.insert(QStringLiteral("role_id"), QJsonValue::Null);
        user.insert(QStringLiteral("group"), QJsonValue::Null);
    } else {
        user.insert(QStringLiteral("role_id"), QJsonValue(principal.roleId));
        user.insert(QStringLiteral("group"), QJsonValue(principal.roleId));
    }

    QJsonObject body;
    body.insert(QStringLiteral("token_type"), QStringLiteral("Bearer"));
    body.insert(QStringLiteral("expires_at"), principal.expiresAt);
    body.insert(QStringLiteral("user"), user);
    body.insert(QStringLiteral("commands_allowed"), principal.commandsAllowed);
    return body;
}
