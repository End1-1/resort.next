#pragma once

#include "auth.h"
#include "config.h"

#include <QByteArray>
#include <QString>

// hy, en, or ru. Query wins. Then the first Accept-Language tag we know.
// Anything else is ru. Closed codes (room status) are not translated here.
QString localeFromRequest(const QString &langQuery, const QByteArray &acceptLanguage);

// Read-only lists for one property. Names come from nx_label
// (requested locale, then ru, then the 0002 name column, then code).
ApiResult listRooms(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, const QString &locale);
ApiResult listRoomTypes(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, const QString &locale);
ApiResult listBuildings(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, const QString &locale);

// Known nx_room.status_code values. The name is not in the JSON; the client translates the code.
ApiResult listRoomStatuses(const QString &locale);
