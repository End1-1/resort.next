#include "reservations.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
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
};

HttpReply httpCall(quint16 port,
                   const QByteArray &method,
                   const QByteArray &path,
                   const QByteArray &authorization,
                   const QByteArray &body = {})
{
    HttpReply reply;
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!socket.waitForConnected(3000))
        return reply;
    QByteArray request = method + ' ' + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nAccept: application/json\r\n";
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

} // namespace

class ReservationTest : public QObject {
    Q_OBJECT

private slots:
    void reservationsRequireBearerWhenDatabaseIsOff();
    void databaseRejectsOverlapAndIllegalTransitions();
};

void ReservationTest::reservationsRequireBearerWhenDatabaseIsOff()
{
    const QString bin = serverBinary();
    if (!QFileInfo::exists(bin))
        QSKIP("hotel-api binary is not next to the test");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath(QStringLiteral("hotel-api.ini")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("listen=127.0.0.1:18089\nmysql_host=\nmysql_schema=\nws_listen=\n");
    file.close();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("HOTEL_DSN"));
    env.remove(QStringLiteral("HOTEL_MYSQL_HOST"));
    env.remove(QStringLiteral("HOTEL_MYSQL_PORT"));
    env.remove(QStringLiteral("HOTEL_MYSQL_SCHEMA"));
    env.remove(QStringLiteral("HOTEL_MYSQL_USER"));
    env.remove(QStringLiteral("HOTEL_MYSQL_PASSWORD"));
    env.remove(QStringLiteral("HOTEL_WS_LISTEN"));
    env.insert(QStringLiteral("HOTEL_CONFIG"), dir.filePath(QStringLiteral("hotel-api.ini")));
    env.insert(QStringLiteral("HOTEL_LISTEN"), QStringLiteral("127.0.0.1:18089"));

    QProcess server;
    server.setProcessEnvironment(env);
    server.setProcessChannelMode(QProcess::SeparateChannels);
    server.start(bin, {});
    QVERIFY(server.waitForStarted(5000));
    bool health = false;
    for (int i = 0; i < 40 && !health; ++i) {
        health = httpCall(18089, "GET", "/health", {}).status == 200;
        if (!health)
            QTest::qWait(50);
    }
    const HttpReply denied = httpCall(18089, "GET", "/api/v1/reservations", {});
    const HttpReply post = httpCall(18089, "POST", "/api/v1/reservations", {}, "{}");
    server.terminate();
    if (!server.waitForFinished(3000)) {
        server.kill();
        server.waitForFinished(2000);
    }
    QVERIFY(health);
    QCOMPARE(denied.status, 401);
    QCOMPARE(post.status, 401);
}

