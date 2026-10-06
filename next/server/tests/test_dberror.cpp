#include "dberror.h"
#include "mysqlclient.h"

#include <QFile>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QTest>

namespace {

void write16(QByteArray *file, int off, quint16 value)
{
    (*file)[off] = char(value & 0xff);
    (*file)[off + 1] = char((value >> 8) & 0xff);
}

void write32(QByteArray *file, int off, quint32 value)
{
    (*file)[off] = char(value & 0xff);
    (*file)[off + 1] = char((value >> 8) & 0xff);
    (*file)[off + 2] = char((value >> 16) & 0xff);
    (*file)[off + 3] = char((value >> 24) & 0xff);
}

void write64(QByteArray *file, int off, quint64 value)
{
    write32(file, off, quint32(value));
    write32(file, off + 4, quint32(value >> 32));
}

QByteArray elf64WithNeeded(const char *needed)
{
    const QByteArray name = QByteArray(needed) + '\0';
    const QByteArray strtab = QByteArray(1, '\0') + name;
    const int phoff = 64;
    const int phentsize = 56;
    const int dynOff = phoff + 2 * phentsize;
    const int strOff = dynOff + 16 * 3;
    QByteArray file(strOff + strtab.size(), '\0');
    file[0] = 0x7f;
    file[1] = 'E';
    file[2] = 'L';
    file[3] = 'F';
    file[4] = 2;
    file[5] = 1;
    file[6] = 1;
    write16(&file, 16, 3);
    write16(&file, 18, 62);
    write32(&file, 20, 1);
    write64(&file, 32, phoff);
    write16(&file, 52, 64);
    write16(&file, 54, phentsize);
    write16(&file, 56, 2);

    const auto phdr = [&](int index, quint32 type, quint64 offset, quint64 filesz) {
        const int o = phoff + index * phentsize;
        write32(&file, o, type);
        write64(&file, o + 8, offset);
        write64(&file, o + 16, offset);
        write64(&file, o + 32, filesz);
        write64(&file, o + 40, filesz);
    };
    phdr(0, 1, 0, quint64(file.size()));
    phdr(1, 2, dynOff, 48);
    const auto dyn = [&](int index, quint64 tag, quint64 val) {
        const int o = dynOff + index * 16;
        write64(&file, o, tag);
        write64(&file, o + 8, val);
    };
    dyn(0, 5, strOff);
    dyn(1, 1, 1);
    dyn(2, 0, 0);
    file.replace(strOff, strtab.size(), strtab);
    return file;
}

QByteArray pe64WithImport(const char *dll)
{
    const QByteArray dllName = QByteArray(dll) + '\0';
    const int lfanew = 64;
    const int optSize = 240;
    const int sec = lfanew + 4 + 20 + optSize;
    const int raw = sec + 40;
    const int nameOff = raw + 40;
    QByteArray file(nameOff + dllName.size(), '\0');
    write16(&file, 0, 0x5A4D);
    write32(&file, 0x3C, lfanew);
    write32(&file, lfanew, 0x00004550);
    write16(&file, lfanew + 6, 1);
    write16(&file, lfanew + 20, optSize);
    write16(&file, lfanew + 24, 0x20b);
    write32(&file, lfanew + 24 + 108, 16);
    write32(&file, lfanew + 24 + 112 + 8, raw);
    write32(&file, lfanew + 24 + 112 + 12, 40);
    write32(&file, sec + 8, quint32(file.size() - raw));
    write32(&file, sec + 12, raw);
    write32(&file, sec + 16, quint32(file.size() - raw));
    write32(&file, sec + 20, raw);
    write32(&file, raw + 12, nameOff);
    file.replace(nameOff, dllName.size(), dllName);
    return file;
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size();
}

} // namespace

class TestDbError : public QObject
{
    Q_OBJECT

private slots:
    void mapsNativeCodes();
    void probeRejectsRawText();
    void scrubsPasswordAndEncoding();
    void logKeepsUserAndDropsPassword();
    void connectOptions();
    void clientInfoKind();
    void pluginImageNamesClientLibrary();
    void loadedPluginClientKind();
};

