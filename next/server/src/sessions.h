#pragma once

#include "config.h"

#include <QByteArray>
#include <QJsonObject>

struct SessionResult {
    int httpStatus = 500;
    QJsonObject body;
};

// POST /api/v1/sessions. Verifies nx_user.password_hash when password_scheme is md5.
// Does not read the legacy users table and does not write the hash back.
SessionResult createSession(const DatabaseTarget &target, int connectTimeoutSec, const QByteArray &body);
