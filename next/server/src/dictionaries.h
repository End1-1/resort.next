#pragma once

#include "auth.h"
#include "config.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

// hy, en, or ru. Query wins. Then the first Accept-Language tag we know.
// Anything else is ru. Closed codes (room status) are not translated here.
QString localeFromRequest(const QString &langQuery, const QByteArray &acceptLanguage);

// Read-only lists for one property. Names come from nx_label
// (requested locale, then ru, then the 0002 name column, then code).
// Room types and buildings also return names.hy/en/ru and version (0005).
ApiResult listRooms(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, const QString &locale);
ApiResult listRoomTypes(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, const QString &locale);
ApiResult listBuildings(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, const QString &locale);

// Known nx_room.status_code values. The name is not in the JSON; the client translates the code.
// This set is not a dictionary table and has no create/update/delete route.
ApiResult listRoomStatuses(const QString &locale);
bool knownRoomStatus(const QString &code);

// Body of POST/PATCH for nx_room_type and nx_building. All three names are required.
// version is read only when requireVersion is true (PATCH).
struct NamedWrite {
    QString code;
    QString hy;
    QString en;
    QString ru;
    int version = 0;
};

// Body of POST/PATCH for nx_room. property_id in the JSON is ignored.
struct RoomWrite {
    QString code;
    qint64 roomTypeId = 0;
    bool hasBuilding = false;
    qint64 buildingId = 0;
    bool hasFloor = false;
    int floor = 0;
    bool hasPhone = false;
    QString phone;
    QString statusCode;
    bool doNotDisturb = false;
    int version = 0;
};

// False leaves *error set. Does not touch the database.
bool parseNamedWrite(const QJsonObject &body, bool requireVersion, NamedWrite *out, ApiResult *error);
bool parseRoomWrite(const QJsonObject &body, bool requireVersion, RoomWrite *out, ApiResult *error);

// Writes are scoped to propertyId (the session user's property). The caller has
// already passed RouteAccess::Command. Deletes are refused with in_use when a
// room, or a stay, still points at the row. There is no deactivate column.
ApiResult createRoomType(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, const QByteArray &body);
ApiResult updateRoomType(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, qint64 id, const QByteArray &body);
ApiResult deleteRoomType(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, qint64 id);

ApiResult createBuilding(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, const QByteArray &body);
ApiResult updateBuilding(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, qint64 id, const QByteArray &body);
ApiResult deleteBuilding(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, qint64 id);

ApiResult createRoom(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, const QByteArray &body);
ApiResult updateRoom(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, qint64 id, const QByteArray &body);
ApiResult deleteRoom(const DatabaseTarget &target, int connectTimeoutSec, qint64 propertyId, qint64 id);
