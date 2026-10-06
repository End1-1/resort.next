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
    return QStringLiteral("connection_failed");
}

QString probeDatabaseError(const QString &failure)
{
    if (failure == QLatin1String("driver_not_loaded")
        || failure == QLatin1String("access_denied")
        || failure == QLatin1String("unknown_database")
        || failure == QLatin1String("cannot_connect")
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

QString mysqlConnectOptions(int connectTimeoutSec)
{
    const int timeoutSec = connectTimeoutSec > 0 ? connectTimeoutSec : 3;
    const QString seconds = QString::number(timeoutSec);
    return QStringLiteral(
               "MYSQL_OPT_CONNECT_TIMEOUT=%1;MYSQL_OPT_READ_TIMEOUT=%2;MYSQL_OPT_WRITE_TIMEOUT=%3")
        .arg(seconds, seconds, seconds);
}
