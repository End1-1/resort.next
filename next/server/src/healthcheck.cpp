#include "healthcheck.h"

#include "db.h"
#include "version.h"

#include <QDebug>

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

    const MysqlConnection connection(target, connectTimeoutSec);
    if (connection.opened) {
        report.httpStatus = 200;
        report.body = baseBody("ok", target);
        QJsonObject db = report.body.value(QStringLiteral("db")).toObject();
        db.insert(QStringLiteral("state"), QStringLiteral("up"));
        report.body.insert(QStringLiteral("db"), db);
        return report;
    }

    const QString code = connection.failure == QLatin1String("driver_not_loaded")
                             ? QStringLiteral("driver_not_loaded")
                             : QStringLiteral("connection_failed");
    report.httpStatus = 503;
    report.body = baseBody("degraded", target);
    QJsonObject db = report.body.value(QStringLiteral("db")).toObject();
    db.insert(QStringLiteral("error"), code);
    report.body.insert(QStringLiteral("db"), db);
    // QSqlError text can echo the user name. Keep it out of the response and the log.
    qWarning("database probe failed: %s", qPrintable(code));
    return report;
}