void TestDbError::mapsNativeCodes()
{
    QCOMPARE(publicDatabaseErrorCode(QStringLiteral("1045")), QStringLiteral("access_denied"));
    QCOMPARE(publicDatabaseErrorCode(QStringLiteral("1049")), QStringLiteral("unknown_database"));
    QCOMPARE(publicDatabaseErrorCode(QStringLiteral("2002")), QStringLiteral("cannot_connect"));
    QCOMPARE(publicDatabaseErrorCode(QStringLiteral("2003")), QStringLiteral("cannot_connect"));
    QCOMPARE(publicDatabaseErrorCode(QStringLiteral("2026")), QStringLiteral("tls_error"));
    QCOMPARE(publicDatabaseErrorCode(QStringLiteral("2013")), QStringLiteral("connection_failed"));
    QCOMPARE(publicDatabaseErrorCode(QStringLiteral("1146")), QStringLiteral("connection_failed"));
    QCOMPARE(publicDatabaseErrorCode(QString()), QStringLiteral("connection_failed"));
    QCOMPARE(publicDatabaseErrorCode(QStringLiteral("10450")), QStringLiteral("connection_failed"));
}

void TestDbError::probeRejectsRawText()
{
    QCOMPARE(probeDatabaseError(QStringLiteral("access_denied")), QStringLiteral("access_denied"));
    QCOMPARE(probeDatabaseError(QStringLiteral("driver_not_loaded")), QStringLiteral("driver_not_loaded"));
    QCOMPARE(probeDatabaseError(QStringLiteral("tls_error")), QStringLiteral("tls_error"));
    QCOMPARE(probeDatabaseError(QStringLiteral("Access denied for user 'hotel_api'")),
             QStringLiteral("connection_failed"));
    QCOMPARE(probeDatabaseError(QStringLiteral("s3cret")), QStringLiteral("connection_failed"));
}

void TestDbError::scrubsPasswordAndEncoding()
{
    const QString password = QStringLiteral("p@ss:word");
    const QString raw = QStringLiteral("driver said p@ss:word and also p%40ss%3Aword\r\nnext");
    const QString scrubbed = scrubDatabaseMessage(raw, password);
    QVERIFY(!scrubbed.contains(password));
    QVERIFY(!scrubbed.contains(QStringLiteral("p%40ss%3Aword")));
    QVERIFY(scrubbed.contains(QStringLiteral("***")));
    QVERIFY(!scrubbed.contains(QLatin1Char('\n')));
    QVERIFY(!scrubbed.contains(QLatin1Char('\r')));

    const QString untouched = scrubDatabaseMessage(QStringLiteral("using password: YES"), QString());
    QCOMPARE(untouched, QStringLiteral("using password: YES"));
}

void TestDbError::logKeepsUserAndDropsPassword()
{
    const QString password = QStringLiteral("hotel");
    const QString driver = scrubDatabaseMessage(
        QStringLiteral("Access denied for user 'hotel_api'@'127.0.0.1' (using password: YES) hotel"),
        password);
    const QString server = scrubDatabaseMessage(QStringLiteral("Access denied hotel"), password);
    const QString line = formatConnectFailureLog(QStringLiteral("access_denied"),
                                                 QStringLiteral("1045"),
                                                 QStringLiteral("127.0.0.1"),
                                                 3306,
                                                 QStringLiteral("hotelnext"),
                                                 QStringLiteral("hotel_api"),
                                                 driver,
                                                 server);
    QVERIFY(line.contains(QStringLiteral("code=access_denied")));
    QVERIFY(line.contains(QStringLiteral("native=1045")));
    QVERIFY(line.contains(QStringLiteral("host=127.0.0.1")));
    QVERIFY(line.contains(QStringLiteral("port=3306")));
    QVERIFY(line.contains(QStringLiteral("database=hotelnext")));
    QVERIFY(line.contains(QStringLiteral("user=hotel_api")));
    QVERIFY(!line.contains(QStringLiteral(" hotel")));
    QVERIFY(line.contains(QStringLiteral("***")));

    const QString tricky = formatConnectFailureLog(QStringLiteral("connection_failed"),
                                                   QStringLiteral("1"),
                                                   QStringLiteral("127.0.0.1"),
                                                   3306,
                                                   QStringLiteral("hotelnext"),
                                                   QStringLiteral("hotel_api"),
                                                   QStringLiteral("odd %1 %2 leftover"),
                                                   QStringLiteral("server"));
    QVERIFY(tricky.contains(QStringLiteral("driver=odd %1 %2 leftover")));
    QVERIFY(tricky.contains(QStringLiteral("user=hotel_api")));
    QVERIFY(tricky.endsWith(QStringLiteral("server=server")));
}

