#include "iniparse.h"

#include <QCoreApplication>
#include <QFile>
#include <QTest>

int runDatabaseConfigTests(int argc, char **argv);

class TestIniParse : public QObject
{
    Q_OBJECT

private slots:
    void shippedExample();
    void hashAndSemicolonComments();
    void crlf();
    void utf8Bom();
    void percent40StaysLiteral();
    void quotedValue();
    void section();
    void utf16Bom();
    void malformedLine();
    void missingFile();
    void mysqlKeysKeepLiteralPassword();
};

void TestIniParse::shippedExample()
{
    HotelIniValues values;
    QString error;
    QVERIFY2(readHotelIniFile(QStringLiteral(HOTEL_API_INI_EXAMPLE), &values, &error),
             qPrintable(error));
    QVERIFY(values.hasListen);
    QCOMPARE(values.listen, QStringLiteral("127.0.0.1:8080"));
    QVERIFY(!values.hasDsn);
    QVERIFY(values.hasMysqlHost);
    QCOMPARE(values.mysqlHost, QString());
    QVERIFY(values.hasMysqlPort);
    QCOMPARE(values.mysqlPort, QString());
    QVERIFY(values.hasMysqlSchema);
    QCOMPARE(values.mysqlSchema, QString());
    QVERIFY(values.hasMysqlUser);
    QCOMPARE(values.mysqlUser, QString());
    QVERIFY(values.hasMysqlPassword);
    QCOMPARE(values.mysqlPassword, QString());
    QVERIFY(values.hasWsListen);
    QCOMPARE(values.wsListen, QString());
}

void TestIniParse::hashAndSemicolonComments()
{
    const QByteArray data =
        "# comment without equals\n"
        "; also a comment\n"
        "# dsn=mysql://should:not@be-read:3306/nope\n"
        "listen=127.0.0.1:8080\n"
        "dsn=mysql://user:p%40ss@127.0.0.1:3306/db\n";
    HotelIniValues values;
    QString error;
    QVERIFY2(parseHotelIni(data, &values, &error), qPrintable(error));
    QCOMPARE(values.listen, QStringLiteral("127.0.0.1:8080"));
    QCOMPARE(values.dsn, QStringLiteral("mysql://user:p%40ss@127.0.0.1:3306/db"));
}

void TestIniParse::crlf()
{
    const QByteArray data =
        "# comment\r\n"
        "listen=127.0.0.1:9090\r\n"
        "dsn=mysql://user:pass@127.0.0.1:3306/db\r\n"
        "ws_listen=\r\n";
    HotelIniValues values;
    QString error;
    QVERIFY2(parseHotelIni(data, &values, &error), qPrintable(error));
    QCOMPARE(values.listen, QStringLiteral("127.0.0.1:9090"));
    QCOMPARE(values.dsn, QStringLiteral("mysql://user:pass@127.0.0.1:3306/db"));
    QVERIFY(values.hasWsListen);
    QCOMPARE(values.wsListen, QString());
}

void TestIniParse::utf8Bom()
{
    QByteArray data;
    data.append(char(0xEF));
    data.append(char(0xBB));
    data.append(char(0xBF));
    data.append("listen=8080\ndsn=mysql://user:pass@127.0.0.1:3306/db\n");
    HotelIniValues values;
    QString error;
    QVERIFY2(parseHotelIni(data, &values, &error), qPrintable(error));
    QCOMPARE(values.listen, QStringLiteral("8080"));
    QCOMPARE(values.dsn, QStringLiteral("mysql://user:pass@127.0.0.1:3306/db"));
}

void TestIniParse::percent40StaysLiteral()
{
    const QByteArray data = "dsn=mysql://hotel_api:p%40ss%3Aword@127.0.0.1:3306/resort\n";
    HotelIniValues values;
    QString error;
    QVERIFY2(parseHotelIni(data, &values, &error), qPrintable(error));
    QCOMPARE(values.dsn, QStringLiteral("mysql://hotel_api:p%40ss%3Aword@127.0.0.1:3306/resort"));
}

