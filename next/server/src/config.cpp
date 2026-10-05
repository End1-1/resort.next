#include "config.h"

#include <QFileInfo>
#include <QSettings>
#include <QUrl>

namespace {

void fail(ConfigLoadResult *result, const QString &error)
{
    result->ok = false;
    result->error = error;
}

bool parseListen(const QString &text, ListenEndpoint *endpoint, QString *error, const char *name)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        *error = QStringLiteral("%1 is empty").arg(QLatin1String(name));
        return false;
    }

    QString host;
    QString portText;
    bool allDigits = !trimmed.isEmpty();
    for (const QChar ch : trimmed) {
        if (!ch.isDigit()) {
            allDigits = false;
            break;
        }
    }
    if (allDigits) {
        host = QStringLiteral("127.0.0.1");
        portText = trimmed;
    } else {
        const int colon = trimmed.lastIndexOf(QLatin1Char(':'));
        if (colon <= 0 || colon == trimmed.size() - 1) {
            *error = QStringLiteral("%1 must be <ip>:<port>, for example 127.0.0.1:8080").arg(QLatin1String(name));
            return false;
        }
        host = trimmed.left(colon);
        portText = trimmed.mid(colon + 1);
    }

    bool ok = false;
    const int port = portText.toInt(&ok);
    if (!ok || port < 1 || port > 65535) {
        *error = QStringLiteral("%1 has an invalid port").arg(QLatin1String(name));
        return false;
    }

    QHostAddress address;
    if (!address.setAddress(host)) {
        *error = QStringLiteral("%1 host must be a numeric IP address").arg(QLatin1String(name));
        return false;
    }

    endpoint->address = address;
    endpoint->port = static_cast<quint16>(port);
    return true;
}

bool parseDsn(const QString &text, DatabaseTarget *target, QString *error)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        *target = DatabaseTarget{};
        return true;
    }

    const QUrl url(trimmed, QUrl::StrictMode);
    if (!url.isValid() || url.scheme().compare(QLatin1String("mysql"), Qt::CaseInsensitive) != 0) {
        *error = QStringLiteral("HOTEL_DSN must be mysql://USER:PASSWORD@HOST:PORT/DATABASE");
        return false;
    }
    if (url.hasQuery() || url.hasFragment()) {
        *error = QStringLiteral("HOTEL_DSN must not include a query or fragment");
        return false;
    }
    if (url.host().isEmpty()) {
        *error = QStringLiteral("HOTEL_DSN is missing a host");
        return false;
    }

    QString database = url.path(QUrl::FullyDecoded);
    if (database.startsWith(QLatin1Char('/')))
        database.remove(0, 1);
    if (database.isEmpty() || database.contains(QLatin1Char('/'))) {
        *error = QStringLiteral("HOTEL_DSN is missing a database name");
        return false;
    }

    const int port = url.port(3306);
    if (port < 1 || port > 65535) {
        *error = QStringLiteral("HOTEL_DSN has an invalid port");
        return false;
    }

    target->configured = true;
    target->host = url.host();
    target->port = port;
    target->database = database;
    target->user = url.userName(QUrl::FullyDecoded);
    target->password = url.password(QUrl::FullyDecoded);
    return true;
}

} // namespace

ConfigLoadResult loadConfig()
{
    ConfigLoadResult result;
    result.ok = true;

    QString listen = QStringLiteral("127.0.0.1:8080");
    QString dsn;
    QString websocket;

    if (qEnvironmentVariableIsSet("HOTEL_CONFIG")) {
        const QString path = qEnvironmentVariable("HOTEL_CONFIG").trimmed();
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            fail(&result, QStringLiteral("HOTEL_CONFIG does not exist"));
            return result;
        }
        QSettings settings(path, QSettings::IniFormat);
        if (settings.status() != QSettings::NoError) {
            fail(&result, QStringLiteral("HOTEL_CONFIG could not be read"));
            return result;
        }
        listen = settings.value(QStringLiteral("listen"), listen).toString();
        dsn = settings.value(QStringLiteral("dsn")).toString();
        websocket = settings.value(QStringLiteral("ws_listen")).toString();
    }

    if (qEnvironmentVariableIsSet("HOTEL_LISTEN"))
        listen = qEnvironmentVariable("HOTEL_LISTEN");
    if (qEnvironmentVariableIsSet("HOTEL_DSN"))
        dsn = qEnvironmentVariable("HOTEL_DSN");
    if (qEnvironmentVariableIsSet("HOTEL_WS_LISTEN"))
        websocket = qEnvironmentVariable("HOTEL_WS_LISTEN");

    QString error;
    if (!parseListen(listen, &result.config.http, &error, "HOTEL_LISTEN")) {
        fail(&result, error);
        return result;
    }
    if (!parseDsn(dsn, &result.config.database, &error)) {
        fail(&result, error);
        return result;
    }

    const QString websocketTrimmed = websocket.trimmed();
    if (!websocketTrimmed.isEmpty()) {
        if (!parseListen(websocketTrimmed, &result.config.websocket, &error, "HOTEL_WS_LISTEN")) {
            fail(&result, error);
            return result;
        }
        result.config.websocketEnabled = true;
    }

    return result;
}
