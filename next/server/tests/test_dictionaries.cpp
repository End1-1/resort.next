#include "dictionaries.h"

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
                   const QByteArray &body = {},
                   const QByteArray &extraHeaders = {})
{
    HttpReply reply;
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!socket.waitForConnected(3000))
        return reply;
    QByteArray request = method + ' ' + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nAccept: application/json\r\n";
    if (!authorization.isEmpty())
        request += "Authorization: " + authorization + "\r\n";
    request += extraHeaders;
    if (!body.isEmpty()) {
        request += "Content-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
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

class DictionaryTest : public QObject {
    Q_OBJECT

private slots:
    void localeFallsBackToRussian();
    void roomStatusesAreCodes();
    void namedWriteValidation();
    void roomWriteValidation();
    void roomsRequireBearerWhenDatabaseIsOff();
    void databaseNamesFollowLocale();
};

void DictionaryTest::localeFallsBackToRussian()
{
    QCOMPARE(localeFromRequest(QStringLiteral("hy"), {}), QStringLiteral("hy"));
    QCOMPARE(localeFromRequest(QStringLiteral("EN"), {}), QStringLiteral("en"));
    QCOMPARE(localeFromRequest(QStringLiteral("hy-AM"), {}), QStringLiteral("hy"));
    QCOMPARE(localeFromRequest(QString(), "hy-AM,en;q=0.8"), QStringLiteral("hy"));
    QCOMPARE(localeFromRequest(QStringLiteral("fr"), "en;q=0.5"), QStringLiteral("en"));
    QCOMPARE(localeFromRequest(QStringLiteral("de"), "fr,de;q=0.9"), QStringLiteral("ru"));
    QCOMPARE(localeFromRequest(QString(), {}), QStringLiteral("ru"));
}

void DictionaryTest::roomStatusesAreCodes()
{
    const ApiResult result = listRoomStatuses(QStringLiteral("hy"));
    QCOMPARE(result.httpStatus, 200);
    QCOMPARE(result.body.value(QStringLiteral("lang")).toString(), QStringLiteral("hy"));
    const QJsonArray items = result.body.value(QStringLiteral("items")).toArray();
    QVERIFY(items.size() >= 7);
    bool ready = false;
    for (const QJsonValue &item : items) {
        QVERIFY(item.toObject().contains(QStringLiteral("code")));
        QVERIFY(!item.toObject().contains(QStringLiteral("name")));
        if (item.toObject().value(QStringLiteral("code")).toString() == QLatin1String("vacant_ready"))
            ready = true;
    }
    QVERIFY(ready);
}

void DictionaryTest::namedWriteValidation()
{
    NamedWrite row;
    ApiResult error;
    QVERIFY(!parseNamedWrite(QJsonObject(), true, &row, &error));
    QCOMPARE(error.httpStatus, 400);
    QCOMPARE(QString::fromLatin1(error.body.value(QStringLiteral("error")).toString().toLatin1()), QStringLiteral("invalid_request"));

    QJsonObject names;
    names.insert(QStringLiteral("hy"), QStringLiteral("Հայ"));
    names.insert(QStringLiteral("en"), QStringLiteral("En"));
    names.insert(QStringLiteral("ru"), QStringLiteral("Ру"));
    QJsonObject body;
    body.insert(QStringLiteral("code"), QStringLiteral("  "));
    body.insert(QStringLiteral("names"), names);
    body.insert(QStringLiteral("version"), 0);
    QVERIFY(!parseNamedWrite(body, true, &row, &error));
    QCOMPARE(error.body.value(QStringLiteral("error")).toString(), QStringLiteral("code_required"));

    body.insert(QStringLiteral("code"), QString(33, QLatin1Char('A')));
    QVERIFY(!parseNamedWrite(body, true, &row, &error));
    QCOMPARE(error.body.value(QStringLiteral("error")).toString(), QStringLiteral("code_too_long"));

    body.insert(QStringLiteral("code"), QStringLiteral("std"));
    body.remove(QStringLiteral("names"));
    QVERIFY(!parseNamedWrite(body, false, &row, &error));
    QCOMPARE(error.body.value(QStringLiteral("error")).toString(), QStringLiteral("name_required"));

    names.insert(QStringLiteral("en"), QStringLiteral(" "));
    body.insert(QStringLiteral("names"), names);
    QVERIFY(!parseNamedWrite(body, false, &row, &error));
    QCOMPARE(error.body.value(QStringLiteral("error")).toString(), QStringLiteral("name_required"));

    names.insert(QStringLiteral("en"), QString(129, QLatin1Char('n')));
    body.insert(QStringLiteral("names"), names);
    QVERIFY(!parseNamedWrite(body, false, &row, &error));
    QCOMPARE(error.body.value(QStringLiteral("error")).toString(), QStringLiteral("name_too_long"));

    names.insert(QStringLiteral("en"), QStringLiteral("English"));
    body.insert(QStringLiteral("names"), names);
    QVERIFY(parseNamedWrite(body, false, &row, &error));
    QCOMPARE(row.code, QStringLiteral("std"));
    QCOMPARE(row.hy, QStringLiteral("Հայ"));
    QCOMPARE(row.ru, QString::fromUtf8("Ру"));
    body.insert(QStringLiteral("version"), -1);
    QVERIFY(!parseNamedWrite(body, true, &row, &error));
    QCOMPARE(error.body.value(QStringLiteral("error")).toString(), QStringLiteral("invalid_request"));
    body.insert(QStringLiteral("version"), 2);
    QVERIFY(parseNamedWrite(body, true, &row, &error));
    QCOMPARE(row.version, 2);
}

void DictionaryTest::roomWriteValidation()
{
    RoomWrite row;
    ApiResult error;
    QJsonObject body;
    body.insert(QStringLiteral("code"), QStringLiteral("101"));
    body.insert(QStringLiteral("room_type_id"), 4);
    body.insert(QStringLiteral("status_code"), QStringLiteral("dirty"));
    QVERIFY(!parseRoomWrite(body, false, &row, &error));
    QCOMPARE(error.body.value(QStringLiteral("error")).toString(), QStringLiteral("invalid_status"));

    body.insert(QStringLiteral("status_code"), QStringLiteral("vacant_ready"));
    body.insert(QStringLiteral("room_type_id"), 0);
    QVERIFY(!parseRoomWrite(body, false, &row, &error));
    QCOMPARE(error.body.value(QStringLiteral("error")).toString(), QStringLiteral("invalid_request"));

    body.insert(QStringLiteral("room_type_id"), 4);
    body.insert(QStringLiteral("building_id"), QJsonValue::Null);
    body.insert(QStringLiteral("floor"), QJsonValue::Null);
    body.insert(QStringLiteral("phone"), QStringLiteral("  "));
    body.insert(QStringLiteral("do_not_disturb"), true);
    QVERIFY(parseRoomWrite(body, false, &row, &error));
    QCOMPARE(row.roomTypeId, 4);
    QVERIFY(!row.hasBuilding);
    QVERIFY(!row.hasFloor);
    QVERIFY(!row.hasPhone);
    QVERIFY(row.doNotDisturb);
    QCOMPARE(row.statusCode, QStringLiteral("vacant_ready"));

    body.insert(QStringLiteral("floor"), 1.5);
    QVERIFY(!parseRoomWrite(body, false, &row, &error));
    body.insert(QStringLiteral("floor"), 2);
    body.insert(QStringLiteral("building_id"), 9);
    body.insert(QStringLiteral("phone"), QStringLiteral("101"));
    body.insert(QStringLiteral("version"), 3);
    QVERIFY(parseRoomWrite(body, true, &row, &error));
    QVERIFY(row.hasBuilding);
    QCOMPARE(row.buildingId, 9);
    QVERIFY(row.hasFloor);
    QCOMPARE(row.floor, 2);
    QCOMPARE(row.phone, QStringLiteral("101"));
    QCOMPARE(row.version, 3);
    QVERIFY(knownRoomStatus(QStringLiteral("out_of_inventory")));
    QVERIFY(!knownRoomStatus(QStringLiteral("dirty")));
}

void DictionaryTest::roomsRequireBearerWhenDatabaseIsOff()
{
    const QString bin = serverBinary();
    if (!QFileInfo::exists(bin))
        QSKIP("hotel-api binary is not next to the test");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("hotel-api.ini"));
    QFile file(ini);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("listen=127.0.0.1:18084\nmysql_host=\nmysql_schema=\nws_listen=\n");
    file.close();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("HOTEL_DSN"));
    env.remove(QStringLiteral("HOTEL_MYSQL_HOST"));
    env.remove(QStringLiteral("HOTEL_MYSQL_PORT"));
    env.remove(QStringLiteral("HOTEL_MYSQL_SCHEMA"));
    env.remove(QStringLiteral("HOTEL_MYSQL_USER"));
    env.remove(QStringLiteral("HOTEL_MYSQL_PASSWORD"));
    env.remove(QStringLiteral("HOTEL_WS_LISTEN"));
    env.insert(QStringLiteral("HOTEL_CONFIG"), ini);
    env.insert(QStringLiteral("HOTEL_LISTEN"), QStringLiteral("127.0.0.1:18084"));

    QProcess server;
    server.setProcessEnvironment(env);
    server.setProcessChannelMode(QProcess::SeparateChannels);
    server.start(bin, {});
    QVERIFY2(server.waitForStarted(5000), "hotel-api did not start");

    bool health = false;
    for (int i = 0; i < 40 && !health; ++i) {
        health = httpCall(18084, "GET", "/health", {}).status == 200;
        if (!health)
            QTest::qWait(50);
    }
    const HttpReply rooms = httpCall(18084, "GET", "/api/v1/rooms", {});
    const HttpReply typed = httpCall(18084, "GET", "/api/v1/room-types?lang=hy", {});
    const HttpReply statuses = httpCall(18084, "GET", "/api/v1/room-statuses", {});
    const HttpReply posted = httpCall(18084, "POST", "/api/v1/room-types", {}, "{\"code\":\"STD\"}");
    const HttpReply removed = httpCall(18084, "DELETE", "/api/v1/rooms/1", {});
    server.terminate();
    if (!server.waitForFinished(3000)) {
        server.kill();
        server.waitForFinished(2000);
    }
    QVERIFY2(health, qPrintable(QString::fromUtf8(server.readAllStandardError())));
    QCOMPARE(rooms.status, 401);
    QCOMPARE(typed.status, 401);
    QCOMPARE(statuses.status, 401);
    QCOMPARE(posted.status, 401);
    QCOMPARE(removed.status, 401);
    QVERIFY(rooms.body.contains("unauthorized"));
}

