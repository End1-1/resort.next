#pragma once

#include "auth.h"
#include "config.h"

#include <QDate>
#include <QString>

// Night window [from, to). to is the morning the last night ends, not a night
// that is still occupied. At most 120 nights. Invalid input does not touch the database.
struct RackRange {
    bool ok = false;
    QDate from;
    QDate to;
    int httpStatus = 400;
    const char *code = "invalid_range";
    const char *message = "from and to must be YYYY-MM-DD dates with to after from";
};

RackRange parseRackRange(const QString &fromText, const QString &toText);

// Rooms of the signed-in property, ordered by building code (empty first),
// then room code, then id — the legacy chart order (f_building, f_id).
// Stay blocks that overlap the window are attached.
// A stay occupies [arrival, departure). Canceled stays and canceled reservations
// are omitted. Stays with no room are omitted. Guest name is the primary
// stay guest, otherwise the first guest: last name, then first name.
ApiResult occupancyChart(const DatabaseTarget &target,
                         int connectTimeoutSec,
                         qint64 propertyId,
                         const QString &locale,
                         const QString &fromText,
                         const QString &toText);
