#include "config.h"
#include "dberror.h"
#include "iniparse.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {

class EnvRestore {
public:
    EnvRestore()
    {
        const char *names[] = {"HOTEL_CONFIG",
                               "HOTEL_LISTEN",
                               "HOTEL_DSN",
                               "HOTEL_WS_LISTEN",
                               "HOTEL_MYSQL_HOST",
                               "HOTEL_MYSQL_PORT",
                               "HOTEL_MYSQL_SCHEMA",
                               "HOTEL_MYSQL_USER",
                               "HOTEL_MYSQL_PASSWORD",
                               "HOTEL_MYSQL_SSL",
                               "HOTEL_MYSQL_SSL_CA",
                               nullptr};
        for (int i = 0; names[i] != nullptr; ++i) {
            keys.append(QByteArray(names[i]));
            saved.append(qgetenv(names[i]));
            present.append(!qEnvironmentVariableIsEmpty(names[i]));
            qunsetenv(names[i]);
        }
    }

    ~EnvRestore()
    {
        for (int i = 0; i < keys.size(); ++i) {
            if (present.at(i))
                qputenv(keys.at(i), saved.at(i));
            else
                qunsetenv(keys.at(i).constData());
        }
    }

    QList<QByteArray> keys;
    QList<QByteArray> saved;
    QList<bool> present;
};

bool writeIni(const QString &path, const QByteArray &body)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(body) == body.size();
}

} // namespace

class TestDatabaseConfig : public QObject
{
    Q_OBJECT

private slots:
    void literalPasswordIsNotDecoded();
    void portDefaultsAndRejects();
    void missingUserIsAnError();
    void missingHostIsAnError();
    void legacyDsnWhenNoMysqlKeys();
    void mysqlKeysWinOverDsn();
    void startupLineOmitsPassword();
    void envOverridesIni();
    void emptyEnvDoesNotClearIni();
    void envDsnFallback();
    void envDsnLosesToMysqlKeys();
    void sslDefaultsToPreferred();
    void sslEnvOverridesIni();
    void sslRejectsUnknownMode();
    void sslCaRejectsSemicolon();
    void legacyDsnKeepsSslMode();
};

void TestDatabaseConfig::literalPasswordIsNotDecoded()
{
    const QByteArray data =
        "listen = 127.0.0.1:8080\n"
        "mysql_host = 127.0.0.1\n"
        "mysql_schema = hotelnext\n"
        "mysql_user = root\n"
        "mysql_password = p@ss:w%rd#;x\n"
        "ws_listen = 8081\n";
    HotelIniValues values;
    QString error;
    QVERIFY2(parseHotelIni(data, &values, &error), qPrintable(error));
    QCOMPARE(values.mysqlUser, QStringLiteral("root"));
    QCOMPARE(values.mysqlPassword, QStringLiteral("p@ss:w%rd#;x"));

    DatabaseResolveInput input;
    input.mysqlHost = values.mysqlHost;
    input.mysqlSchema = values.mysqlSchema;
    input.mysqlUser = values.mysqlUser;
    input.mysqlPassword = values.mysqlPassword;
    const DatabaseResolveResult resolved = resolveDatabaseTarget(input);
    QVERIFY(resolved.ok);
    QCOMPARE(resolved.notice, DatabaseConfigNotice::None);
    QVERIFY(resolved.target.configured);
    QCOMPARE(resolved.target.host, QStringLiteral("127.0.0.1"));
    QCOMPARE(resolved.target.port, 3306);
    QCOMPARE(resolved.target.database, QStringLiteral("hotelnext"));
    QCOMPARE(resolved.target.user, QStringLiteral("root"));
    QCOMPARE(resolved.target.password, QStringLiteral("p@ss:w%rd#;x"));
}

