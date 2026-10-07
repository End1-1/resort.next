#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QWebSocket>

namespace {

struct HttpReply {
    int status = 0;
    QByteArray body;
};

HttpReply httpCall(quint16 port, const QByteArray &method, const QByteArray &path, const QByteArray &authorization, const QByteArray &body = {})
{
    HttpReply reply;
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!socket.waitForConnected(3000))
        return reply;
    QByteArray request = method + ' ' + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n";
    if (!authorization.isEmpty())
        request += "Authorization: " + authorization + "\r\n";
    if (!body.isEmpty())
        request += "Content-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
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
    if (split < 0)
        return reply;
    const QList<QByteArray> parts = raw.left(raw.indexOf("\r\n")).split(' ');
    if (parts.size() >= 2)
        reply.status = parts.at(1).toInt();
    reply.body = raw.mid(split + 4);
    return reply;
}

QString serverBinary()
{
#ifdef Q_OS_WIN
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("hotel-api.exe"));
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("hotel-api"));
#endif
}

bool waitHealth(quint16 port)
{
    for (int i = 0; i < 40; ++i) {
        const HttpReply health = httpCall(port, "GET", "/health", {});
        if (health.status == 200)
            return true;
        QTest::qWait(50);
    }
    return false;
}

} // namespace

class RealtimeTest : public QObject {
    Q_OBJECT

private slots:
    void unauthenticatedSocketIsClosed();
    void authenticatedSocketReceivesReservationEvents();
};

void RealtimeTest::unauthenticatedSocketIsClosed()
{
    const QString bin = serverBinary();
    if (!QFileInfo::exists(bin))
        QSKIP("hotel-api binary is not next to the test");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath(QStringLiteral("hotel-api.ini")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("listen=127.0.0.1:18095\nmysql_host=\nmysql_schema=\nws_listen=127.0.0.1:18096\n");
    file.close();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("HOTEL_DSN"));
    env.remove(QStringLiteral("HOTEL_MYSQL_HOST"));
    env.remove(QStringLiteral("HOTEL_MYSQL_SCHEMA"));
    env.remove(QStringLiteral("HOTEL_MYSQL_USER"));
    env.remove(QStringLiteral("HOTEL_MYSQL_PASSWORD"));
    env.remove(QStringLiteral("HOTEL_WS_LISTEN"));
    env.insert(QStringLiteral("HOTEL_CONFIG"), dir.filePath(QStringLiteral("hotel-api.ini")));
    QProcess server;
    server.setProcessEnvironment(env);
    server.setProcessChannelMode(QProcess::SeparateChannels);
    server.start(bin, {});
    QVERIFY(server.waitForStarted(5000));
    QVERIFY(waitHealth(18095));

    QWebSocket socket;
    QSignalSpy messages(&socket, &QWebSocket::textMessageReceived);
    socket.open(QUrl(QStringLiteral("ws://127.0.0.1:18096/api/v1/ws")));
    QTRY_VERIFY_WITH_TIMEOUT(socket.state() == QAbstractSocket::ConnectedState, 5000);
    QTest::qWait(200);
    QCOMPARE(messages.count(), 0);
    socket.close();

    QWebSocket bad;
    QSignalSpy badMessages(&bad, &QWebSocket::textMessageReceived);
    QSignalSpy badClosed(&bad, &QWebSocket::disconnected);
    QObject::connect(&bad, &QWebSocket::connected, &bad, [&bad]() {
        bad.sendTextMessage(QStringLiteral("{\"type\":\"hello\"}"));
    });
    bad.open(QUrl(QStringLiteral("ws://127.0.0.1:18096/api/v1/ws")));
    QTRY_VERIFY_WITH_TIMEOUT(badClosed.count() >= 1, 5000);
    QCOMPARE(badMessages.count(), 0);

    server.terminate();
    if (!server.waitForFinished(3000)) {
        server.kill();
        server.waitForFinished(2000);
    }
}

