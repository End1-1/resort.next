#include "sessions.h"

#include "db.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QtGlobal>

#include <algorithm>

namespace {

constexpr int kSessionTtlSeconds = 12 * 60 * 60;
constexpr int kMaxBodyBytes = 8192;
constexpr int kMaxPasswordChars = 1024;

const char kSchemaMessage[] =
    "nx_user and nx_session are required; apply next/dbdump/migrations/0002_nx_core.sql";

QJsonObject errorBody(const char *code, const char *message)
{
    QJsonObject body;
    body.insert(QStringLiteral("error"), QLatin1String(code));
    if (message && message[0] != '\0')
        body.insert(QStringLiteral("message"), QLatin1String(message));
    return body;
}

SessionResult fail(int status, const char *code, const char *message)
{
    SessionResult result;
    result.httpStatus = status;
    result.body = errorBody(code, message);
    return result;
}

bool constantTimeEqual(const QByteArray &left, const QByteArray &right)
{
    const qsizetype n = std::max(left.size(), right.size());
    unsigned diff = left.size() == right.size() ? 0u : 1u;
    for (qsizetype i = 0; i < n; ++i) {
        const unsigned char a = i < left.size() ? static_cast<unsigned char>(left.at(i)) : 0;
        const unsigned char b = i < right.size() ? static_cast<unsigned char>(right.at(i)) : 0;
        diff |= static_cast<unsigned>(a ^ b);
    }
    return diff == 0;
}

bool isMd5Hex(const QByteArray &value)
{
    if (value.size() != 32)
        return false;
    for (char ch : value) {
        const unsigned char c = static_cast<unsigned char>(ch);
        const bool digit = c >= '0' && c <= '9';
        const bool hex = c >= 'a' && c <= 'f';
        if (!digit && !hex)
            return false;
    }
    return true;
}

QByteArray md5Hex(const QString &password)
{
    return QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Md5).toHex();
}

QByteArray randomToken()
{
    quint32 words[8];
    QRandomGenerator::system()->fillRange(words);
    const QByteArray raw(reinterpret_cast<const char *>(words), static_cast<int>(sizeof(words)));
    return raw.toHex();
}

bool missingTable(const QSqlError &error)
{
    return error.nativeErrorCode() == QLatin1String("1146");
}

struct MatchedUser {
    bool found = false;
    bool failed = false;
    bool schemaMissing = false;
    qint64 id = 0;
    bool roleNull = true;
    qint64 roleId = 0;
    QString login;
    QString firstName;
    QString lastName;
};

MatchedUser findUser(QSqlDatabase &database, const QString &login, const QByteArray &passwordMd5)
{
    MatchedUser matched;
    QSqlQuery query(database);
    query.setForwardOnly(true);
    if (!query.prepare(QStringLiteral(
            "SELECT id, role_id, login, first_name, last_name, password_hash, password_scheme "
            "FROM nx_user "
            "WHERE login = :login AND state = 'active' AND CHAR_LENGTH(login) > 0"))) {
        matched.failed = true;
        matched.schemaMissing = missingTable(query.lastError());
        return matched;
    }
    query.bindValue(QStringLiteral(":login"), login);
    if (!query.exec()) {
        matched.failed = true;
        matched.schemaMissing = missingTable(query.lastError());
        return matched;
    }

    const QByteArray dummy = QByteArrayLiteral("00000000000000000000000000000000");
    if (!query.next()) {
        constantTimeEqual(passwordMd5, dummy);
        return matched;
    }

    const QByteArray stored = query.value(5).toString().trimmed().toLower().toLatin1();
    const QString scheme = query.value(6).toString().trimmed().toLower();
    const bool md5 = scheme == QLatin1String("md5") && isMd5Hex(stored);
    const bool same = constantTimeEqual(passwordMd5, md5 ? stored : dummy);
    if (same && md5) {
        matched.found = true;
        matched.id = query.value(0).toLongLong();
        matched.roleNull = query.value(1).isNull();
        matched.roleId = query.value(1).toLongLong();
        matched.login = query.value(2).toString();
        matched.firstName = query.value(3).toString();
        matched.lastName = query.value(4).toString();
    }
    return matched;
}

bool commandsAllowed(QSqlDatabase &database, const MatchedUser &user, bool *ok, bool *schemaMissing)
{
    *ok = true;
    *schemaMissing = false;
    if (!user.found || user.roleNull)
        return false;

    QSqlQuery query(database);
    query.setForwardOnly(true);
    if (!query.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM nx_role_permission WHERE role_id = :role_id"))) {
        *ok = false;
        *schemaMissing = missingTable(query.lastError());
        return false;
    }
    query.bindValue(QStringLiteral(":role_id"), user.roleId);
    if (!query.exec() || !query.next()) {
        *ok = false;
        *schemaMissing = missingTable(query.lastError());
        return false;
    }
    return query.value(0).toInt() > 0;
}

} // namespace

