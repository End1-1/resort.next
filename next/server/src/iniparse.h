#pragma once

#include <QByteArray>
#include <QString>

// Keys recognized in hotel-api.ini. Absent keys stay has* = false.
// Values are literal: %40 is not turned into @, and there is no comma split.
// dsn is accepted only so an old file still parses. New files use mysql_*.
struct HotelIniValues {
    bool hasListen = false;
    QString listen;
    bool hasMysqlHost = false;
    QString mysqlHost;
    bool hasMysqlPort = false;
    QString mysqlPort;
    bool hasMysqlSchema = false;
    QString mysqlSchema;
    bool hasMysqlUser = false;
    QString mysqlUser;
    bool hasMysqlPassword = false;
    QString mysqlPassword;
    bool hasMysqlSsl = false;
    QString mysqlSsl;
    bool hasMysqlSslCa = false;
    QString mysqlSslCa;
    bool hasDsn = false;
    QString dsn;
    bool hasWsListen = false;
    QString wsListen;
};

// UTF-8 text. A leading UTF-8 BOM is skipped. '#' and ';' comments are whole
// lines. Optional sections are [General] and [hotel-api]. On failure, *error
// is "file is UTF-16; save as UTF-8", "file is not UTF-8; save as UTF-8",
// "line N malformed", or "line N: ...".
bool parseHotelIni(const QByteArray &data, HotelIniValues *out, QString *error);

// Opens path as raw bytes. On failure, *error starts with "cannot open <path>:"
// plus QFile::errorString(), or the parseHotelIni phrase followed by ": <path>".
bool readHotelIniFile(const QString &path, HotelIniValues *out, QString *error);
