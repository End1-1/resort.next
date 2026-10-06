#include "auth.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

namespace {

struct HttpReply {
    int status = 0;
    QByteArray body;
    QString error;
};

HttpReply httpCall(quint16 port,
                   const QByteArray &method,
                   const QByteArray &path,
                   const QByteArray &body,
                   const QByteArray &authorization)
{
    HttpReply reply;
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!socket.waitForConnected(3000)) {
        reply.error = socket.errorString();
        return reply;
    }

    QByteArray request;
    request += method + ' ' + path + " HTTP/1.1\r\n";
    request += "Host: 127.0.0.1\r\n";
    request += "Connection: close\r\n";
    request += "Accept: application/json\r\n";
    if (!authorization.isEmpty())
        request += "Authorization: " + authorization + "\r\n";
    if (!body.isEmpty()) {
        request += "Content-Type: application/json\r\n";
        request += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    }
    request += "\r\n";
    request += body;
    socket.write(request);
    socket.flush();

    QByteArray raw;
    for (;;) {
        const int split = raw.indexOf("\r\n\r\n");
        if (split >= 0) {
            const QByteArray headers = raw.left(split).toLower();
            const int marker = headers.indexOf("content-length:");
            int length = 0;
            if (marker >= 0) {
                const int valueAt = marker + int(sizeof("content-length:") - 1);
                int end = headers.indexOf("\r\n", valueAt);
                if (end < 0)
                    end = headers.size();
                length = headers.mid(valueAt, end - valueAt).trimmed().toInt();
            }
            if (raw.size() >= split + 4 + length)
                break;
        }
        if (!socket.waitForReadyRead(3000))
            break;
        raw += socket.readAll();
    }
    raw += socket.readAll();

    const int split = raw.indexOf("\r\n\r\n");
    if (split < 0 || !raw.startsWith("HTTP/")) {
        reply.error = QStringLiteral("short response");
        return reply;
    }
    const QByteArray statusLine = raw.left(raw.indexOf("\r\n"));
    const QList<QByteArray> parts = statusLine.split(' ');
    if (parts.size() >= 2)
        reply.status = parts.at(1).toInt();
    reply.body = raw.mid(split + 4);
    return reply;
}

QString errorCode(const HttpReply &reply)
{
    const QJsonDocument document = QJsonDocument::fromJson(reply.body);
    if (!document.isObject())
        return {};
    return document.object().value(QStringLiteral("error")).toString();
}

QString serverBinary()
{
    const QString base = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    return QDir(base).filePath(QStringLiteral("hotel-api.exe"));
#else
    return QDir(base).filePath(QStringLiteral("hotel-api"));
#endif
}

class ServerProcess {
public:
    bool start(const QString &iniBody, const QProcessEnvironment &extra, QString *error)
    {
        if (!dir.isValid()) {
            *error = QStringLiteral("temp dir");
            return false;
        }
        const QString ini = dir.filePath(QStringLiteral("hotel-api.ini"));
        QFile file(ini);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            *error = QStringLiteral("ini");
            return false;
        }
        file.write(iniBody.toUtf8());
        file.close();

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("HOTEL_DSN"));
        env.remove(QStringLiteral("HOTEL_LISTEN"));
        env.remove(QStringLiteral("HOTEL_WS_LISTEN"));
        env.remove(QStringLiteral("HOTEL_MYSQL_HOST"));
        env.remove(QStringLiteral("HOTEL_MYSQL_PORT"));
        env.remove(QStringLiteral("HOTEL_MYSQL_SCHEMA"));
        env.remove(QStringLiteral("HOTEL_MYSQL_USER"));
        env.remove(QStringLiteral("HOTEL_MYSQL_PASSWORD"));
        env.insert(QStringLiteral("HOTEL_CONFIG"), ini);
        const QStringList keys = extra.keys();
        for (const QString &key : keys)
            env.insert(key, extra.value(key));

        process.setProcessEnvironment(env);
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start(serverBinary(), {});
        if (!process.waitForStarted(5000)) {
            *error = QStringLiteral("did not start");
            return false;
        }
        return true;
    }

    void stop()
    {
        if (process.state() == QProcess::NotRunning)
            return;
        process.terminate();
        if (!process.waitForFinished(3000)) {
            process.kill();
            process.waitForFinished(2000);
        }
    }

    QString output()
    {
        return QString::fromUtf8(process.readAllStandardError()) + QString::fromUtf8(process.readAllStandardOutput());
    }

    QTemporaryDir dir;
    QProcess process;
};

} // namespace

