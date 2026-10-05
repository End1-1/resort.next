#pragma once

#include "config.h"

#include <QByteArray>
#include <QJsonObject>

struct SessionResult {
    int httpStatus = 500;
    QJsonObject body;
};

// POST /api/v1/sessions. Verifies users.f_password as legacy MD5.
// Does not write the hash back (the desktop still compares MD5).
SessionResult createSession(const DatabaseTarget &target, int connectTimeoutSec, const QByteArray &body);
