#include "dberror.h"

#include <QTest>

class TestDbError : public QObject
{
    Q_OBJECT

private slots:
    void mapsNativeCodes();
    void probeRejectsRawText();
    void scrubsPasswordAndEncoding();
    void logKeepsUserAndDropsPassword();
    void connectOptions();
};

void TestDbError::mapsNativeCodes()
{
    QCOMPARE(publicDatabaseErrorCode(QStringLiteral("1045")), QStringLiteral("access_denied"));
    QCOMPARE(publicDatabaseErrorCode(QStringLiteral("1049")), QStringLiteral("unknown_database"));
    QCOMPARE(publicDatabaseErrorCode(QStringLiteral("2002")), QStringLiteral("cannot_connect"));
    QCOMPARE(publicDatabaseErrorCode(QStringLiteral("2003")), QStringLiteral("cannot_connect"));
    QCOMPARE(publicDatabaseErrorCode(QStringLiteral("2013")), QStringLiteral("connection_failed"));
    QCOMPARE(publicDatabaseErrorCode(QStringLiteral("1146")), QStringLiteral("connection_failed"));
    QCOMPARE(publicDatabaseErrorCode(QString()), QStringLiteral("connection_failed"));
    QCOMPARE(publicDatabaseErrorCode(QStringLiteral("10450")), QStringLiteral("connection_failed"));
}

void TestDbError::probeRejectsRawText()
{
    QCOMPARE(probeDatabaseError(QStringLiteral("access_denied")), QStringLiteral("access_denied"));
    QCOMPARE(probeDatabaseError(QStringLiteral("driver_not_loaded")), QStringLiteral("driver_not_loaded"));
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
    QCOMPARE(mysqlConnectOptions(3),
             QStringLiteral(
                 "MYSQL_OPT_CONNECT_TIMEOUT=3;MYSQL_OPT_READ_TIMEOUT=3;MYSQL_OPT_WRITE_TIMEOUT=3"));
    QCOMPARE(mysqlConnectOptions(0),
             QStringLiteral(
                 "MYSQL_OPT_CONNECT_TIMEOUT=3;MYSQL_OPT_READ_TIMEOUT=3;MYSQL_OPT_WRITE_TIMEOUT=3"));
    QCOMPARE(mysqlConnectOptions(8),
             QStringLiteral(
                 "MYSQL_OPT_CONNECT_TIMEOUT=8;MYSQL_OPT_READ_TIMEOUT=8;MYSQL_OPT_WRITE_TIMEOUT=8"));
    QVERIFY(mysqlConnectOptions(3).contains(QStringLiteral("MYSQL_OPT_CONNECT_TIMEOUT=3")));
    QVERIFY(!mysqlConnectOptions(3).contains(QStringLiteral("MYSQL_SET_CHARSET_NAME")));
}

QTEST_GUILESS_MAIN(TestDbError)
#include "test_dberror.moc"
