#pragma once

#include "config.h"

#include <QSqlDatabase>
#include <QString>

// One QMYSQL connection for the calling thread. The destructor closes it and
// removes the named connection. Callers must destroy QSqlQuery objects first.
class MysqlConnection {
public:
    MysqlConnection(const DatabaseTarget &target, int connectTimeoutSec);
    ~MysqlConnection();

    MysqlConnection(const MysqlConnection &) = delete;
    MysqlConnection &operator=(const MysqlConnection &) = delete;

    bool opened = false;
    // database_not_configured, driver_not_loaded, or connection_failed.
    // Never a DSN, user, or password.
    QString failure;
    QSqlDatabase db;

private:
    QString m_name;
};
