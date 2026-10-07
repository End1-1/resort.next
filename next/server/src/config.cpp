#include "config.h"

#include "iniparse.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
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

bool mysqlValuesPresent(const DatabaseResolveInput &input)
{
    return !input.mysqlHost.isEmpty() || !input.mysqlPort.isEmpty() || !input.mysqlSchema.isEmpty()
        || !input.mysqlUser.isEmpty() || !input.mysqlPassword.isEmpty();
}

bool canonicalMysqlSsl(const QString &text, QString *mode, QString *error)
{
    const QString value = text.trimmed().toLower();
    if (value.isEmpty() || value == QLatin1String("preferred")) {
        *mode = QStringLiteral("preferred");
        return true;
    }
    if (value == QLatin1String("off") || value == QLatin1String("required") || value == QLatin1String("verify")) {
        *mode = value;
        return true;
    }
    *error = QStringLiteral("mysql_ssl must be off, preferred, required, or verify");
    return false;
}

} // namespace

DatabaseResolveResult resolveDatabaseTarget(const DatabaseResolveInput &input)
{
    DatabaseResolveResult result;
    QString sslMode;
    QString sslError;
    if (!canonicalMysqlSsl(input.mysqlSsl, &sslMode, &sslError)) {
        result.ok = false;
        result.error = sslError;
        return result;
    }
    if (input.mysqlSslCa.contains(QLatin1Char(';')) || input.mysqlSslCa.contains(QLatin1Char('\n'))
        || input.mysqlSslCa.contains(QLatin1Char('\r'))) {
        result.ok = false;
        result.error = QStringLiteral("mysql_ssl_ca must not contain ';'");
        return result;
    }
    const auto stampSsl = [&]() {
        result.target.sslMode = sslMode;
        result.target.sslCa = input.mysqlSslCa;
    };

    const bool mysql = mysqlValuesPresent(input);
    const QString dsn = input.dsn.trimmed();
    const bool legacy = !dsn.isEmpty();

    if (mysql) {
        if (legacy)
            result.notice = DatabaseConfigNotice::MysqlOverridesDsn;
        if (input.mysqlHost.isEmpty()) {
            result.ok = false;
            result.error = QStringLiteral("mysql_host is required");
            return result;
        }
        if (input.mysqlSchema.isEmpty()) {
            result.ok = false;
            result.error = QStringLiteral("mysql_schema is required");
            return result;
        }
        if (input.mysqlUser.isEmpty()) {
            result.ok = false;
            result.error = QStringLiteral("mysql_user is required");
            return result;
        }
        int port = 3306;
        if (!input.mysqlPort.isEmpty()) {
            bool ok = false;
            const int parsed = input.mysqlPort.toInt(&ok);
            if (!ok || parsed < 1 || parsed > 65535) {
                result.ok = false;
                result.error = QStringLiteral("mysql_port has an invalid port");
                return result;
            }
            port = parsed;
        }
        result.target.configured = true;
        result.target.host = input.mysqlHost;
        result.target.port = port;
        result.target.database = input.mysqlSchema;
        result.target.user = input.mysqlUser;
        result.target.password = input.mysqlPassword;
        stampSsl();
        return result;
    }

    if (legacy) {
        result.notice = DatabaseConfigNotice::DeprecatedDsn;
        QString error;
        if (!parseDsn(dsn, &result.target, &error)) {
            result.ok = false;
            result.error = error;
            return result;
        }
        stampSsl();
        return result;
    }

    stampSsl();
    return result;
}

QString databaseStartupDetail(const DatabaseTarget &target)
{
    if (!target.configured)
        return QStringLiteral("not configured");
    return QStringLiteral("%1:%2/%3 user=%4")
        .arg(target.host, QString::number(target.port), target.database, target.user);
}

QString databaseConfigNoticeLine(DatabaseConfigNotice notice)
{
    if (notice == DatabaseConfigNotice::DeprecatedDsn) {
        return QStringLiteral(
            "hotel-api database: dsn is deprecated; use mysql_host, mysql_port, mysql_schema, mysql_user, mysql_password");
    }
    if (notice == DatabaseConfigNotice::MysqlOverridesDsn)
        return QStringLiteral("hotel-api database: mysql_* overrides dsn");
    return QString();
}

