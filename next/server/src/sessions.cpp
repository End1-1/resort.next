#include "sessions.h"

#include "db.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QSqlQuery>
#include <QVariant>
#include <QtGlobal>

#include <algorithm>

namespace {

constexpr int kSessionTtlSeconds = 12 * 60 * 60;
constexpr int kMaxBodyBytes = 8192;
constexpr int kMaxPasswordChars = 1024;

// Keep in sync with next/dbdump/migrations/0001_hotel_api_session.sql.
const char kCreateSessionTable[] = R"SQL(
CREATE TABLE IF NOT EXISTS `hotel_api_session` (
  `f_id` bigint(20) NOT NULL AUTO_INCREMENT,
  `f_token_hash` char(64) NOT NULL,
  `f_user` int(11) NOT NULL,
  `f_group` int(11) DEFAULT NULL,
  `f_commands_allowed` tinyint(1) NOT NULL DEFAULT 0,
  `f_created_at` datetime NOT NULL,
  `f_expires_at` datetime NOT NULL,
  `f_revoked_at` datetime DEFAULT NULL,
  PRIMARY KEY (`f_id`),
  UNIQUE KEY `uq_hotel_api_session_token` (`f_token_hash`),
  KEY `ix_hotel_api_session_user` (`f_user`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
)SQL";

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

bool ensureSessionTable(QSqlDatabase &database)
{
    QSqlQuery query(database);
    return query.exec(QString::fromLatin1(kCreateSessionTable));
}

struct MatchedUser {
    bool found = false;
    int id = 0;
    bool groupNull = true;
    int group = 0;
    QString firstName;
    QString lastName;
};

MatchedUser findUser(QSqlDatabase &database, const QString &login, const QByteArray &passwordMd5)
{
    MatchedUser matched;
    QSqlQuery query(database);
    query.setForwardOnly(true);
    if (!query.prepare(QStringLiteral(
            "SELECT f_id, f_group, f_firstName, f_lastName, f_password "
            "FROM users "
            "WHERE f_username = :login AND f_state = 1 AND CHAR_LENGTH(f_username) > 0 "
            "ORDER BY f_id"))) {
        matched.id = -1;
        return matched;
    }
    query.bindValue(QStringLiteral(":login"), login);
    if (!query.exec()) {
        matched.id = -1;
        return matched;
    }

    const QByteArray dummy = QByteArrayLiteral("00000000000000000000000000000000");
    while (query.next()) {
        const QByteArray stored = query.value(4).toString().trimmed().toLower().toLatin1();
        const QByteArray expect = isMd5Hex(stored) ? stored : dummy;
        const bool same = constantTimeEqual(passwordMd5, expect);
        if (!matched.found && same && isMd5Hex(stored)) {
            matched.found = true;
            matched.id = query.value(0).toInt();
            matched.groupNull = query.value(1).isNull();
            matched.group = query.value(1).toInt();
            matched.firstName = query.value(2).toString();
            matched.lastName = query.value(3).toString();
        }
    }
    return matched;
}

bool commandsAllowed(QSqlDatabase &database, const MatchedUser &user, bool *ok)
{
    *ok = true;
    if (!user.found || user.groupNull)
        return false;

    QSqlQuery query(database);
    query.setForwardOnly(true);
    if (!query.prepare(QStringLiteral(
            "SELECT COUNT(DISTINCT f_right) FROM users_rights "
            "WHERE f_group = :user_group AND f_flag = 1 AND f_right IS NOT NULL"))) {
        *ok = false;
        return false;
    }
    query.bindValue(QStringLiteral(":user_group"), user.group);
    if (!query.exec() || !query.next()) {
        *ok = false;
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
                    "HOTEL_DSN is empty; login needs MariaDB");
    }
    if (login.isEmpty() || password.isEmpty() || password.size() > kMaxPasswordChars) {
        qInfo("session denied");
        return fail(401, "unauthorized", nullptr);
    }

    const QByteArray passwordMd5 = md5Hex(password);

    MysqlConnection connection(target, connectTimeoutSec);
    if (!connection.opened) {
        const QString code = connection.failure.isEmpty()
                                 ? QStringLiteral("database_unavailable")
                                 : connection.failure;
        qWarning("session login failed: %s", qPrintable(code));
        if (code == QLatin1String("driver_not_loaded")) {
            return fail(503, "driver_not_loaded", "QMYSQL is not loaded");
        }
        return fail(503, "database_unavailable", "MariaDB did not accept the connection");
    }

    if (!ensureSessionTable(connection.db)) {
        qWarning("session login failed: session_store_unavailable");
        return fail(503, "session_store_unavailable", "could not create hotel_api_session");
    }

    const MatchedUser user = findUser(connection.db, login, passwordMd5);
    if (user.id < 0) {
        qWarning("session login failed: database_unavailable");
        return fail(503, "database_unavailable", "MariaDB did not accept the query");
    }
    if (!user.found) {
        qInfo("session denied");
        return fail(401, "unauthorized", nullptr);
    }

    bool rightsOk = false;
    const bool allowed = commandsAllowed(connection.db, user, &rightsOk);
    if (!rightsOk) {
        qWarning("session login failed: database_unavailable");
        return fail(503, "database_unavailable", "MariaDB did not accept the query");
    }

    {
        QSqlQuery cleanup(connection.db);
        cleanup.prepare(QStringLiteral(
            "DELETE FROM hotel_api_session "
            "WHERE f_user = :user AND (f_expires_at <= UTC_TIMESTAMP() OR f_revoked_at IS NOT NULL)"));
        cleanup.bindValue(QStringLiteral(":user"), user.id);
        cleanup.exec();
    }

    const QDateTime created = QDateTime::currentDateTimeUtc();
    const QDateTime expires = created.addSecs(kSessionTtlSeconds);
    const QString createdText = created.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QString expiresText = expires.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    QByteArray token;
    bool inserted = false;
    for (int attempt = 0; attempt < 2 && !inserted; ++attempt) {
        token = randomToken();
        const QByteArray tokenHash =
            QCryptographicHash::hash(token, QCryptographicHash::Sha256).toHex();
        QSqlQuery insert(connection.db);
        if (!insert.prepare(QStringLiteral(
                "INSERT INTO hotel_api_session "
                "(f_token_hash, f_user, f_group, f_commands_allowed, f_created_at, f_expires_at) "
                "VALUES (:hash, :user, :user_group, :allowed, :created, :expires)"))) {
            qWarning("session login failed: session_store_unavailable");
            return fail(503, "session_store_unavailable", "could not store the session");
        }
        insert.bindValue(QStringLiteral(":hash"), QString::fromLatin1(tokenHash));
        insert.bindValue(QStringLiteral(":user"), user.id);
        if (user.groupNull)
            insert.bindValue(QStringLiteral(":user_group"), QVariant());
        else
            insert.bindValue(QStringLiteral(":user_group"), user.group);
        insert.bindValue(QStringLiteral(":allowed"), allowed ? 1 : 0);
        insert.bindValue(QStringLiteral(":created"), createdText);
        insert.bindValue(QStringLiteral(":expires"), expiresText);
        inserted = insert.exec();
    }
    if (!inserted) {
        qWarning("session login failed: session_store_unavailable");
        return fail(503, "session_store_unavailable", "could not store the session");
    }

    QJsonObject userJson;
    userJson.insert(QStringLiteral("id"), user.id);
    userJson.insert(QStringLiteral("login"), login);
    const QString name = (user.firstName + QLatin1Char(' ') + user.lastName).trimmed();
    userJson.insert(QStringLiteral("name"), name);
    if (user.groupNull)
        userJson.insert(QStringLiteral("group"), QJsonValue::Null);
    else
        userJson.insert(QStringLiteral("group"), user.group);

    QJsonObject bodyOut;
    bodyOut.insert(QStringLiteral("token_type"), QStringLiteral("Bearer"));
    bodyOut.insert(QStringLiteral("token"), QString::fromLatin1(token));
    bodyOut.insert(QStringLiteral("expires_at"), expires.toString(Qt::ISODate));
    bodyOut.insert(QStringLiteral("user"), userJson);
    bodyOut.insert(QStringLiteral("commands_allowed"), allowed);

    qInfo("session created user=%d commands_allowed=%d", user.id, allowed ? 1 : 0);

    SessionResult result;
    result.httpStatus = 200;
    result.body = bodyOut;
    return result;
}
