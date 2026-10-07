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
    server.terminate();
    if (!server.waitForFinished(3000)) {
        server.kill();
        server.waitForFinished(2000);
    }
    QVERIFY2(health, qPrintable(QString::fromUtf8(server.readAllStandardError())));
    QCOMPARE(rooms.status, 401);
    QCOMPARE(typed.status, 401);
    QCOMPARE(statuses.status, 401);
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