void TestDatabaseConfig::portDefaultsAndRejects()
{
    DatabaseResolveInput input;
    input.mysqlHost = QStringLiteral("127.0.0.1");
    input.mysqlSchema = QStringLiteral("hotelnext");
    input.mysqlUser = QStringLiteral("root");
    input.mysqlPort = QStringLiteral("3307");
    DatabaseResolveResult resolved = resolveDatabaseTarget(input);
    QVERIFY(resolved.ok);
    QCOMPARE(resolved.target.port, 3307);

    input.mysqlPort = QStringLiteral("0");
    resolved = resolveDatabaseTarget(input);
    QVERIFY(!resolved.ok);
    QCOMPARE(resolved.error, QStringLiteral("mysql_port has an invalid port"));

    input.mysqlPort = QStringLiteral("nope");
    resolved = resolveDatabaseTarget(input);
    QVERIFY(!resolved.ok);
    QVERIFY(!resolved.error.contains(QStringLiteral("nope")));
}

void TestDatabaseConfig::missingUserIsAnError()
{
    DatabaseResolveInput input;
    input.mysqlHost = QStringLiteral("127.0.0.1");
    input.mysqlSchema = QStringLiteral("hotelnext");
    const DatabaseResolveResult resolved = resolveDatabaseTarget(input);
    QVERIFY(!resolved.ok);
    QCOMPARE(resolved.error, QStringLiteral("mysql_user is required"));
    QVERIFY(!resolved.target.configured);
}

void TestDatabaseConfig::missingHostIsAnError()
{
    DatabaseResolveInput input;
    input.mysqlSchema = QStringLiteral("hotelnext");
    input.mysqlUser = QStringLiteral("root");
    const DatabaseResolveResult resolved = resolveDatabaseTarget(input);
    QVERIFY(!resolved.ok);
    QCOMPARE(resolved.error, QStringLiteral("mysql_host is required"));
}

void TestDatabaseConfig::legacyDsnWhenNoMysqlKeys()
{
    DatabaseResolveInput input;
    input.dsn = QStringLiteral("mysql://hotel_api:p%40ss%3Aword@127.0.0.1:3306/resort");
    const DatabaseResolveResult resolved = resolveDatabaseTarget(input);
    QVERIFY(resolved.ok);
    QCOMPARE(resolved.notice, DatabaseConfigNotice::DeprecatedDsn);
    QVERIFY(resolved.target.configured);
    QCOMPARE(resolved.target.host, QStringLiteral("127.0.0.1"));
    QCOMPARE(resolved.target.port, 3306);
    QCOMPARE(resolved.target.database, QStringLiteral("resort"));
    QCOMPARE(resolved.target.user, QStringLiteral("hotel_api"));
    QCOMPARE(resolved.target.password, QStringLiteral("p@ss:word"));
}

void TestDatabaseConfig::mysqlKeysWinOverDsn()
{
    DatabaseResolveInput input;
    input.mysqlHost = QStringLiteral("127.0.0.1");
    input.mysqlSchema = QStringLiteral("hotelnext");
    input.mysqlUser = QStringLiteral("root");
    input.mysqlPassword = QStringLiteral("p@ss:w%rd#;x");
    input.dsn = QStringLiteral("mysql://legacy:legacypass@10.0.0.2:3306/olddb");
    const DatabaseResolveResult resolved = resolveDatabaseTarget(input);
    QVERIFY(resolved.ok);
    QCOMPARE(resolved.notice, DatabaseConfigNotice::MysqlOverridesDsn);
    QCOMPARE(resolved.target.host, QStringLiteral("127.0.0.1"));
    QCOMPARE(resolved.target.database, QStringLiteral("hotelnext"));
    QCOMPARE(resolved.target.user, QStringLiteral("root"));
    QCOMPARE(resolved.target.password, QStringLiteral("p@ss:w%rd#;x"));
    QVERIFY(!databaseStartupDetail(resolved.target).contains(QStringLiteral("legacypass")));
    QVERIFY(!databaseStartupDetail(resolved.target).contains(QStringLiteral("p@ss")));
}