void TestIniParse::quotedValue()
{
    const QByteArray data = "listen=\"127.0.0.1:8080\"\ndsn=\"mysql://user:p%40ss@h:3306/db\"\n";
    HotelIniValues values;
    QString error;
    QVERIFY2(parseHotelIni(data, &values, &error), qPrintable(error));
    QCOMPARE(values.listen, QStringLiteral("127.0.0.1:8080"));
    QCOMPARE(values.dsn, QStringLiteral("mysql://user:p%40ss@h:3306/db"));
}

void TestIniParse::section()
{
    const QByteArray data = "[hotel-api]\nlisten=9090\n[General]\ndsn=mysql://u:p@h:3306/db\n";
    HotelIniValues values;
    QString error;
    QVERIFY2(parseHotelIni(data, &values, &error), qPrintable(error));
    QCOMPARE(values.listen, QStringLiteral("9090"));
    QCOMPARE(values.dsn, QStringLiteral("mysql://u:p@h:3306/db"));

    QString bad;
    QVERIFY(!parseHotelIni("[database]\nlisten=1\n", &values, &bad));
    QVERIFY(bad.contains(QStringLiteral("unknown section")));
}

void TestIniParse::utf16Bom()
{
    QByteArray data;
    data.append(char(0xFF));
    data.append(char(0xFE));
    const char text[] = "listen=127.0.0.1:8080\n";
    for (const char *p = text; *p; ++p) {
        data.append(*p);
        data.append(char(0));
    }
    HotelIniValues values;
    QString error;
    QVERIFY(!parseHotelIni(data, &values, &error));
    QCOMPARE(error, QStringLiteral("file is UTF-16; save as UTF-8"));
}

void TestIniParse::malformedLine()
{
    HotelIniValues values;
    QString error;
    QVERIFY(!parseHotelIni("listen\n", &values, &error));
    QCOMPARE(error, QStringLiteral("line 1 malformed"));
}

void TestIniParse::missingFile()
{
    HotelIniValues values;
    QString error;
    QVERIFY(!readHotelIniFile(QStringLiteral("/tmp/hotel-api-ini-does-not-exist.ini"), &values, &error));
    QVERIFY(error.startsWith(QStringLiteral("cannot open ")));
}

void TestIniParse::mysqlKeysKeepLiteralPassword()
{
    const QByteArray data =
        "# mysql_password=not-this\n"
        "; mysql_user=not-this\n"
        "mysql_host = 127.0.0.1\n"
        "mysql_port = 3306\n"
        "mysql_schema = hotelnext\n"
        "mysql_user = root\n"
        "mysql_password = p@ss:w%rd#;x\n";
    HotelIniValues values;
    QString error;
    QVERIFY2(parseHotelIni(data, &values, &error), qPrintable(error));
    QCOMPARE(values.mysqlHost, QStringLiteral("127.0.0.1"));
    QCOMPARE(values.mysqlPort, QStringLiteral("3306"));
    QCOMPARE(values.mysqlSchema, QStringLiteral("hotelnext"));
    QCOMPARE(values.mysqlUser, QStringLiteral("root"));
    QCOMPARE(values.mysqlPassword, QStringLiteral("p@ss:w%rd#;x"));
    QVERIFY(!values.mysqlPassword.contains(QStringLiteral("%40")));

    QVERIFY2(parseHotelIni("mysql_password=\" spaced \"\n", &values, &error), qPrintable(error));
    QCOMPARE(values.mysqlPassword, QStringLiteral(" spaced "));

    QString bad;
    QVERIFY(!parseHotelIni("mysqlhost=127.0.0.1\n", &values, &bad));
    QVERIFY(bad.contains(QStringLiteral("unknown key")));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    TestIniParse ini;
    const int status = QTest::qExec(&ini, argc, argv);
    return status | runDatabaseConfigTests(argc, argv);
}

#include "test_iniparse.moc"