SessionResult createSession(const DatabaseTarget &target, int connectTimeoutSec, const QByteArray &body)
{
    if (body.size() > kMaxBodyBytes) {
        return fail(400, "invalid_request", "JSON body is too large");
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(400, "invalid_request", "JSON body must be an object with login and password");
    }
    const QJsonObject object = document.object();
    const QJsonValue loginValue = object.value(QStringLiteral("login"));
    const QJsonValue passwordValue = object.value(QStringLiteral("password"));
    if (!loginValue.isString() || !passwordValue.isString()) {
        return fail(400, "invalid_request", "JSON body must be an object with login and password");
    }
    const QString login = loginValue.toString();
    const QString password = passwordValue.toString();
    if (!target.configured) {
        return fail(503,
                    "database_not_configured",
                    "no database configured; set mysql_host and mysql_schema in hotel-api.ini");
    }
    if (login.isEmpty() || password.isEmpty() || password.size() > kMaxPasswordChars) {
        qInfo("session denied");
        return fail(401, "unauthorized", nullptr);
    }

    const QByteArray passwordMd5 = md5Hex(password);

    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened) {
        const QString failure = connection.failure;
        qWarning("session login failed: %s", qPrintable(failure));
        // The JSON code is a fixed token. Host, user, and driver text stay in
        // the connect log written by MysqlConnection, not in this body.
        if (failure == QLatin1String("driver_not_loaded"))
            return fail(503, "driver_not_loaded", "QMYSQL is not loaded");
        if (failure == QLatin1String("access_denied"))
            return fail(503, "access_denied", "MariaDB refused the configured account");
        if (failure == QLatin1String("unknown_database"))
            return fail(503, "unknown_database", "MariaDB database was not found");
        if (failure == QLatin1String("cannot_connect"))
            return fail(503, "cannot_connect", "MariaDB did not accept the connection");
        return fail(503, "database_unavailable", "MariaDB did not accept the connection");
    }

    const MatchedUser user = findUser(connection.db, login, passwordMd5);
    if (user.failed) {
        if (user.schemaMissing) {
            qWarning("session login failed: session_store_unavailable");
            return fail(503, "session_store_unavailable", kSchemaMessage);
        }
        qWarning("session login failed: database_unavailable");
        return fail(503, "database_unavailable", "MariaDB did not accept the query");
    }
    if (!user.found) {
        qInfo("session denied");
        return fail(401, "unauthorized", nullptr);
    }

    bool rightsOk = false;
    bool rightsSchemaMissing = false;
    const bool allowed = commandsAllowed(connection.db, user, &rightsOk, &rightsSchemaMissing);
    if (!rightsOk) {
        if (rightsSchemaMissing) {
            qWarning("session login failed: session_store_unavailable");
            return fail(503, "session_store_unavailable", kSchemaMessage);
        }
        qWarning("session login failed: database_unavailable");
        return fail(503, "database_unavailable", "MariaDB did not accept the query");
    }

    {
        QSqlQuery cleanup(connection.db);
        cleanup.prepare(QStringLiteral(
            "DELETE FROM nx_session "
            "WHERE user_id = :user AND (expires_at <= UTC_TIMESTAMP() OR revoked_at IS NOT NULL)"));
        cleanup.bindValue(QStringLiteral(":user"), user.id);
        cleanup.exec();
    }

    const QDateTime created = QDateTime::currentDateTimeUtc();
    const QDateTime expires = created.addSecs(kSessionTtlSeconds);
    const QString createdText = created.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QString expiresText = expires.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    QByteArray token;
    bool inserted = false;
    bool insertSchemaMissing = false;
    for (int attempt = 0; attempt < 2 && !inserted; ++attempt) {
        token = randomToken();
        const QByteArray tokenHash =
            QCryptographicHash::hash(token, QCryptographicHash::Sha256).toHex();
        QSqlQuery insert(connection.db);
        if (!insert.prepare(QStringLiteral(
                "INSERT INTO nx_session "
                "(token_hash, user_id, role_id, commands_allowed, created_at, expires_at) "
                "VALUES (:hash, :user, :role_id, :allowed, :created, :expires)"))) {
            insertSchemaMissing = missingTable(insert.lastError());
            break;
        }
        insert.bindValue(QStringLiteral(":hash"), QString::fromLatin1(tokenHash));
        insert.bindValue(QStringLiteral(":user"), user.id);
        if (user.roleNull)
            insert.bindValue(QStringLiteral(":role_id"), QVariant());
        else
            insert.bindValue(QStringLiteral(":role_id"), user.roleId);
        insert.bindValue(QStringLiteral(":allowed"), allowed ? 1 : 0);
        insert.bindValue(QStringLiteral(":created"), createdText);
        insert.bindValue(QStringLiteral(":expires"), expiresText);
        inserted = insert.exec();
        if (!inserted)
            insertSchemaMissing = missingTable(insert.lastError());
    }
    if (!inserted) {
        qWarning("session login failed: session_store_unavailable");
        if (insertSchemaMissing)
            return fail(503, "session_store_unavailable", kSchemaMessage);
        return fail(503, "session_store_unavailable", "could not store the session");
    }

    QJsonObject userJson;
    userJson.insert(QStringLiteral("id"), QJsonValue(user.id));
    userJson.insert(QStringLiteral("login"), user.login);
    const QString name = (user.firstName + QLatin1Char(' ') + user.lastName).trimmed();
    userJson.insert(QStringLiteral("name"), name);
    if (user.roleNull) {
        userJson.insert(QStringLiteral("role_id"), QJsonValue::Null);
        userJson.insert(QStringLiteral("group"), QJsonValue::Null);
    } else {
        userJson.insert(QStringLiteral("role_id"), QJsonValue(user.roleId));
        userJson.insert(QStringLiteral("group"), QJsonValue(user.roleId));
    }

    QJsonObject bodyOut;
    bodyOut.insert(QStringLiteral("token_type"), QStringLiteral("Bearer"));
    bodyOut.insert(QStringLiteral("token"), QString::fromLatin1(token));
    bodyOut.insert(QStringLiteral("expires_at"), expires.toString(Qt::ISODate));
    bodyOut.insert(QStringLiteral("user"), userJson);
    bodyOut.insert(QStringLiteral("commands_allowed"), allowed);

    qInfo("session created user=%lld commands_allowed=%d",
          static_cast<long long>(user.id),
          allowed ? 1 : 0);

    SessionResult result;
    result.httpStatus = 200;
    result.body = bodyOut;
    return result;
}