class AuthTest : public QObject {
    Q_OBJECT

private slots:
    void bearerHeaderAcceptsOnly64Hex();
    void sha256IsLowerHex();
    void accessPolicy();
    void routesRejectMissingBearerWhenDatabaseIsOff();
    void databaseSessionRoundTrip();
};

void AuthTest::bearerHeaderAcceptsOnly64Hex()
{
    const QByteArray token(64, 'a');
    QCOMPARE(bearerTokenFromHeader("Bearer " + token), token);
    QCOMPARE(bearerTokenFromHeader("bearer " + token), token);
    QCOMPARE(bearerTokenFromHeader("  Bearer   " + token + "  "), token);
    QVERIFY(bearerTokenFromHeader(QByteArray()).isEmpty());
    QVERIFY(bearerTokenFromHeader("Basic " + token).isEmpty());
    QVERIFY(bearerTokenFromHeader("Bearer short").isEmpty());
    QVERIFY(bearerTokenFromHeader("Bearer " + QByteArray(64, 'g')).isEmpty());
    QVERIFY(bearerTokenFromHeader("Bearer" + token).isEmpty());
    QVERIFY(isSessionToken(token));
    QVERIFY(!isSessionToken(token.left(63)));
}

void AuthTest::sha256IsLowerHex()
{
    const QByteArray token(64, 'b');
    const QByteArray hash = sha256Hex(token);
    QCOMPARE(hash, QCryptographicHash::hash(token, QCryptographicHash::Sha256).toHex());
    QCOMPARE(hash.size(), 64);
    QCOMPARE(hash, hash.toLower());
}

void AuthTest::accessPolicy()
{
    const AccessDecision missing = decideAccess(false, false, false, false, false, false);
    QCOMPARE(missing.httpStatus, 401);
    QCOMPARE(missing.code, "unauthorized");

    const AccessDecision revoked = decideAccess(true, true, true, true, true, true);
    QCOMPARE(revoked.httpStatus, 401);
    QCOMPARE(revoked.code, "unauthorized");

    const AccessDecision expired = decideAccess(true, false, true, false, true, false);
    QCOMPARE(expired.httpStatus, 401);
    QCOMPARE(expired.code, "session_expired");

    const AccessDecision disabled = decideAccess(true, false, false, true, true, false);
    QCOMPARE(disabled.httpStatus, 401);
    QCOMPARE(disabled.code, "user_disabled");

    const AccessDecision readOnly = decideAccess(true, false, false, false, false, false);
    QCOMPARE(readOnly.httpStatus, 200);
    QVERIFY(readOnly.code == nullptr);

    const AccessDecision forbidden = decideAccess(true, false, false, false, false, true);
    QCOMPARE(forbidden.httpStatus, 403);
    QCOMPARE(forbidden.code, "commands_not_allowed");

    const AccessDecision writer = decideAccess(true, false, false, false, true, true);
    QCOMPARE(writer.httpStatus, 200);
}