void TestDbError::connectOptions()
{
    const QString preferred = QStringLiteral(
        "MYSQL_OPT_CONNECT_TIMEOUT=3;MYSQL_OPT_READ_TIMEOUT=3;MYSQL_OPT_WRITE_TIMEOUT=3;"
        "MYSQL_OPT_SSL_MODE=PREFERRED;MYSQL_OPT_SSL_VERIFY_SERVER_CERT=0");
    QCOMPARE(mysqlConnectOptions(3, QString(), QString()), preferred);
    QCOMPARE(mysqlConnectOptions(0, QStringLiteral("preferred"), QString()), preferred);
    QCOMPARE(mysqlConnectOptions(8, QStringLiteral("off"), QString()),
             QStringLiteral(
                 "MYSQL_OPT_CONNECT_TIMEOUT=8;MYSQL_OPT_READ_TIMEOUT=8;MYSQL_OPT_WRITE_TIMEOUT=8;"
                 "MYSQL_OPT_SSL_MODE=DISABLED;MYSQL_OPT_SSL_VERIFY_SERVER_CERT=0"));
    QCOMPARE(mysqlConnectOptions(3, QStringLiteral("required"), QStringLiteral("C:/ca.pem")),
             QStringLiteral(
                 "MYSQL_OPT_CONNECT_TIMEOUT=3;MYSQL_OPT_READ_TIMEOUT=3;MYSQL_OPT_WRITE_TIMEOUT=3;"
                 "MYSQL_OPT_SSL_MODE=REQUIRED;MYSQL_OPT_SSL_VERIFY_SERVER_CERT=1;"
                 "MYSQL_OPT_SSL_CA=C:/ca.pem"));
    QCOMPARE(mysqlConnectOptions(3, QStringLiteral("verify"), QStringLiteral("/etc/hotel-api/ca.pem")),
             QStringLiteral(
                 "MYSQL_OPT_CONNECT_TIMEOUT=3;MYSQL_OPT_READ_TIMEOUT=3;MYSQL_OPT_WRITE_TIMEOUT=3;"
                 "MYSQL_OPT_SSL_MODE=VERIFY_CA;MYSQL_OPT_SSL_VERIFY_SERVER_CERT=1;"
                 "MYSQL_OPT_SSL_CA=/etc/hotel-api/ca.pem"));
    QVERIFY(!mysqlConnectOptions(3, QStringLiteral("preferred"), QStringLiteral("C:/ca.pem"))
                 .contains(QStringLiteral("SSL_CA")));
    QVERIFY(!mysqlConnectOptions(3, QStringLiteral("preferred"), QString()).contains(QStringLiteral("MYSQL_SET_CHARSET_NAME")));

    const QString mariaPreferred = QStringLiteral(
        "MYSQL_OPT_CONNECT_TIMEOUT=3;MYSQL_OPT_READ_TIMEOUT=3;MYSQL_OPT_WRITE_TIMEOUT=3;"
        "MYSQL_OPT_SSL_VERIFY_SERVER_CERT=0");
    QCOMPARE(mysqlConnectOptions(3, QString(), QString(), MysqlClientKind::MariaDb), mariaPreferred);
    QCOMPARE(mysqlConnectOptions(3, QStringLiteral("off"), QString(), MysqlClientKind::MariaDb), mariaPreferred);
    const QString mariaRequired = QStringLiteral(
        "MYSQL_OPT_CONNECT_TIMEOUT=3;MYSQL_OPT_READ_TIMEOUT=3;MYSQL_OPT_WRITE_TIMEOUT=3;"
        "MYSQL_OPT_SSL_VERIFY_SERVER_CERT=1;MYSQL_OPT_SSL_CA=C:/ca.pem");
    QCOMPARE(mysqlConnectOptions(3, QStringLiteral("required"), QStringLiteral("C:/ca.pem"), MysqlClientKind::MariaDb),
             mariaRequired);
    QVERIFY(!mariaRequired.contains(QStringLiteral("MYSQL_OPT_SSL_MODE")));
    QCOMPARE(mysqlConnectOptions(3, QStringLiteral("verify"), QStringLiteral("/etc/hotel-api/ca.pem"), MysqlClientKind::MariaDb),
             QStringLiteral(
                 "MYSQL_OPT_CONNECT_TIMEOUT=3;MYSQL_OPT_READ_TIMEOUT=3;MYSQL_OPT_WRITE_TIMEOUT=3;"
                 "MYSQL_OPT_SSL_VERIFY_SERVER_CERT=1;MYSQL_OPT_SSL_CA=/etc/hotel-api/ca.pem"));
}