void DictionaryTest::databaseNamesFollowLocale()
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
    QVERIFY2(mysql(QStringLiteral("source ") + QStringLiteral(HOTEL_MIGRATION_0003)).isEmpty(), "0003 again");
    QVERIFY2(mysql(QStringLiteral("source ") + QStringLiteral(HOTEL_MIGRATION_0005)).isEmpty(), "0005");
    QVERIFY2(mysql(QStringLiteral("source ") + QStringLiteral(HOTEL_MIGRATION_0005)).isEmpty(), "0005 again");

    qint64 otherTypeId = 0;
    const QString connectionName = QStringLiteral("dict-test");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connectionName);
        db.setHostName(host);
        db.setPort(portText.toInt());
        db.setDatabaseName(schema);
        db.setUserName(user);
        db.setPassword(password);
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral("SET NAMES utf8mb4")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_property (code, name, timezone_name, currency_code, created_at) "
            "VALUES ('DICT', 'Dict', 'UTC', 'AMD', UTC_TIMESTAMP()), "
            "('OTHER', 'Other', 'UTC', 'AMD', UTC_TIMESTAMP())")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_room_type (property_id, code, name) "
            "SELECT id, 'STD', 'Standard' FROM nx_property WHERE code='DICT'")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_room_type (property_id, code, name) "
            "SELECT id, 'BARE', '' FROM nx_property WHERE code='DICT'")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_room_type (property_id, code, name) "
            "SELECT id, 'STD', 'Hidden' FROM nx_property WHERE code='OTHER'")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_building (property_id, code, name) "
            "SELECT id, 'MAIN', 'Main' FROM nx_property WHERE code='DICT'")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_label (owner_table, owner_id, locale, name) "
            "SELECT 'nx_room_type', t.id, 'hy', 'Ստանդարտ' FROM nx_room_type t "
            "JOIN nx_property p ON p.id=t.property_id WHERE p.code='DICT' AND t.code='STD'")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_label (owner_table, owner_id, locale, name) "
            "SELECT 'nx_room_type', t.id, 'ru', 'Стандарт' FROM nx_room_type t "
            "JOIN nx_property p ON p.id=t.property_id WHERE p.code='DICT' AND t.code='STD'")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_label (owner_table, owner_id, locale, name) "
            "SELECT 'nx_building', b.id, 'en', 'Main building' FROM nx_building b "
            "JOIN nx_property p ON p.id=b.property_id WHERE p.code='DICT' AND b.code='MAIN'")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_room (property_id, room_type_id, building_id, code, floor, status_code) "
            "SELECT p.id, t.id, b.id, '101', 1, 'vacant_ready' FROM nx_property p "
            "JOIN nx_room_type t ON t.property_id=p.id AND t.code='STD' "
            "JOIN nx_building b ON b.property_id=p.id AND b.code='MAIN' WHERE p.code='DICT'")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_room (property_id, room_type_id, code, floor, status_code) "
            "SELECT p.id, t.id, '102', NULL, 'occupied' FROM nx_property p "
            "JOIN nx_room_type t ON t.property_id=p.id AND t.code='BARE' WHERE p.code='DICT'")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_room (property_id, room_type_id, code, status_code) "
            "SELECT p.id, t.id, '999', 'vacant_ready' FROM nx_property p "
            "JOIN nx_room_type t ON t.property_id=p.id AND t.code='STD' WHERE p.code='OTHER'")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_permission (code, name) VALUES ('desk', 'Desk')")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_role (property_id, code, name) "
            "SELECT id, 'desk', 'Desk' FROM nx_property WHERE code='DICT'")));
        QVERIFY(QSqlQuery(db).exec(QStringLiteral(
            "INSERT INTO nx_role_permission (role_id, permission_id) "
            "SELECT r.id, p.id FROM nx_role r JOIN nx_permission p ON p.code='desk' WHERE r.code='desk'")));
        const QByteArray hash = QCryptographicHash::hash(QByteArrayLiteral("test-pass"), QCryptographicHash::Md5).toHex();
        QSqlQuery userInsert(db);
        QVERIFY(userInsert.prepare(QStringLiteral(
            "INSERT INTO nx_user (property_id, role_id, login, first_name, last_name, password_hash, "
            "password_scheme, state, created_at) "
            "SELECT p.id, r.id, 'dict', 'Dict', 'User', :hash, 'md5', 'active', UTC_TIMESTAMP() "
            "FROM nx_property p JOIN nx_role r ON r.property_id=p.id AND r.code='desk' WHERE p.code='DICT'")));
        userInsert.bindValue(QStringLiteral(":hash"), QString::fromLatin1(hash));
        QVERIFY(userInsert.exec());
        QSqlQuery reader(db);
        QVERIFY(reader.prepare(QStringLiteral(
            "INSERT INTO nx_user (property_id, role_id, login, first_name, last_name, password_hash, "
            "password_scheme, state, created_at) "
            "SELECT id, NULL, 'dict-read', 'Read', 'Only', :hash, 'md5', 'active', UTC_TIMESTAMP() "
            "FROM nx_property WHERE code='DICT'")));
        reader.bindValue(QStringLiteral(":hash"), QString::fromLatin1(hash));
        QVERIFY(reader.exec());
        QSqlQuery otherType(db);
        QVERIFY(otherType.exec(QStringLiteral(
            "SELECT t.id FROM nx_room_type t JOIN nx_property p ON p.id = t.property_id "
            "WHERE p.code = 'OTHER' AND t.code = 'STD'")));
        QVERIFY(otherType.next());
        otherTypeId = otherType.value(0).toLongLong();
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("hotel-api.ini"));
    QFile iniFile(ini);
    QVERIFY(iniFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    iniFile.write("listen=127.0.0.1:18085\nws_listen=\n");
    iniFile.close();

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
        const HttpReply health = httpCall(18085, "GET", "/health", {});
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

    const HttpReply loggedIn = httpCall(18085,
                                        "POST",
                                        "/api/v1/sessions",
                                        {},
                                        "{\"login\":\"dict\",\"password\":\"test-pass\"}");
    QCOMPARE(loggedIn.status, 200);
    const QJsonObject session = QJsonDocument::fromJson(loggedIn.body).object();
    const QByteArray token = session.value(QStringLiteral("token")).toString().toLatin1();
    QVERIFY(token.size() == 64);
    const QByteArray bearer = "Bearer " + token;

    const auto items = [](const HttpReply &reply) {
        return QJsonDocument::fromJson(reply.body).object().value(QStringLiteral("items")).toArray();
    };
    const HttpReply hy = httpCall(18085, "GET", "/api/v1/rooms?lang=hy", bearer);
    QCOMPARE(hy.status, 200);
    const QJsonArray hyItems = items(hy);
    QCOMPARE(hyItems.size(), 2);
    QCOMPARE(hyItems.at(0).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("101"));
    QCOMPARE(hyItems.at(0).toObject().value(QStringLiteral("room_type")).toObject().value(QStringLiteral("name")).toString(),
             QString::fromUtf8("Ստանդարտ"));
    QCOMPARE(hyItems.at(0).toObject().value(QStringLiteral("building")).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("Main"));
    QCOMPARE(hyItems.at(1).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("102"));
    QCOMPARE(hyItems.at(1).toObject().value(QStringLiteral("room_type")).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("BARE"));
    QVERIFY(hyItems.at(1).toObject().value(QStringLiteral("floor")).isNull());
    QVERIFY(hyItems.at(1).toObject().value(QStringLiteral("building")).isNull());

    const HttpReply ru = httpCall(18085, "GET", "/api/v1/room-types", bearer, {}, "Accept-Language: fr, ru;q=0.8\r\n");
    QCOMPARE(ru.status, 200);
    QCOMPARE(QJsonDocument::fromJson(ru.body).object().value(QStringLiteral("lang")).toString(), QStringLiteral("ru"));
    const QJsonArray types = items(ru);
    QCOMPARE(types.size(), 2);
    QString stdName;
    for (const QJsonValue &type : types) {
        if (type.toObject().value(QStringLiteral("code")).toString() == QLatin1String("STD"))
            stdName = type.toObject().value(QStringLiteral("name")).toString();
    }
    QCOMPARE(stdName, QString::fromUtf8("Стандарт"));

    const HttpReply statuses = httpCall(18085, "GET", "/api/v1/room-statuses?lang=en", bearer);
    QCOMPARE(statuses.status, 200);
    QVERIFY(statuses.body.contains("vacant_ready"));
    QVERIFY(!statuses.body.contains("\"name\""));

    QVERIFY(types.at(0).toObject().contains(QStringLiteral("version")));
    QVERIFY(types.at(0).toObject().contains(QStringLiteral("names")));
    QJsonObject stdType;
    for (const QJsonValue &type : types) {
        if (type.toObject().value(QStringLiteral("code")).toString() == QLatin1String("STD"))
            stdType = type.toObject();
    }
    QCOMPARE(stdType.value(QStringLiteral("names")).toObject().value(QStringLiteral("ru")).toString(),
             QString::fromUtf8("Стандарт"));
    QCOMPARE(stdType.value(QStringLiteral("names")).toObject().value(QStringLiteral("hy")).toString(),
             QString::fromUtf8("Ստանդարտ"));
    QVERIFY(stdType.value(QStringLiteral("names")).toObject().value(QStringLiteral("en")).isNull());
    QCOMPARE(hyItems.at(0).toObject().value(QStringLiteral("version")).toInt(), 0);
    QCOMPARE(hyItems.at(0).toObject().value(QStringLiteral("do_not_disturb")).toBool(), false);

    const auto postJson = [&](const QByteArray &path, const QByteArray &json, const QByteArray &auth) {
        return httpCall(18085, "POST", path, auth, json);
    };
    const QByteArray missingNames = "{\"code\":\"SUITE\"}";
    const HttpReply missing = postJson("/api/v1/room-types", missingNames, bearer);
    QCOMPARE(missing.status, 400);
    QVERIFY(missing.body.contains("name_required"));

    const QByteArray suite =
        "{\"code\":\"SUITE\",\"names\":{\"hy\":\"Սյուիտ\",\"en\":\"Suite\",\"ru\":\"Сюит\"}}";
    const HttpReply created = postJson("/api/v1/room-types", suite, bearer);
    QCOMPARE(created.status, 201);
    const QJsonObject createdBody = QJsonDocument::fromJson(created.body).object();
    const qint64 suiteId = createdBody.value(QStringLiteral("id")).toInteger();
    QVERIFY(suiteId > 0);
    QCOMPARE(createdBody.value(QStringLiteral("version")).toInt(), 0);
    QCOMPARE(createdBody.value(QStringLiteral("name")).toString(), QString::fromUtf8("Сюит"));

    const HttpReply duplicate = postJson("/api/v1/room-types", suite, bearer);
    QCOMPARE(duplicate.status, 409);
    QVERIFY(duplicate.body.contains("duplicate_code"));

    const QByteArray stale =
        QByteArray("{\"version\":1,\"code\":\"SUITE\",\"names\":{\"hy\":\"Սյուիտ\",\"en\":\"Suite\",\"ru\":\"Сюит\"}}");
    const HttpReply conflict = httpCall(18085, "PATCH", "/api/v1/room-types/" + QByteArray::number(suiteId), bearer, stale);
    QCOMPARE(conflict.status, 409);
    QVERIFY(conflict.body.contains("version_conflict"));

    const QByteArray renamed =
        QByteArray("{\"version\":0,\"code\":\"SU\",\"names\":{\"hy\":\"Սյուիտ\",\"en\":\"Suite\",\"ru\":\"Люкс\"}}");
    const HttpReply patched = httpCall(18085, "PATCH", "/api/v1/room-types/" + QByteArray::number(suiteId), bearer, renamed);
    QCOMPARE(patched.status, 200);
    QCOMPARE(QJsonDocument::fromJson(patched.body).object().value(QStringLiteral("version")).toInt(), 1);
    QCOMPARE(QJsonDocument::fromJson(patched.body).object().value(QStringLiteral("code")).toString(), QStringLiteral("SU"));

    const QByteArray foreignType = QByteArray("{\"version\":0,\"code\":\"H\",\"names\":{\"hy\":\"Հ\",\"en\":\"H\",\"ru\":\"Ч\"}}");
    const HttpReply hidden = httpCall(18085, "PATCH", "/api/v1/room-types/" + QByteArray::number(otherTypeId), bearer, foreignType);
    QCOMPARE(hidden.status, 404);
    QVERIFY(hidden.body.contains("room_type_not_found"));

    const QByteArray badRoom =
        QByteArray("{\"code\":\"301\",\"room_type_id\":999999,\"status_code\":\"vacant_ready\"}");
    const HttpReply badRoomReply = postJson("/api/v1/rooms", badRoom, bearer);
    QCOMPARE(badRoomReply.status, 404);
    QVERIFY(badRoomReply.body.contains("room_type_not_found"));

    const QByteArray roomJson = QByteArray("{\"code\":\"301\",\"room_type_id\":") + QByteArray::number(suiteId)
        + ",\"floor\":3,\"phone\":\"301\",\"status_code\":\"vacant_ready\",\"do_not_disturb\":false}";
    const HttpReply roomCreated = postJson("/api/v1/rooms", roomJson, bearer);
    QCOMPARE(roomCreated.status, 201);
    const qint64 roomId = QJsonDocument::fromJson(roomCreated.body).object().value(QStringLiteral("id")).toInteger();
    QVERIFY(roomId > 0);
    QCOMPARE(QJsonDocument::fromJson(roomCreated.body).object().value(QStringLiteral("version")).toInt(), 0);

    const HttpReply typeInUse = httpCall(18085, "DELETE", "/api/v1/room-types/" + QByteArray::number(suiteId), bearer);
    QCOMPARE(typeInUse.status, 409);
    QVERIFY(typeInUse.body.contains("in_use"));

    const HttpReply roomGone = httpCall(18085, "DELETE", "/api/v1/rooms/" + QByteArray::number(roomId), bearer);
    QCOMPARE(roomGone.status, 200);
    QVERIFY(roomGone.body.contains("deleted"));
    const HttpReply typeGone = httpCall(18085, "DELETE", "/api/v1/room-types/" + QByteArray::number(suiteId), bearer);
    QCOMPARE(typeGone.status, 200);

    const HttpReply readerLogin = httpCall(18085, "POST", "/api/v1/sessions", {}, "{\"login\":\"dict-read\",\"password\":\"test-pass\"}");
    QCOMPARE(readerLogin.status, 200);
    const QByteArray readerToken = QJsonDocument::fromJson(readerLogin.body).object().value(QStringLiteral("token")).toString().toLatin1();
    const QByteArray readerBearer = "Bearer " + readerToken;
    QVERIFY(!QJsonDocument::fromJson(readerLogin.body).object().value(QStringLiteral("commands_allowed")).toBool());
    const HttpReply forbidden = postJson("/api/v1/buildings", suite, readerBearer);
    QCOMPARE(forbidden.status, 403);
    QVERIFY(forbidden.body.contains("commands_not_allowed"));

    server.terminate();
    server.waitForFinished(3000);
    QVERIFY(!QString::fromUtf8(server.readAllStandardError()).contains(password));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    DictionaryTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_dictionaries.moc"