void AuthTest::routesRejectMissingBearerWhenDatabaseIsOff()
{
    const QString bin = serverBinary();
    if (!QFileInfo::exists(bin))
        QSKIP("hotel-api binary is not next to the test");

    ServerProcess server;
    QString error;
    QVERIFY2(server.start(QStringLiteral("listen=127.0.0.1:18082\nmysql_host=\nmysql_schema=\nws_listen=\n"),
                          QProcessEnvironment(),
                          &error),
             qPrintable(error + server.output()));

    const quint16 port = 18082;
    bool health = false;
    for (int i = 0; i < 40 && !health; ++i) {
        const HttpReply reply = httpCall(port, "GET", "/health", {}, {});
        health = reply.status == 200 && reply.body.contains("\"skipped\"");
        if (!health)
            QTest::qWait(100);
    }
    if (!health) {
        const QString output = server.output();
        server.stop();
        QFAIL(qPrintable(QStringLiteral("health did not come up: ") + output));
    }

    const HttpReply root = httpCall(port, "GET", "/api/v1", {}, {});
    QCOMPARE(root.status, 401);
    QCOMPARE(errorCode(root), QStringLiteral("unauthorized"));

    const HttpReply bad = httpCall(port, "GET", "/api/v1", {}, "Bearer nope");
    QCOMPARE(bad.status, 401);
    QCOMPARE(errorCode(bad), QStringLiteral("unauthorized"));

    const QByteArray shaped(64, 'a');
    const HttpReply shapedReply = httpCall(port, "GET", "/api/v1/sessions/current", {}, "Bearer " + shaped);
    QCOMPARE(shapedReply.status, 503);
    QCOMPARE(errorCode(shapedReply), QStringLiteral("database_not_configured"));
    QVERIFY(!shapedReply.body.contains(shaped));

    const HttpReply current = httpCall(port, "GET", "/api/v1/sessions/current", {}, {});
    QCOMPARE(current.status, 401);
    QCOMPARE(errorCode(current), QStringLiteral("unauthorized"));

    const HttpReply logout = httpCall(port, "DELETE", "/api/v1/sessions", {}, {});
    QCOMPARE(logout.status, 401);
    QCOMPARE(errorCode(logout), QStringLiteral("unauthorized"));

    const HttpReply login = httpCall(port, "POST", "/api/v1/sessions", "{\"login\":\"nobody\",\"password\":\"x\"}", {});
    QCOMPARE(login.status, 503);
    QCOMPARE(errorCode(login), QStringLiteral("database_not_configured"));

    const HttpReply open = httpCall(port, "GET", "/health", {}, {});
    QCOMPARE(open.status, 200);

    server.stop();
}