void TestDbError::clientInfoKind()
{
    QCOMPARE(mysqlClientKindFromInfo(QStringLiteral("3.3.17")), MysqlClientKind::MariaDb);
    QCOMPARE(mysqlClientKindFromInfo(QStringLiteral("3.4.5")), MysqlClientKind::MariaDb);
    QCOMPARE(mysqlClientKindFromInfo(QStringLiteral("10.11.14-MariaDB")), MysqlClientKind::MariaDb);
    QCOMPARE(mysqlClientKindFromInfo(QStringLiteral("8.0.46")), MysqlClientKind::LibMySql);
    QCOMPARE(mysqlClientKindFromInfo(QStringLiteral("5.7.44")), MysqlClientKind::LibMySql);
    QCOMPARE(mysqlClientKindFromInfo(QString()), MysqlClientKind::LibMySql);
}

void TestDbError::pluginImageNamesClientLibrary()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString elfPath = dir.filePath(QStringLiteral("libqsqlmysql.so"));
    QVERIFY(writeBytes(elfPath, elf64WithNeeded("libmariadb.so.3")));
    QCOMPARE(mysqlClientLibrariesInPlugin(elfPath), QStringList{QStringLiteral("libmariadb.so.3")});
    QVERIFY(writeBytes(elfPath, elf64WithNeeded("libmysqlclient.so.21")));
    QCOMPARE(mysqlClientLibrariesInPlugin(elfPath), QStringList{QStringLiteral("libmysqlclient.so.21")});

    const QString pePath = dir.filePath(QStringLiteral("qsqlmysql.dll"));
    QVERIFY(writeBytes(pePath, pe64WithImport("libmariadb.dll")));
    QCOMPARE(mysqlClientLibrariesInPlugin(pePath), QStringList{QStringLiteral("libmariadb.dll")});
    QVERIFY(writeBytes(pePath, pe64WithImport("libmysql.dll")));
    QCOMPARE(mysqlClientLibrariesInPlugin(pePath), QStringList{QStringLiteral("libmysql.dll")});
}

void TestDbError::loadedPluginClientKind()
{
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QMYSQL")))
        QSKIP("QMYSQL is not installed");
    // isDriverAvailable only reads plugin metadata. addDatabase maps the module.
    const QString connection = QStringLiteral("mysql-client-kind");
    {
        const QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connection);
        QVERIFY(db.isValid());
        const QString path = loadedQmysqlPluginPath();
        QVERIFY2(!path.isEmpty(), "QMYSQL is loaded but its module path was not found");
        const QStringList libs = mysqlClientLibrariesInPlugin(path);
        QVERIFY2(!libs.isEmpty(), qPrintable(path + QStringLiteral(" -> ") + libs.join(QLatin1Char(','))));
        bool mariadb = false;
        for (const QString &lib : libs) {
            if (lib.contains(QLatin1String("mariadb"), Qt::CaseInsensitive))
                mariadb = true;
        }
        QCOMPARE(detectedMysqlClientKind(), mariadb ? MysqlClientKind::MariaDb : MysqlClientKind::LibMySql);
    }
    QSqlDatabase::removeDatabase(connection);
}

QTEST_GUILESS_MAIN(TestDbError)
#include "test_dberror.moc"