void TestDatabaseConfig::startupLineOmitsPassword()
{
    DatabaseTarget target;
    target.configured = true;
    target.host = QStringLiteral("127.0.0.1");
    target.port = 3306;
    target.database = QStringLiteral("hotelnext");
    target.user = QStringLiteral("root");
    target.password = QStringLiteral("p@ss:w%rd#;x");
    QCOMPARE(databaseStartupDetail(target), QStringLiteral("127.0.0.1:3306/hotelnext user=root"));
    QVERIFY(!databaseStartupDetail(target).contains(QStringLiteral("p@ss")));

    DatabaseTarget empty;
    QCOMPARE(databaseStartupDetail(empty), QStringLiteral("not configured"));
}

void TestDatabaseConfig::envOverridesIni()
{
    EnvRestore env;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("hotel-api.ini"));
    QVERIFY(writeIni(path,
                     "listen=127.0.0.1:8080\n"
                     "mysql_host=10.1.1.1\n"
                     "mysql_port=3306\n"
                     "mysql_schema=fromini\n"
                     "mysql_user=iniuser\n"
                     "mysql_password=inipass\n"
                     "dsn=mysql://legacy:legacypass@10.0.0.2:3306/olddb\n"));
    qputenv("HOTEL_CONFIG", path.toUtf8());
    qputenv("HOTEL_MYSQL_HOST", "127.0.0.1");
    qputenv("HOTEL_MYSQL_PORT", "3307");
    qputenv("HOTEL_MYSQL_SCHEMA", "hotelnext");
    qputenv("HOTEL_MYSQL_USER", "root");
    qputenv("HOTEL_MYSQL_PASSWORD", "p@ss:w%rd#;x");

    const ConfigLoadResult loaded = loadConfig();

    QVERIFY2(loaded.ok, qPrintable(loaded.error));
    QCOMPARE(loaded.config.database.host, QStringLiteral("127.0.0.1"));
    QCOMPARE(loaded.config.database.port, 3307);
    QCOMPARE(loaded.config.database.database, QStringLiteral("hotelnext"));
    QCOMPARE(loaded.config.database.user, QStringLiteral("root"));
    QCOMPARE(loaded.config.database.password, QStringLiteral("p@ss:w%rd#;x"));
    QCOMPARE(loaded.databaseNotice, DatabaseConfigNotice::MysqlOverridesDsn);
    QCOMPARE(databaseConfigNoticeLine(loaded.databaseNotice),
             QStringLiteral("hotel-api database: mysql_* overrides dsn"));
    QVERIFY(!databaseStartupDetail(loaded.config.database).contains(QStringLiteral("p@ss")));
    QVERIFY(!databaseStartupDetail(loaded.config.database).contains(QStringLiteral("legacypass")));
}

void TestDatabaseConfig::emptyEnvDoesNotClearIni()
{
    EnvRestore env;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("hotel-api.ini"));
    QVERIFY(writeIni(path,
                     "mysql_host=127.0.0.1\n"
                     "mysql_schema=hotelnext\n"
                     "mysql_user = root\n"
                     "mysql_password=\" p@ss:w%rd#;x \"\n"));
    qputenv("HOTEL_CONFIG", path.toUtf8());
    qputenv("HOTEL_MYSQL_HOST", "");
    qputenv("HOTEL_MYSQL_PASSWORD", "");
    qputenv("HOTEL_DSN", "");

    const ConfigLoadResult loaded = loadConfig();

    QVERIFY2(loaded.ok, qPrintable(loaded.error));
    QCOMPARE(loaded.config.database.host, QStringLiteral("127.0.0.1"));
    QCOMPARE(loaded.config.database.user, QStringLiteral("root"));
    QCOMPARE(loaded.config.database.password, QStringLiteral(" p@ss:w%rd#;x "));
    QCOMPARE(loaded.config.database.port, 3306);
    QCOMPARE(loaded.databaseNotice, DatabaseConfigNotice::None);
    QVERIFY(databaseConfigNoticeLine(loaded.databaseNotice).isEmpty());
}

