#include "db.h"

#include <QSqlQuery>
#include <QUuid>

MysqlConnection::MysqlConnection(const DatabaseTarget &target, int connectTimeoutSec)
{
    if (!target.configured) {
        failure = QStringLiteral("database_not_configured");
        return;
    }
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QMYSQL"))) {
        failure = QStringLiteral("driver_not_loaded");
        return;
    }

    m_name = QStringLiteral("mysql-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), m_name);
    db.setHostName(target.host);
    db.setPort(target.port);
    db.setDatabaseName(target.database);
    db.setUserName(target.user);
    db.setPassword(target.password);
    const int timeoutSec = connectTimeoutSec > 0 ? connectTimeoutSec : 3;
    // QMYSQL accepts MYSQL_OPT_CONNECT_TIMEOUT. MYSQL_SET_CHARSET_NAME is not a
    // connect option on this driver (it warns and is ignored). SET NAMES below
    // is the charset switch that matches a utf8 MD5() on the server.
    db.setConnectOptions(QStringLiteral("MYSQL_OPT_CONNECT_TIMEOUT=%1").arg(timeoutSec));
    opened = db.open();
    if (!opened) {
        failure = QStringLiteral("connection_failed");
        // QSqlError::text() can repeat the user name. Drop it.
        db.close();
        return;
    }
    QSqlQuery names(db);
    names.exec(QStringLiteral("SET NAMES utf8mb4"));
}

MysqlConnection::~MysqlConnection()
{
    if (m_name.isEmpty())
        return;
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_name);
}
