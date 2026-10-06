#pragma once

#include "config.h"

#include <QSqlDatabase>
#include <QString>

// One QMYSQL connection for the calling thread. Health and login run on the
// HTTP handler thread and each call builds its own connection. The name is a
// fresh UUID, not the default connection, so two calls cannot share or remove
// each other's handle. The destructor closes it and removes the name. Callers
// must destroy QSqlQuery objects first. That lifetime is not why open() fails.
class MysqlConnection {
public:
    MysqlConnection(const DatabaseTarget &target, int connectTimeoutSec);
    ~MysqlConnection();

    MysqlConnection(const MysqlConnection &) = delete;
    MysqlConnection &operator=(const MysqlConnection &) = delete;

    bool opened = false;
    // database_not_configured, driver_not_loaded, connection_failed,
    // access_denied, unknown_database, or cannot_connect.
    // Never a DSN, host, user, driver text, or password.
    QString failure;
    QSqlDatabase db;

private:
    QString m_name;
};
