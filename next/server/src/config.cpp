#include "config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
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

QString nonEmptyEnv(const char *name)
{
    const QString value = qEnvironmentVariable(name).trimmed();
    return value;
}

// applicationDirPath is the executable's directory. The Windows service
// working directory is System32, so the ini must not be resolved from cwd.
QStringList defaultConfigCandidates()
{
    QStringList candidates;
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty())
        candidates.append(QDir(appDir).filePath(QStringLiteral("hotel-api.ini")));
#ifdef Q_OS_LINUX
    candidates.append(QStringLiteral("/etc/hotel-api/hotel-api.ini"));
#endif
    return candidates;
}

bool readIniFile(const QString &path, QString *listen, QString *dsn, QString *websocket, QString *error)
{
    QSettings settings(path, QSettings::IniFormat);
    settings.setFallbacksEnabled(false);
    if (settings.status() != QSettings::NoError) {
        *error = QStringLiteral("could not read config file: %1")
                     .arg(QDir::toNativeSeparators(path));
        return false;
    }
    if (settings.contains(QStringLiteral("listen")))
        *listen = settings.value(QStringLiteral("listen")).toString();
    if (settings.contains(QStringLiteral("dsn")))
        *dsn = settings.value(QStringLiteral("dsn")).toString();
    if (settings.contains(QStringLiteral("ws_listen")))
        *websocket = settings.value(QStringLiteral("ws_listen")).toString();
    if (settings.status() != QSettings::NoError) {
        *error = QStringLiteral("could not read config file: %1")
                     .arg(QDir::toNativeSeparators(path));
        return false;
    }
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

    const QString overridePath = nonEmptyEnv("HOTEL_CONFIG");
    QString chosenPath;
    if (!overridePath.isEmpty()) {
        const QFileInfo info(overridePath);
        if (!info.isFile()) {
            fail(&result,
                 QStringLiteral("HOTEL_CONFIG does not exist: %1")
                     .arg(QDir::toNativeSeparators(info.absoluteFilePath())));
            return result;
        }
        chosenPath = info.absoluteFilePath();
    } else {
        const QStringList candidates = defaultConfigCandidates();
        for (const QString &candidate : candidates) {
            const QFileInfo info(candidate);
            if (info.isFile()) {
                chosenPath = info.absoluteFilePath();
                break;
            }
        }
    }

    if (!chosenPath.isEmpty()) {
        QString error;
        if (!readIniFile(chosenPath, &listen, &dsn, &websocket, &error)) {
            fail(&result, error);
            return result;
        }
        result.configPath = QDir::toNativeSeparators(chosenPath);
    }

    const QString envListen = nonEmptyEnv("HOTEL_LISTEN");
    if (!envListen.isEmpty())
        listen = envListen;
    const QString envDsn = nonEmptyEnv("HOTEL_DSN");
    if (!envDsn.isEmpty())
        dsn = envDsn;
    const QString envWebsocket = nonEmptyEnv("HOTEL_WS_LISTEN");
    if (!envWebsocket.isEmpty())
        websocket = envWebsocket;

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