void RealtimeTest::authenticatedSocketReceivesReservationEvents()
{
    const QString host = qEnvironmentVariable("HOTEL_TEST_MYSQL_HOST");
    if (host.isEmpty())
        QSKIP("HOTEL_TEST_MYSQL_HOST is not set");
    const QString portText = qEnvironmentVariable("HOTEL_TEST_MYSQL_PORT", QStringLiteral("3306"));
    const QString schema = qEnvironmentVariable("HOTEL_TEST_MYSQL_SCHEMA");
    const QString user = qEnvironmentVariable("HOTEL_TEST_MYSQL_USER");
    const QString password = qEnvironmentVariable("HOTEL_TEST_MYSQL_PASSWORD");

    QTemporaryDir secretDir;
    QVERIFY(secretDir.isValid());
    const QString defaults = secretDir.filePath(QStringLiteral("client.cnf"));
    {
        QFile file(defaults);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray cnf = "[client]\nhost=" + host.toUtf8() + "\nport=" + portText.toUtf8() + "\nuser=" + user.toUtf8()
            + "\npassword=" + password.toUtf8() + "\n";
        QCOMPARE(file.write(cnf), qint64(cnf.size()));
    }
    const auto mysql = [&](const QString &sql) {
        QProcess process;
        process.start(QStringLiteral("mariadb"),
                      {QStringLiteral("--defaults-extra-file=") + defaults,
                       QStringLiteral("--default-character-set=utf8mb4"),
                       schema,
                       QStringLiteral("-e"),
                       sql});
        if (!process.waitForFinished(60000) || process.exitCode() != 0)
            return QString::fromUtf8(process.readAllStandardError() + process.readAllStandardOutput());
        return QString();
    };
    QVERIFY2(mysql(QStringLiteral(
                       "SET FOREIGN_KEY_CHECKS=0; DROP TABLE IF EXISTS nx_posting, nx_folio, nx_stay_guest, nx_stay, "
                       "nx_reservation, nx_room, nx_guest, nx_session, nx_user, nx_role_permission, nx_role, nx_permission, "
                       "nx_room_type, nx_building, nx_label, nx_voucher, nx_setting, nx_property; SET FOREIGN_KEY_CHECKS=1;"))
                 .isEmpty(),
             "drop");
    QVERIFY2(mysql(QStringLiteral("source ") + QStringLiteral(HOTEL_MIGRATION_0002)).isEmpty(), "0002");
    QVERIFY2(mysql(QStringLiteral("source ") + QStringLiteral(HOTEL_MIGRATION_0003)).isEmpty(), "0003");
    QVERIFY2(mysql(QStringLiteral("source ") + QStringLiteral(HOTEL_MIGRATION_0004)).isEmpty(), "0004");

    qint64 roomId = 0;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), QStringLiteral("realtime-test"));
        db.setHostName(host);
        db.setPort(portText.toInt());
        db.setDatabaseName(schema);
        db.setUserName(user);
        db.setPassword(password);
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_property (code, name, timezone_name, currency_code, created_at) "
            "VALUES ('LIVE', 'Live', 'UTC', 'AMD', UTC_TIMESTAMP())")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_room_type (property_id, code, name) SELECT id, 'STD', 'Standard' FROM nx_property WHERE code='LIVE'")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_room (property_id, room_type_id, code, status_code) "
            "SELECT p.id, t.id, '101', 'vacant_ready' FROM nx_property p JOIN nx_room_type t ON t.property_id=p.id WHERE p.code='LIVE'")));
        QSqlQuery room(db);
        QVERIFY(room.exec(QStringLiteral("SELECT r.id FROM nx_room r JOIN nx_property p ON p.id=r.property_id WHERE p.code='LIVE'")));
        QVERIFY(room.next());
        roomId = room.value(0).toLongLong();
        QVERIFY(QSqlQuery(db).exec(QStringLiteral("INSERT INTO nx_permission (code, name) VALUES ('desk', 'Desk')")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_role (property_id, code, name) SELECT id, 'desk', 'Desk' FROM nx_property WHERE code='LIVE'")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_role_permission (role_id, permission_id) "
            "SELECT r.id, p.id FROM nx_role r JOIN nx_permission p ON p.code='desk' WHERE r.code='desk'")));
        const QByteArray hash = QCryptographicHash::hash(QByteArrayLiteral("test-pass"), QCryptographicHash::Md5).toHex();
        QSqlQuery writer(db);
        QVERIFY(writer.prepare(QStringLiteral(
            "INSERT INTO nx_user (property_id, role_id, login, first_name, last_name, password_hash, password_scheme, state, created_at) "
            "SELECT p.id, r.id, 'live', 'L', 'User', :hash, 'md5', 'active', UTC_TIMESTAMP() "
            "FROM nx_property p JOIN nx_role r ON r.property_id=p.id AND r.code='desk' WHERE p.code='LIVE'")));
        writer.bindValue(QStringLiteral(":hash"), QString::fromLatin1(hash));
        QVERIFY(writer.exec());
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(QStringLiteral("realtime-test"));
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile ini(dir.filePath(QStringLiteral("hotel-api.ini")));
    QVERIFY(ini.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ini.write("listen=127.0.0.1:18097\nws_listen=127.0.0.1:18098\n");
    ini.close();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("HOTEL_DSN"));
    env.remove(QStringLiteral("HOTEL_LISTEN"));
    env.remove(QStringLiteral("HOTEL_WS_LISTEN"));
    env.insert(QStringLiteral("HOTEL_CONFIG"), dir.filePath(QStringLiteral("hotel-api.ini")));
    env.insert(QStringLiteral("HOTEL_MYSQL_HOST"), host);
    env.insert(QStringLiteral("HOTEL_MYSQL_PORT"), portText);
    env.insert(QStringLiteral("HOTEL_MYSQL_SCHEMA"), schema);
    env.insert(QStringLiteral("HOTEL_MYSQL_USER"), user);
    env.insert(QStringLiteral("HOTEL_MYSQL_PASSWORD"), password);
    QProcess server;
    server.setProcessEnvironment(env);
    server.setProcessChannelMode(QProcess::SeparateChannels);
    server.start(serverBinary(), {});
    QVERIFY(server.waitForStarted(5000));
    bool up = false;
    for (int i = 0; i < 40 && !up; ++i) {
        const HttpReply health = httpCall(18097, "GET", "/health", {});
        up = health.status == 200 && health.body.contains("\"up\"");
        if (!up)
            QTest::qWait(100);
    }
    QVERIFY(up);

    const HttpReply loggedIn = httpCall(18097, "POST", "/api/v1/sessions", {}, "{\"login\":\"live\",\"password\":\"test-pass\"}");
    QCOMPARE(loggedIn.status, 200);
    const QString token = QJsonDocument::fromJson(loggedIn.body).object().value(QStringLiteral("token")).toString();
    const QByteArray bearer = "Bearer " + token.toLatin1();

    QWebSocket socket;
    QSignalSpy messages(&socket, &QWebSocket::textMessageReceived);
    socket.open(QUrl(QStringLiteral("ws://127.0.0.1:18098/api/v1/ws?token=") + token));
    QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 1, 5000);
    QVERIFY(messages.at(0).at(0).toString().contains(QStringLiteral("hello")));

    const QByteArray body = QByteArray("{\"room_id\":") + QByteArray::number(roomId)
        + ",\"arrival\":\"2026-12-01\",\"departure\":\"2026-12-03\",\"guest\":{\"last_name\":\"Live\",\"first_name\":\"Ada\"}}";
    const HttpReply created = httpCall(18097, "POST", "/api/v1/reservations", bearer, body);
    QCOMPARE(created.status, 201);
    QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 2, 5000);
    QVERIFY(messages.at(1).at(0).toString().contains(QStringLiteral("reservation.created")));

    const qint64 id = QJsonDocument::fromJson(created.body).object().value(QStringLiteral("id")).toInteger();
    const HttpReply inHouse = httpCall(18097,
                                       "PATCH",
                                       "/api/v1/reservations/" + QByteArray::number(id),
                                       bearer,
                                       "{\"version\":0,\"state_code\":\"in_house\"}");
    QCOMPARE(inHouse.status, 200);
    QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 4, 5000);
    const QString third = messages.at(2).at(0).toString();
    const QString fourth = messages.at(3).at(0).toString();
    QVERIFY(third.contains(QStringLiteral("reservation.updated")));
    QVERIFY(fourth.contains(QStringLiteral("room.status_changed")));
    QVERIFY(fourth.contains(QStringLiteral("occupied")));

    server.terminate();
    server.waitForFinished(3000);
    QVERIFY(!QString::fromUtf8(server.readAllStandardError()).contains(password));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    RealtimeTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_realtime.moc"
