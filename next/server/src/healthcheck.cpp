#include "healthcheck.h"

#include "version.h"

#include <QDebug>
#include <QSqlDatabase>
#include <QUuid>

namespace {

QJsonObject baseBody(const char *status, const DatabaseTarget &target)
{
    QJsonObject db;
    db.insert(QStringLiteral("configured"), target.configured);
    db.insert(QStringLiteral("state"), target.configured ? QStringLiteral("down") : QStringLiteral("skipped"));

    QJsonObject body;
    body.insert(QStringLiteral("status"), QLatin1String(status));
    body.insert(QStringLiteral("service"), QStringLiteral("hotel-api"));
    body.insert(QStringLiteral("version"), QStringLiteral(HOTEL_API_VERSION));
    body.insert(QStringLiteral("db"), db);
    return body;
}

} // namespace

HealthReport probeHealth(const DatabaseTarget &target, int connectTimeoutSec)
{
    HealthReport report;
    if (!target.configured) {
        report.httpStatus = 200;
        report.body = baseBody("ok", target);
        return report;
    }

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QMYSQL"))) {
        report.httpStatus = 503;
        report.body = baseBody("degraded", target);
        QJsonObject db = report.body.value(QStringLiteral("db")).toObject();
        db.insert(QStringLiteral("error"), QStringLiteral("driver_not_loaded"));
        report.body.insert(QStringLiteral("db"), db);
        qWarning("database probe failed: driver_not_loaded");
        return report;
    }

    const QString connectionName =
        QStringLiteral("health-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    bool opened = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connectionName);
        database.setHostName(target.host);
        database.setPort(target.port);
        database.setDatabaseName(target.database);
        database.setUserName(target.user);
        database.setPassword(target.password);
        const int timeoutSec = connectTimeoutSec > 0 ? connectTimeoutSec : 3;
        database.setConnectOptions(QStringLiteral("MYSQL_OPT_CONNECT_TIMEOUT=%1").arg(timeoutSec));
        opened = database.open();
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    if (opened) {
        report.httpStatus = 200;
        report.body = baseBody("ok", target);
        QJsonObject db = report.body.value(QStringLiteral("db")).toObject();
        db.insert(QStringLiteral("state"), QStringLiteral("up"));
        report.body.insert(QStringLiteral("db"), db);
        return report;
    }

    report.httpStatus = 503;
    report.body = baseBody("degraded", target);
    QJsonObject db = report.body.value(QStringLiteral("db")).toObject();
    db.insert(QStringLiteral("error"), QStringLiteral("connection_failed"));
    report.body.insert(QStringLiteral("db"), db);
    // QSqlError text can echo the user name. Keep it out of the response and the log.
    qWarning("database probe failed: connection_failed");
    return report;
}
