#pragma once

#include "config.h"

#include <QJsonObject>

struct HealthReport {
    int httpStatus = 200;
    QJsonObject body;
};

// Opens QMYSQL only when target.configured. Never copies the DSN into body.
HealthReport probeHealth(const DatabaseTarget &target, int connectTimeoutSec);