void ReservationTest::databaseRejectsOverlapAndIllegalTransitions()
{
    const QString host = qEnvironmentVariable("HOTEL_TEST_MYSQL_HOST");
    if (host.isEmpty())
        QSKIP("HOTEL_TEST_MYSQL_HOST is not set");
    const QString portText = qEnvironmentVariable("HOTEL_TEST_MYSQL_PORT", QStringLiteral("3306"));
    const QString schema = qEnvironmentVariable("HOTEL_TEST_MYSQL_SCHEMA");
    const QString user = qEnvironmentVariable("HOTEL_TEST_MYSQL_USER");
    const QString password = qEnvironmentVariable("HOTEL_TEST_MYSQL_PASSWORD");
    QVERIFY(!schema.isEmpty());

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
    const QString drop = QStringLiteral(
        "SET FOREIGN_KEY_CHECKS=0; DROP TABLE IF EXISTS nx_posting, nx_folio, nx_stay_guest, nx_stay, "
        "nx_reservation, nx_room, nx_guest, nx_session, nx_user, nx_role_permission, nx_role, nx_permission, "
        "nx_room_type, nx_building, nx_label, nx_voucher, nx_setting, nx_property; SET FOREIGN_KEY_CHECKS=1;");
    QVERIFY2(mysql(drop).isEmpty(), "drop");
    QVERIFY2(mysql(QStringLiteral("source ") + QStringLiteral(HOTEL_MIGRATION_0002)).isEmpty(), "0002");
    QVERIFY2(mysql(QStringLiteral("source ") + QStringLiteral(HOTEL_MIGRATION_0003)).isEmpty(), "0003");
    QVERIFY2(mysql(QStringLiteral("source ") + QStringLiteral(HOTEL_MIGRATION_0004)).isEmpty(), "0004");
    QVERIFY2(mysql(QStringLiteral("source ") + QStringLiteral(HOTEL_MIGRATION_0004)).isEmpty(), "0004 again");

    qint64 room101 = 0;
    qint64 room102 = 0;
    const QString connectionName = QStringLiteral("reservation-test");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connectionName);
        db.setHostName(host);
        db.setPort(portText.toInt());
        db.setDatabaseName(schema);
        db.setUserName(user);
        db.setPassword(password);
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_property (code, name, timezone_name, currency_code, created_at) "
            "VALUES ('BOOK', 'Book', 'UTC', 'AMD', UTC_TIMESTAMP())")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_room_type (property_id, code, name) SELECT id, 'STD', 'Standard' FROM nx_property WHERE code='BOOK'")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_room (property_id, room_type_id, code, floor, status_code) "
            "SELECT p.id, t.id, '101', 1, 'vacant_ready' FROM nx_property p "
            "JOIN nx_room_type t ON t.property_id=p.id WHERE p.code='BOOK'")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_room (property_id, room_type_id, code, floor, status_code) "
            "SELECT p.id, t.id, '102', 1, 'vacant_ready' FROM nx_property p "
            "JOIN nx_room_type t ON t.property_id=p.id WHERE p.code='BOOK'")));
        QSqlQuery rooms(db);
        QVERIFY2(rooms.exec(QStringLiteral(
                     "SELECT r.code, r.id FROM nx_room r JOIN nx_property p ON p.id=r.property_id WHERE p.code='BOOK'")),
                 qPrintable(rooms.lastError().text()));
        while (rooms.next()) {
            if (rooms.value(0).toString() == QLatin1String("101"))
                room101 = rooms.value(1).toLongLong();
            if (rooms.value(0).toString() == QLatin1String("102"))
                room102 = rooms.value(1).toLongLong();
        }
        QVERIFY(QSqlQuery(db).exec(QStringLiteral("INSERT INTO nx_permission (code, name) VALUES ('desk', 'Desk')")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_role (property_id, code, name) "
            "SELECT id, 'desk', 'Desk' FROM nx_property WHERE code='BOOK'")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_role (property_id, code, name) "
            "SELECT id, 'read', 'Read' FROM nx_property WHERE code='BOOK'")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_role_permission (role_id, permission_id) "
            "SELECT r.id, p.id FROM nx_role r JOIN nx_permission p ON p.code='desk' WHERE r.code='desk'")));
        const QByteArray hash = QCryptographicHash::hash(QByteArrayLiteral("test-pass"), QCryptographicHash::Md5).toHex();
        QSqlQuery writer(db);
        QVERIFY(writer.prepare(QStringLiteral(
            "INSERT INTO nx_user (property_id, role_id, login, first_name, last_name, password_hash, "
            "password_scheme, state, created_at) "
            "SELECT p.id, r.id, 'writer', 'W', 'User', :hash, 'md5', 'active', UTC_TIMESTAMP() "
            "FROM nx_property p JOIN nx_role r ON r.property_id=p.id AND r.code='desk' WHERE p.code='BOOK'")));
        writer.bindValue(QStringLiteral(":hash"), QString::fromLatin1(hash));
        QVERIFY(writer.exec());
        QSqlQuery reader(db);
        QVERIFY(reader.prepare(QStringLiteral(
            "INSERT INTO nx_user (property_id, role_id, login, first_name, last_name, password_hash, "
            "password_scheme, state, created_at) "
            "SELECT p.id, r.id, 'reader', 'R', 'User', :hash, 'md5', 'active', UTC_TIMESTAMP() "
            "FROM nx_property p JOIN nx_role r ON r.property_id=p.id AND r.code='read' WHERE p.code='BOOK'")));
        reader.bindValue(QStringLiteral(":hash"), QString::fromLatin1(hash));
        QVERIFY(reader.exec());
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    QVERIFY(room101 > 0);
    QVERIFY(room102 > 0);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile iniFile(dir.filePath(QStringLiteral("hotel-api.ini")));
    QVERIFY(iniFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    iniFile.write("listen=127.0.0.1:18090\nws_listen=\n");
    iniFile.close();

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
        const HttpReply health = httpCall(18090, "GET", "/health", {});
        up = health.status == 200 && health.body.contains("\"up\"");
        if (!up)
            QTest::qWait(100);
    }
    if (!up) {
        const QString output = QString::fromUtf8(server.readAllStandardError());
        server.kill();
        server.waitForFinished(2000);
        QFAIL(qPrintable(output));
    }

    const auto login = [](const char *name) {
        return httpCall(18090, "POST", "/api/v1/sessions", {}, QByteArray("{\"login\":\"") + name + "\",\"password\":\"test-pass\"}");
    };
    const HttpReply writerLogin = login("writer");
    QCOMPARE(writerLogin.status, 200);
    const QByteArray writer = "Bearer " + QJsonDocument::fromJson(writerLogin.body).object().value(QStringLiteral("token")).toString().toLatin1();
    const HttpReply readerLogin = login("reader");
    QCOMPARE(readerLogin.status, 200);
    const QByteArray reader = "Bearer " + QJsonDocument::fromJson(readerLogin.body).object().value(QStringLiteral("token")).toString().toLatin1();

    const auto createBody = [&](qint64 room, const char *arrival, const char *departure) {
        return QByteArray("{\"status_code\":\"confirmed\",\"room_id\":") + QByteArray::number(room)
            + ",\"arrival\":\"" + arrival + "\",\"departure\":\"" + departure
            + "\",\"guest\":{\"first_name\":\"Ani\",\"last_name\":\"Petrosyan\"}}";
    };
    const HttpReply created = httpCall(18090, "POST", "/api/v1/reservations", writer, createBody(room101, "2026-10-03", "2026-10-05"));
    QCOMPARE(created.status, 201);
    const QJsonObject detail = QJsonDocument::fromJson(created.body).object();
    QVERIFY(detail.value(QStringLiteral("created_by")).toInteger() > 0);
    QVERIFY(detail.value(QStringLiteral("updated_at")).isNull());
    const QJsonObject stay = detail.value(QStringLiteral("stays")).toArray().at(0).toObject();
    QCOMPARE(stay.value(QStringLiteral("state_code")).toString(), QStringLiteral("reserved"));
    QCOMPARE(stay.value(QStringLiteral("guest")).toObject().value(QStringLiteral("last_name")).toString(), QStringLiteral("Petrosyan"));
    const qint64 id = detail.value(QStringLiteral("id")).toInteger();
    QCOMPARE(stay.value(QStringLiteral("version")).toInt(), 0);

    const HttpReply overlap = httpCall(18090, "POST", "/api/v1/reservations", writer, createBody(room101, "2026-10-04", "2026-10-06"));
    QCOMPARE(overlap.status, 409);
    QVERIFY(overlap.body.contains("overlap"));

    const HttpReply adjacent = httpCall(18090, "POST", "/api/v1/reservations", writer, createBody(room101, "2026-10-05", "2026-10-07"));
    QCOMPARE(adjacent.status, 201);

    const HttpReply otherRoom = httpCall(18090, "POST", "/api/v1/reservations", writer, createBody(room102, "2026-10-03", "2026-10-05"));
    QCOMPARE(otherRoom.status, 201);

    const HttpReply listed = httpCall(18090, "GET", "/api/v1/reservations?guest=Petrosyan&room=101&from=2026-10-01&to=2026-10-08", writer);
    QCOMPARE(listed.status, 200);
    QCOMPARE(QJsonDocument::fromJson(listed.body).object().value(QStringLiteral("items")).toArray().size(), 2);

    const HttpReply denied = httpCall(18090, "POST", "/api/v1/reservations", reader, createBody(room102, "2026-11-01", "2026-11-03"));
    QCOMPARE(denied.status, 403);
    QVERIFY(denied.body.contains("commands_not_allowed"));

    const QByteArray patchBase = QByteArray::number(id);
    const HttpReply skipped = httpCall(18090,
                                       "PATCH",
                                       "/api/v1/reservations/" + patchBase,
                                       writer,
                                       "{\"version\":0,\"state_code\":\"checked_out\"}");
    QCOMPARE(skipped.status, 409);
    QVERIFY(skipped.body.contains("invalid_transition"));

    const HttpReply stale = httpCall(18090, "PATCH", "/api/v1/reservations/" + patchBase, writer, "{\"version\":4,\"remarks\":\"x\"}");
    QCOMPARE(stale.status, 409);
    QVERIFY(stale.body.contains("version_conflict"));

    const HttpReply inHouse = httpCall(18090, "PATCH", "/api/v1/reservations/" + patchBase, writer, "{\"version\":0,\"state_code\":\"in_house\"}");
    QCOMPARE(inHouse.status, 200);
    const QJsonObject afterIn = QJsonDocument::fromJson(inHouse.body).object();
    QCOMPARE(afterIn.value(QStringLiteral("stays")).toArray().at(0).toObject().value(QStringLiteral("state_code")).toString(),
             QStringLiteral("in_house"));
    QCOMPARE(afterIn.value(QStringLiteral("stays")).toArray().at(0).toObject().value(QStringLiteral("version")).toInt(), 1);
    QVERIFY(!afterIn.value(QStringLiteral("updated_by")).isNull());

    QSqlDatabase check = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), QStringLiteral("reservation-check"));
    check.setHostName(host);
    check.setPort(portText.toInt());
    check.setDatabaseName(schema);
    check.setUserName(user);
    check.setPassword(password);
    QVERIFY(check.open());
    {
        QSqlQuery status(check);
        QVERIFY(status.exec(QStringLiteral("SELECT status_code FROM nx_room WHERE id=") + QString::number(room101)));
        QVERIFY(status.next());
        QCOMPARE(status.value(0).toString(), QStringLiteral("occupied"));
    }

    const HttpReply locked = httpCall(18090,
                                      "PATCH",
                                      "/api/v1/reservations/" + patchBase,
                                      writer,
                                      "{\"version\":1,\"arrival\":\"2026-10-03\",\"departure\":\"2026-10-06\"}");
    QCOMPARE(locked.status, 409);
    QVERIFY(locked.body.contains("stay_locked"));

    const HttpReply checkedOut = httpCall(18090, "PATCH", "/api/v1/reservations/" + patchBase, writer, "{\"version\":1,\"state_code\":\"checked_out\"}");
    QCOMPARE(checkedOut.status, 200);
    {
        QSqlQuery status(check);
        QVERIFY(status.exec(QStringLiteral("SELECT status_code FROM nx_room WHERE id=") + QString::number(room101)));
        QVERIFY(status.next());
        QCOMPARE(status.value(0).toString(), QStringLiteral("vacant_dirty"));
    }
    check.close();
    check = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("reservation-check"));

    server.terminate();
    server.waitForFinished(3000);
    QVERIFY(!QString::fromUtf8(server.readAllStandardError()).contains(password));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ReservationTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_reservations.moc"
