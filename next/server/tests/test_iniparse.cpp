#include "iniparse.h"

#include <QFile>
#include <QTest>

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
};

void TestIniParse::shippedExample()
{
    HotelIniValues values;
    QString error;
    QVERIFY2(readHotelIniFile(QStringLiteral(HOTEL_API_INI_EXAMPLE), &values, &error),
             qPrintable(error));
    QVERIFY(values.hasListen);
    QCOMPARE(values.listen, QStringLiteral("127.0.0.1:8080"));
    QVERIFY(values.hasDsn);
    QCOMPARE(values.dsn, QString());
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

QTEST_GUILESS_MAIN(TestIniParse)
#include "test_iniparse.moc"
