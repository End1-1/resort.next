#pragma once

#include <QHostAddress>
#include <QString>

struct ListenEndpoint {
    QHostAddress address = QHostAddress::LocalHost;
    quint16 port = 8080;
};

struct DatabaseTarget {
    bool configured = false;
    QString host;
    int port = 3306;
    QString database;
    QString user;
    QString password;
    // off, preferred, required, or verify. Empty means preferred.
    QString sslMode;
    QString sslCa;
};

struct AppConfig {
    ListenEndpoint http;
    bool websocketEnabled = false;
    ListenEndpoint websocket;
    DatabaseTarget database;
    int dbConnectTimeoutSec = 3;
};

enum class DatabaseConfigNotice {
    None,
    DeprecatedDsn,
    MysqlOverridesDsn
};

struct ConfigLoadResult {
    bool ok = false;
    AppConfig config;
    QString error;
    // Absolute path of the ini that was read. Empty when no file was used.
    QString configPath;
    DatabaseConfigNotice databaseNotice = DatabaseConfigNotice::None;
};

// Already-trimmed fields. A non-empty mysql_* value selects the new form.
// dsn is used only when every mysql_* value is empty.
struct DatabaseResolveInput {
    QString mysqlHost;
    QString mysqlPort;
    QString mysqlSchema;
    QString mysqlUser;
    QString mysqlPassword;
    QString mysqlSsl;
    QString mysqlSslCa;
    QString dsn;
};

struct DatabaseResolveResult {
    bool ok = true;
    DatabaseTarget target;
    QString error;
    DatabaseConfigNotice notice = DatabaseConfigNotice::None;
};

DatabaseResolveResult resolveDatabaseTarget(const DatabaseResolveInput &input);

// "not configured", or "127.0.0.1:3306/hotelnext user=root". Never the password.
QString databaseStartupDetail(const DatabaseTarget &target);

// Empty when there is nothing to log. The text does not include the DSN or password.
QString databaseConfigNoticeLine(DatabaseConfigNotice notice);

// Search order when HOTEL_CONFIG is unset or empty:
//   1. hotel-api.ini next to the executable (application dir, not the working directory)
//   2. /etc/hotel-api/hotel-api.ini on Linux
// A non-empty HOTEL_CONFIG replaces that search.
// HOTEL_LISTEN, HOTEL_WS_LISTEN, and HOTEL_MYSQL_HOST/PORT/SCHEMA/USER/PASSWORD/SSL/SSL_CA
// override ini keys only when non-empty. HOTEL_DSN is the legacy fallback and
// is ignored when any mysql host/schema/user/password/port value is set.
// mysql_ssl does not by itself turn the database on.
ConfigLoadResult loadConfig();