namespace {

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

struct IniFields {
    QString listen = QStringLiteral("127.0.0.1:8080");
    QString websocket;
    QString mysqlHost;
    QString mysqlPort;
    QString mysqlSchema;
    QString mysqlUser;
    QString mysqlPassword;
    QString mysqlSsl;
    QString mysqlSslCa;
    QString dsn;
};

bool readIniFile(const QString &path, IniFields *fields, QString *error)
{
    HotelIniValues values;
    if (!readHotelIniFile(path, &values, error))
        return false;
    if (values.hasListen)
        fields->listen = values.listen;
    if (values.hasWsListen)
        fields->websocket = values.wsListen;
    if (values.hasMysqlHost)
        fields->mysqlHost = values.mysqlHost;
    if (values.hasMysqlPort)
        fields->mysqlPort = values.mysqlPort;
    if (values.hasMysqlSchema)
        fields->mysqlSchema = values.mysqlSchema;
    if (values.hasMysqlUser)
        fields->mysqlUser = values.mysqlUser;
    if (values.hasMysqlPassword)
        fields->mysqlPassword = values.mysqlPassword;
    if (values.hasMysqlSsl)
        fields->mysqlSsl = values.mysqlSsl;
    if (values.hasMysqlSslCa)
        fields->mysqlSslCa = values.mysqlSslCa;
    if (values.hasDsn)
        fields->dsn = values.dsn;
    return true;
}

void overlayEnv(const char *name, QString *field)
{
    const QString value = nonEmptyEnv(name);
    if (!value.isEmpty())
        *field = value;
}

} // namespace

ConfigLoadResult loadConfig()
{
    ConfigLoadResult result;
    result.ok = true;

    IniFields fields;

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
        if (!readIniFile(chosenPath, &fields, &error)) {
            fail(&result, error);
            return result;
        }
        result.configPath = QDir::toNativeSeparators(chosenPath);
    }

    overlayEnv("HOTEL_LISTEN", &fields.listen);
    overlayEnv("HOTEL_WS_LISTEN", &fields.websocket);
    overlayEnv("HOTEL_MYSQL_HOST", &fields.mysqlHost);
    overlayEnv("HOTEL_MYSQL_PORT", &fields.mysqlPort);
    overlayEnv("HOTEL_MYSQL_SCHEMA", &fields.mysqlSchema);
    overlayEnv("HOTEL_MYSQL_USER", &fields.mysqlUser);
    overlayEnv("HOTEL_MYSQL_PASSWORD", &fields.mysqlPassword);
    overlayEnv("HOTEL_MYSQL_SSL", &fields.mysqlSsl);
    overlayEnv("HOTEL_MYSQL_SSL_CA", &fields.mysqlSslCa);
    overlayEnv("HOTEL_DSN", &fields.dsn);

    QString error;
    if (!parseListen(fields.listen, &result.config.http, &error, "HOTEL_LISTEN")) {
        fail(&result, error);
        return result;
    }

    DatabaseResolveInput databaseInput;
    databaseInput.mysqlHost = fields.mysqlHost;
    databaseInput.mysqlPort = fields.mysqlPort;
    databaseInput.mysqlSchema = fields.mysqlSchema;
    databaseInput.mysqlUser = fields.mysqlUser;
    databaseInput.mysqlPassword = fields.mysqlPassword;
    databaseInput.mysqlSsl = fields.mysqlSsl;
    databaseInput.mysqlSslCa = fields.mysqlSslCa;
    databaseInput.dsn = fields.dsn;
    const DatabaseResolveResult database = resolveDatabaseTarget(databaseInput);
    result.databaseNotice = database.notice;
    if (!database.ok) {
        fail(&result, database.error);
        return result;
    }
    result.config.database = database.target;

    const QString websocketTrimmed = fields.websocket.trimmed();
    if (!websocketTrimmed.isEmpty()) {
        if (!parseListen(websocketTrimmed, &result.config.websocket, &error, "HOTEL_WS_LISTEN")) {
            fail(&result, error);
            return result;
        }
        result.config.websocketEnabled = true;
    }

    return result;
}
