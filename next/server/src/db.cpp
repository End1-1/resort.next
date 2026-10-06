#include "db.h"

#include "dberror.h"

#include <QDebug>
#include <QSqlError>
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
    // MYSQL_SET_CHARSET_NAME is not a connect option on this driver (it warns
    // and is ignored). SET NAMES below is the charset switch. Host 127.0.0.1
    // is TCP; on Windows "localhost" can be a named pipe and a different
    // MariaDB account. The options string does not change that.
    db.setConnectOptions(mysqlConnectOptions(connectTimeoutSec, target.sslMode, target.sslCa));
    opened = db.open();
    if (!opened) {
        // Capture the error before close(). close() can drop lastError().
        const QSqlError error = db.lastError();
        const QString native = error.nativeErrorCode();
        failure = publicDatabaseErrorCode(native);
        const QString driver = scrubDatabaseMessage(error.driverText(), target.password);
        const QString server = scrubDatabaseMessage(error.databaseText(), target.password);
        qWarning().noquote() << formatConnectFailureLog(failure,
                                                        native,
                                                        target.host,
                                                        target.port,
                                                        target.database,
                                                        target.user,
                                                        driver,
                                                        server);
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
