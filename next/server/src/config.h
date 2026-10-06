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
};

struct AppConfig {
    ListenEndpoint http;
    bool websocketEnabled = false;
    ListenEndpoint websocket;
    DatabaseTarget database;
    int dbConnectTimeoutSec = 3;
};

struct ConfigLoadResult {
    bool ok = false;
    AppConfig config;
    QString error;
    // Absolute path of the ini that was read. Empty when no file was used.
    QString configPath;
};

// Search order when HOTEL_CONFIG is unset or empty:
//   1. hotel-api.ini next to the executable (application dir, not the working directory)
//   2. /etc/hotel-api/hotel-api.ini on Linux
// A non-empty HOTEL_CONFIG replaces that search.
// HOTEL_LISTEN, HOTEL_DSN, and HOTEL_WS_LISTEN override ini keys only when non-empty.
ConfigLoadResult loadConfig();