void TestDatabaseConfig::envDsnFallback()
{
    EnvRestore env;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("hotel-api.ini"));
    QVERIFY(writeIni(path, "listen=127.0.0.1:8080\ndsn=mysql://fileuser:filepass@10.0.0.2:3306/filedb\n"));
    qputenv("HOTEL_CONFIG", path.toUtf8());
    qputenv("HOTEL_DSN", "mysql://hotel_api:p%40ss%3Aword@127.0.0.1:3306/resort");

    const ConfigLoadResult loaded = loadConfig();

    QVERIFY2(loaded.ok, qPrintable(loaded.error));
    QCOMPARE(loaded.config.database.host, QStringLiteral("127.0.0.1"));
    QCOMPARE(loaded.config.database.database, QStringLiteral("resort"));
    QCOMPARE(loaded.config.database.user, QStringLiteral("hotel_api"));
    QCOMPARE(loaded.config.database.password, QStringLiteral("p@ss:word"));
    QCOMPARE(loaded.databaseNotice, DatabaseConfigNotice::DeprecatedDsn);
    QCOMPARE(databaseConfigNoticeLine(loaded.databaseNotice),
             QStringLiteral(
                 "hotel-api database: dsn is deprecated; use mysql_host, mysql_port, mysql_schema, mysql_user, mysql_password"));
}

void TestDatabaseConfig::envDsnLosesToMysqlKeys()
{
    EnvRestore env;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("hotel-api.ini"));
    QVERIFY(writeIni(path,
                     "mysql_host=10.1.1.1\n"
                     "mysql_schema=fromini\n"
                     "mysql_user=iniuser\n"
                     "mysql_password=inipass\n"));
    qputenv("HOTEL_CONFIG", path.toUtf8());
    qputenv("HOTEL_DSN", "mysql://legacy:legacypass@127.0.0.1:3306/olddb");

    const ConfigLoadResult loaded = loadConfig();

    QVERIFY2(loaded.ok, qPrintable(loaded.error));
    QCOMPARE(loaded.config.database.host, QStringLiteral("10.1.1.1"));
    QCOMPARE(loaded.config.database.database, QStringLiteral("fromini"));
    QCOMPARE(loaded.config.database.user, QStringLiteral("iniuser"));
    QCOMPARE(loaded.config.database.password, QStringLiteral("inipass"));
    QCOMPARE(loaded.databaseNotice, DatabaseConfigNotice::MysqlOverridesDsn);
    QCOMPARE(databaseConfigNoticeLine(loaded.databaseNotice),
             QStringLiteral("hotel-api database: mysql_* overrides dsn"));
    QVERIFY(!databaseStartupDetail(loaded.config.database).contains(QStringLiteral("legacypass")));
}

void TestDatabaseConfig::sslDefaultsToPreferred()
{
    DatabaseResolveInput input;
    input.mysqlHost = QStringLiteral("127.0.0.1");
    input.mysqlSchema = QStringLiteral("hotelnext");
    input.mysqlUser = QStringLiteral("root");
    const DatabaseResolveResult resolved = resolveDatabaseTarget(input);
    QVERIFY(resolved.ok);
    QCOMPARE(resolved.target.sslMode, QStringLiteral("preferred"));
    QCOMPARE(mysqlConnectOptions(3, resolved.target.sslMode, resolved.target.sslCa),
             QStringLiteral(
                 "MYSQL_OPT_CONNECT_TIMEOUT=3;MYSQL_OPT_READ_TIMEOUT=3;MYSQL_OPT_WRITE_TIMEOUT=3;"
                 "MYSQL_OPT_SSL_MODE=PREFERRED;MYSQL_OPT_SSL_VERIFY_SERVER_CERT=0"));
}