void AuthTest::databaseSessionRoundTrip()
{
    const QString host = qEnvironmentVariable("HOTEL_TEST_MYSQL_HOST");
    if (host.isEmpty())
        QSKIP("HOTEL_TEST_MYSQL_HOST is not set");

    const QString portText = qEnvironmentVariable("HOTEL_TEST_MYSQL_PORT", QStringLiteral("3306"));
    const QString schema = qEnvironmentVariable("HOTEL_TEST_MYSQL_SCHEMA");
    const QString user = qEnvironmentVariable("HOTEL_TEST_MYSQL_USER");
    const QString password = qEnvironmentVariable("HOTEL_TEST_MYSQL_PASSWORD");
    QVERIFY(!schema.isEmpty());
    QVERIFY(!user.isEmpty());

    const QString migration = QStringLiteral(HOTEL_MIGRATION_0002);
    QVERIFY2(QFileInfo::exists(migration), qPrintable(migration));

    QTemporaryDir secretDir;
    QVERIFY(secretDir.isValid());
    const QString defaults = secretDir.filePath(QStringLiteral("client.cnf"));
    QFile defaultsFile(defaults);
    QVERIFY(defaultsFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray cnf = QByteArray("[client]\nhost=") + host.toUtf8()
        + "\nport=" + portText.toUtf8()
        + "\nuser=" + user.toUtf8()
        + "\npassword=" + password.toUtf8() + "\n";
    QCOMPARE(defaultsFile.write(cnf), qint64(cnf.size()));
    defaultsFile.close();

    const auto mysql = [&](const QString &sql, const QString &database) {
        QProcess process;
        process.start(QStringLiteral("mariadb"),
                      {QStringLiteral("--defaults-extra-file=") + defaults,
                       QStringLiteral("--default-character-set=utf8mb4"),
                       database,
                       QStringLiteral("-e"),
                       sql});
        const bool finished = process.waitForFinished(60000);
        const QString output = QString::fromUtf8(process.readAllStandardError());
        return finished && process.exitCode() == 0 ? QString() : output + QString::fromUtf8(process.readAllStandardOutput());
    };

    const QString drop = QStringLiteral(
        "SET FOREIGN_KEY_CHECKS=0;"
        "DROP TABLE IF EXISTS nx_posting, nx_folio, nx_stay_guest, nx_stay, nx_reservation, "
        "nx_room, nx_guest, nx_session, nx_user, nx_role_permission, nx_role, nx_permission, "
        "nx_room_type, nx_building, nx_voucher, nx_setting, nx_property;"
        "SET FOREIGN_KEY_CHECKS=1;");
    const QString dropped = mysql(drop, schema);
    QVERIFY2(dropped.isEmpty(), qPrintable(dropped));
    const QString applied = mysql(QStringLiteral("source ") + migration, schema);
    QVERIFY2(applied.isEmpty(), qPrintable(applied));

    const QString connectionName = QStringLiteral("auth-test");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connectionName);
        db.setHostName(host);
        db.setPort(portText.toInt());
        db.setDatabaseName(schema);
        db.setUserName(user);
        db.setPassword(password);
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        QSqlQuery names(db);
        QVERIFY(names.exec(QStringLiteral("SET NAMES utf8mb4")));

        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_property (code, name, timezone_name, currency_code, created_at) "
            "VALUES ('AUTH', 'Auth test', 'UTC', 'AMD', UTC_TIMESTAMP())")));
        QSqlQuery property(db);
        QVERIFY(property.exec(QStringLiteral("SELECT id FROM nx_property WHERE code='AUTH'")));
        QVERIFY(property.next());
        const qint64 propId = property.value(0).toLongLong();

        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_permission (code, name) VALUES ('desk', 'Desk')")));
        QSqlQuery permission(db);
        QVERIFY(permission.exec(QStringLiteral("SELECT id FROM nx_permission WHERE code='desk'")));
        QVERIFY(permission.next());
        const qint64 permissionId = permission.value(0).toLongLong();

        auto insertRole = [&](const QString &code, qint64 *roleId) {
            QSqlQuery insert(db);
            insert.prepare(QStringLiteral("INSERT INTO nx_role (property_id, code, name) VALUES (:p, :c, :n)"));
            insert.bindValue(QStringLiteral(":p"), propId);
            insert.bindValue(QStringLiteral(":c"), code);
            insert.bindValue(QStringLiteral(":n"), code);
            if (!insert.exec())
                return false;
            QSqlQuery id(db);
            id.prepare(QStringLiteral("SELECT id FROM nx_role WHERE property_id=:p AND code=:c"));
            id.bindValue(QStringLiteral(":p"), propId);
            id.bindValue(QStringLiteral(":c"), code);
            if (!id.exec() || !id.next())
                return false;
            *roleId = id.value(0).toLongLong();
            return true;
        };
        qint64 writerRole = 0;
        qint64 readerRole = 0;
        QVERIFY(insertRole(QStringLiteral("writer"), &writerRole));
        QVERIFY(insertRole(QStringLiteral("reader"), &readerRole));
        QSqlQuery grant(db);
        grant.prepare(QStringLiteral("INSERT INTO nx_role_permission (role_id, permission_id) VALUES (:r, :p)"));
        grant.bindValue(QStringLiteral(":r"), writerRole);
        grant.bindValue(QStringLiteral(":p"), permissionId);
        QVERIFY(grant.exec());

        const QByteArray hash = QCryptographicHash::hash(QByteArrayLiteral("test-pass"), QCryptographicHash::Md5).toHex();
        auto insertUser = [&](const QString &login, qint64 roleId) {
            QSqlQuery insert(db);
            insert.prepare(QStringLiteral(
                "INSERT INTO nx_user (property_id, role_id, login, first_name, last_name, password_hash, "
                "password_scheme, state, created_at) "
                "VALUES (:p, :r, :login, 'Test', 'User', :hash, 'md5', 'active', UTC_TIMESTAMP())"));
            insert.bindValue(QStringLiteral(":p"), propId);
            insert.bindValue(QStringLiteral(":r"), roleId);
            insert.bindValue(QStringLiteral(":login"), login);
            insert.bindValue(QStringLiteral(":hash"), QString::fromLatin1(hash));
            return insert.exec();
        };
        QVERIFY2(insertUser(QStringLiteral("writer"), writerRole), "writer user");
        QVERIFY2(insertUser(QStringLiteral("reader"), readerRole), "reader user");
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    ServerProcess server;
    QProcessEnvironment mysqlEnv;
    mysqlEnv.insert(QStringLiteral("HOTEL_MYSQL_HOST"), host);
    mysqlEnv.insert(QStringLiteral("HOTEL_MYSQL_PORT"), portText);
    mysqlEnv.insert(QStringLiteral("HOTEL_MYSQL_SCHEMA"), schema);
    mysqlEnv.insert(QStringLiteral("HOTEL_MYSQL_USER"), user);
    mysqlEnv.insert(QStringLiteral("HOTEL_MYSQL_PASSWORD"), password);
    QString error;
    QVERIFY2(server.start(QStringLiteral("listen=127.0.0.1:18083\nws_listen=\n"), mysqlEnv, &error),
             qPrintable(error));

    const quint16 httpPort = 18083;
    bool up = false;
    QString healthBody;
    for (int i = 0; i < 40 && !up; ++i) {
        const HttpReply reply = httpCall(httpPort, "GET", "/health", {}, {});
        healthBody = QString::fromUtf8(reply.body);
        up = reply.status == 200 && reply.body.contains("\"up\"");
        if (!up)
            QTest::qWait(150);
    }
    if (!up) {
        const QString output = server.output();
        server.stop();
        QFAIL(qPrintable(healthBody + QLatin1Char('\n') + output));
    }

    const auto login = [&](const QByteArray &loginName) {
        const QByteArray body = "{\"login\":\"" + loginName + "\",\"password\":\"test-pass\"}";
        return httpCall(httpPort, "POST", "/api/v1/sessions", body, {});
    };

    const HttpReply writerLogin = login("writer");
    QCOMPARE(writerLogin.status, 200);
    const QJsonObject writerJson = QJsonDocument::fromJson(writerLogin.body).object();
    QVERIFY(writerJson.value(QStringLiteral("commands_allowed")).toBool());
    const QByteArray token = writerJson.value(QStringLiteral("token")).toString().toLatin1();
    QVERIFY(isSessionToken(token));
    const QByteArray bearer = "Bearer " + token;

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connectionName);
        db.setHostName(host);
        db.setPort(portText.toInt());
        db.setDatabaseName(schema);
        db.setUserName(user);
        db.setPassword(password);
        QVERIFY(db.open());
        QSqlQuery stored(db);
        QVERIFY(stored.exec(QStringLiteral("SELECT token_hash FROM nx_session")));
        QVERIFY(stored.next());
        QCOMPARE(stored.value(0).toString(), QString::fromLatin1(sha256Hex(token)));
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    const HttpReply root = httpCall(httpPort, "GET", "/api/v1", {}, bearer);
    QCOMPARE(root.status, 200);
    QVERIFY(root.body.contains("hotel-api"));
    QVERIFY(!root.body.contains(token));

    const HttpReply current = httpCall(httpPort, "GET", "/api/v1/sessions/current", {}, bearer);
    QCOMPARE(current.status, 200);
    const QJsonObject currentJson = QJsonDocument::fromJson(current.body).object();
    QCOMPARE(currentJson.value(QStringLiteral("user")).toObject().value(QStringLiteral("login")).toString(),
             QStringLiteral("writer"));
    QVERIFY(!current.body.contains(token));
    QVERIFY(currentJson.value(QStringLiteral("commands_allowed")).toBool());

    const HttpReply readerLogin = login("reader");
    QCOMPARE(readerLogin.status, 200);
    const QJsonObject readerJson = QJsonDocument::fromJson(readerLogin.body).object();
    QVERIFY(!readerJson.value(QStringLiteral("commands_allowed")).toBool());
    const QByteArray readerBearer = "Bearer " + readerJson.value(QStringLiteral("token")).toString().toLatin1();
    const HttpReply readerCurrent = httpCall(httpPort, "GET", "/api/v1/sessions/current", {}, readerBearer);
    QCOMPARE(readerCurrent.status, 200);
    QVERIFY(!QJsonDocument::fromJson(readerCurrent.body).object().value(QStringLiteral("commands_allowed")).toBool());

    const HttpReply gone = httpCall(httpPort, "DELETE", "/api/v1/sessions", {}, bearer);
    QCOMPARE(gone.status, 200);
    QVERIFY(QJsonDocument::fromJson(gone.body).object().value(QStringLiteral("revoked")).toBool());
    const HttpReply after = httpCall(httpPort, "GET", "/api/v1/sessions/current", {}, bearer);
    QCOMPARE(after.status, 401);
    QCOMPARE(errorCode(after), QStringLiteral("unauthorized"));

    const HttpReply again = login("writer");
    QCOMPARE(again.status, 200);
    const QByteArray againToken = QJsonDocument::fromJson(again.body).object().value(QStringLiteral("token")).toString().toLatin1();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connectionName);
        db.setHostName(host);
        db.setPort(portText.toInt());
        db.setDatabaseName(schema);
        db.setUserName(user);
        db.setPassword(password);
        QVERIFY(db.open());
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "UPDATE nx_session SET expires_at = '2000-01-01 00:00:00' "
            "WHERE revoked_at IS NULL AND user_id = (SELECT id FROM nx_user WHERE login='writer')")));
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    const HttpReply expired = httpCall(httpPort, "GET", "/api/v1", {}, "Bearer " + againToken);
    QCOMPARE(expired.status, 401);
    QCOMPARE(errorCode(expired), QStringLiteral("session_expired"));

    const HttpReply fresh = login("writer");
    QCOMPARE(fresh.status, 200);
    const QByteArray freshToken = QJsonDocument::fromJson(fresh.body).object().value(QStringLiteral("token")).toString().toLatin1();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connectionName);
        db.setHostName(host);
        db.setPort(portText.toInt());
        db.setDatabaseName(schema);
        db.setUserName(user);
        db.setPassword(password);
        QVERIFY(db.open());
        QVERIFY(QSqlQuery(db).exec(QStringLiteral("UPDATE nx_user SET state='disabled' WHERE login='writer'")));
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    const HttpReply disabled = httpCall(httpPort, "GET", "/api/v1/sessions/current", {}, "Bearer " + freshToken);
    QCOMPARE(disabled.status, 401);
    QCOMPARE(errorCode(disabled), QStringLiteral("user_disabled"));

    const HttpReply denied = login("writer");
    QCOMPARE(denied.status, 401);
    QCOMPARE(errorCode(denied), QStringLiteral("unauthorized"));

    server.stop();
    QVERIFY(!server.output().contains(password));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    AuthTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_auth.moc"
