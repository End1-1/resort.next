#include "healthcheck.h"

#include "db.h"
#include "dberror.h"
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

    const QString code = probeDatabaseError(connection.failure);
    report.httpStatus = 503;
    report.body = baseBody("degraded", target);
    QJsonObject db = report.body.value(QStringLiteral("db")).toObject();
    db.insert(QStringLiteral("error"), code);
    report.body.insert(QStringLiteral("db"), db);
    // Driver text is logged once in MysqlConnection, with the password removed.
    // This line is only the machine code.
    qWarning("database probe failed: %s", qPrintable(code));
    return report;
}
