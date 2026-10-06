#include "dberror.h"

#include <QUrl>

namespace {

QString flatten(const QString &text)
{
    QString cleaned = text;
    cleaned.replace(QLatin1Char('\r'), QLatin1Char(' '));
    cleaned.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return cleaned;
}

} // namespace

QString publicDatabaseErrorCode(const QString &nativeErrorCode)
{
    if (nativeErrorCode == QLatin1String("1045"))
        return QStringLiteral("access_denied");
    if (nativeErrorCode == QLatin1String("1049"))
        return QStringLiteral("unknown_database");
    if (nativeErrorCode == QLatin1String("2002") || nativeErrorCode == QLatin1String("2003"))
        return QStringLiteral("cannot_connect");
    if (nativeErrorCode == QLatin1String("2026"))
        return QStringLiteral("tls_error");
    return QStringLiteral("connection_failed");
}

QString probeDatabaseError(const QString &failure)
{
    if (failure == QLatin1String("driver_not_loaded")
        || failure == QLatin1String("access_denied")
        || failure == QLatin1String("unknown_database")
        || failure == QLatin1String("cannot_connect")
        || failure == QLatin1String("tls_error")
        || failure == QLatin1String("connection_failed")) {
        return failure;
    }
    return QStringLiteral("connection_failed");
}

QString formatConnectFailureLog(const QString &publicCode,
                                const QString &nativeErrorCode,
                                const QString &host,
                                int port,
                                const QString &database,
                                const QString &user,
                                const QString &driverText,
                                const QString &databaseText)
{
    const QString native = nativeErrorCode.isEmpty() ? QStringLiteral("-") : nativeErrorCode;
    // One .arg() pass. A '%' inside driver text must not eat a later field.
    return QStringLiteral(
               "database connect failed: code=%1 native=%2 host=%3 port=%4 database=%5 user=%6 driver=%7 server=%8")
        .arg(publicCode, native, host, QString::number(port), database, user, driverText, databaseText);
}

QString scrubDatabaseMessage(const QString &text, const QString &password)
{
    QString cleaned = flatten(text);
    if (password.isEmpty())
        return cleaned;
    cleaned.replace(password, QStringLiteral("***"));
    const QByteArray encoded = QUrl::toPercentEncoding(password);
    const QString encodedText = QString::fromUtf8(encoded);
    if (!encodedText.isEmpty() && encodedText != password)
        cleaned.replace(encodedText, QStringLiteral("***"));
    return cleaned;
}

QString mysqlConnectOptions(int connectTimeoutSec, const QString &sslMode, const QString &sslCa)
{
    const int timeoutSec = connectTimeoutSec > 0 ? connectTimeoutSec : 3;
    const QString seconds = QString::number(timeoutSec);
    const QString mode = sslMode.isEmpty() ? QStringLiteral("preferred") : sslMode;

    // Qt 6.10.2 qsql_mysql.cpp passes these before mysql_real_connect:
    //   MYSQL_OPT_SSL_MODE — only when the plugin is built against libmysqlclient
    //     (!defined(MARIADB_VERSION_ID) && MYSQL_VERSION_ID >= 50711).
    //     Tokens: DISABLED, PREFERRED, REQUIRED, VERIFY_CA.
    //     A MariaDB-built plugin logs "Unknown connect option" and ignores it.
    //   MYSQL_OPT_SSL_VERIFY_SERVER_CERT — when built against MariaDB Connector/C
    //     (and MySQL older than 8.0). libmysqlclient 8 ignores it.
    //     Connector/C 3.4 ships with this flag on, which refuses a server that
    //     has no TLS (native 2026). 0 clears that. Qt passes a bool, one byte,
    //     which is the width Connector/C expects. Connector/C 3.3 already
    //     defaults the flag to off, so 0 does not change it.
    // preferred/off use verify=0 so a local server without TLS connects.
    // required/verify use verify=1 so Connector/C will not drop TLS.
    QString sslModeToken = QStringLiteral("PREFERRED");
    QString verify = QStringLiteral("0");
    if (mode == QLatin1String("off")) {
        sslModeToken = QStringLiteral("DISABLED");
    } else if (mode == QLatin1String("required")) {
        sslModeToken = QStringLiteral("REQUIRED");
        verify = QStringLiteral("1");
    } else if (mode == QLatin1String("verify")) {
        sslModeToken = QStringLiteral("VERIFY_CA");
        verify = QStringLiteral("1");
    }

    QString options = QStringLiteral(
                          "MYSQL_OPT_CONNECT_TIMEOUT=%1;MYSQL_OPT_READ_TIMEOUT=%2;MYSQL_OPT_WRITE_TIMEOUT=%3;"
                          "MYSQL_OPT_SSL_MODE=%4;MYSQL_OPT_SSL_VERIFY_SERVER_CERT=%5")
                          .arg(seconds, seconds, seconds, sslModeToken, verify);
    if (!sslCa.isEmpty() && (mode == QLatin1String("required") || mode == QLatin1String("verify")))
        options += QStringLiteral(";MYSQL_OPT_SSL_CA=") + sslCa;
    return options;
}