void TestDatabaseConfig::sslEnvOverridesIni()
{
    EnvRestore env;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("hotel-api.ini"));
    QVERIFY(writeIni(path,
                     "mysql_host=127.0.0.1\n"
                     "mysql_schema=hotelnext\n"
                     "mysql_user = root\n"
                     "mysql_ssl = preferred\n"
                     "mysql_ssl_ca=\"C:/certs/ca.pem\"\n"));
    qputenv("HOTEL_CONFIG", path.toUtf8());
    qputenv("HOTEL_MYSQL_SSL", "verify");
    qputenv("HOTEL_MYSQL_SSL_CA", "/etc/hotel-api/ca.pem");

    const ConfigLoadResult loaded = loadConfig();
    QVERIFY2(loaded.ok, qPrintable(loaded.error));
    QCOMPARE(loaded.config.database.sslMode, QStringLiteral("verify"));
    QCOMPARE(loaded.config.database.sslCa, QStringLiteral("/etc/hotel-api/ca.pem"));
    QVERIFY(mysqlConnectOptions(3, loaded.config.database.sslMode, loaded.config.database.sslCa)
                .contains(QStringLiteral("MYSQL_OPT_SSL_MODE=VERIFY_CA")));
    QVERIFY(mysqlConnectOptions(3, loaded.config.database.sslMode, loaded.config.database.sslCa)
                .contains(QStringLiteral("MYSQL_OPT_SSL_VERIFY_SERVER_CERT=1")));
    QVERIFY(mysqlConnectOptions(3, loaded.config.database.sslMode, loaded.config.database.sslCa)
                .endsWith(QStringLiteral("MYSQL_OPT_SSL_CA=/etc/hotel-api/ca.pem")));
}

void TestDatabaseConfig::sslRejectsUnknownMode()
{
    DatabaseResolveInput input;
    input.mysqlHost = QStringLiteral("127.0.0.1");
    input.mysqlSchema = QStringLiteral("hotelnext");
    input.mysqlUser = QStringLiteral("root");
    input.mysqlSsl = QStringLiteral("maybe");
    const DatabaseResolveResult resolved = resolveDatabaseTarget(input);
    QVERIFY(!resolved.ok);
    QCOMPARE(resolved.error, QStringLiteral("mysql_ssl must be off, preferred, required, or verify"));
    QVERIFY(!resolved.target.configured);
}

void TestDatabaseConfig::sslCaRejectsSemicolon()
{
    DatabaseResolveInput input;
    input.mysqlHost = QStringLiteral("127.0.0.1");
    input.mysqlSchema = QStringLiteral("hotelnext");
    input.mysqlUser = QStringLiteral("root");
    input.mysqlSsl = QStringLiteral("verify");
    input.mysqlSslCa = QStringLiteral("C:/a;b.pem");
    const DatabaseResolveResult resolved = resolveDatabaseTarget(input);
    QVERIFY(!resolved.ok);
    QCOMPARE(resolved.error, QStringLiteral("mysql_ssl_ca must not contain ';'"));
}

void TestDatabaseConfig::legacyDsnKeepsSslMode()
{
    DatabaseResolveInput input;
    input.dsn = QStringLiteral("mysql://hotel_api:p%40ss%3Aword@127.0.0.1:3306/resort");
    input.mysqlSsl = QStringLiteral("off");
    const DatabaseResolveResult resolved = resolveDatabaseTarget(input);
    QVERIFY(resolved.ok);
    QCOMPARE(resolved.notice, DatabaseConfigNotice::DeprecatedDsn);
    QCOMPARE(resolved.target.user, QStringLiteral("hotel_api"));
    QCOMPARE(resolved.target.sslMode, QStringLiteral("off"));
    QVERIFY(mysqlConnectOptions(3, resolved.target.sslMode, resolved.target.sslCa)
                .contains(QStringLiteral("MYSQL_OPT_SSL_MODE=DISABLED")));
    QVERIFY(mysqlConnectOptions(3, resolved.target.sslMode, resolved.target.sslCa)
                .contains(QStringLiteral("MYSQL_OPT_SSL_VERIFY_SERVER_CERT=0")));
}

int runDatabaseConfigTests(int argc, char **argv)
{
    TestDatabaseConfig tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_config.moc"
