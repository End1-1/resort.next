#pragma once

#include "auth.h"
#include "config.h"

#include <QByteArray>
#include <QString>

// One property. Dates on a stay are nights [arrival, departure): departure is
// the morning the room is free, and it must be after arrival.
// Overlap is rejected for stays in reserved, in_house, out_of_order, or
// out_of_inventory. canceled and checked_out do not block the room.
// Writes require the caller to have already passed RouteAccess::Command.

ApiResult listReservations(const DatabaseTarget &target,
                           int connectTimeoutSec,
                           qint64 propertyId,
                           const QString &fromText,
                           const QString &toText,
                           const QString &guest,
                           const QString &status,
                           const QString &roomCode,
                           const QString &roomIdText);

ApiResult getReservation(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, qint64 reservationId);

ApiResult createReservation(const DatabaseTarget &target,
                            int connectTimeoutSec,
                            qint64 propertyId,
                            qint64 userId,
                            const QByteArray &body);

ApiResult updateReservation(const DatabaseTarget &target,
                            int connectTimeoutSec,
                            qint64 propertyId,
                            qint64 userId,
                            qint64 reservationId,
                            const QByteArray &body);
