#include "httpserver.h"

#include "auth.h"
#include "config.h"
#include "dictionaries.h"
#include "rack.h"
#include "realtimehub.h"
#include "reservations.h"
#include "healthcheck.h"
#include "sessions.h"
#include "version.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QUrlQuery>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QHttpHeaders>
#include <QTcpServer>
#endif

namespace {

// Qt 6.4: QHttpServerResponse::setHeader(name, value).
// Qt 6.8+: that method is gone; headers are a QHttpHeaders value from headers()/setHeaders().
// The JSON constructor already stores Content-Type in that collection, so Cache-Control is added
// on top of the existing headers rather than replacing them.
QHttpServerResponse jsonResponse(const QJsonObject &body, QHttpServerResponse::StatusCode status)
{
    QHttpServerResponse response(body, status);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QHttpHeaders headers = response.headers();
    headers.replaceOrAppend(QHttpHeaders::WellKnownHeader::CacheControl, QByteArrayLiteral("no-store"));
    response.setHeaders(std::move(headers));
#else
    response.setHeader(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store"));
#endif
    return response;
}

QByteArray notFoundPayload(const QHttpServerRequest &request)
{
    QJsonObject body;
    body.insert(QStringLiteral("error"), QStringLiteral("not_found"));
    body.insert(QStringLiteral("path"), request.url().path());
    qInfo().noquote() << "http" << request.url().path() << 404;
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QHttpServerResponse::StatusCode toStatus(int httpStatus)
{
    switch (httpStatus) {
    case 201:
        return QHttpServerResponse::StatusCode::Created;
    case 400:
        return QHttpServerResponse::StatusCode::BadRequest;
    case 401:
        return QHttpServerResponse::StatusCode::Unauthorized;
    case 403:
        return QHttpServerResponse::StatusCode::Forbidden;
    case 404:
        return QHttpServerResponse::StatusCode::NotFound;
    case 409:
        return QHttpServerResponse::StatusCode::Conflict;
    case 422:
        return QHttpServerResponse::StatusCode::UnprocessableEntity;
    case 501:
        return QHttpServerResponse::StatusCode::NotImplemented;
    case 503:
        return QHttpServerResponse::StatusCode::ServiceUnavailable;
    default:
        if (httpStatus >= 400)
            return QHttpServerResponse::StatusCode::InternalServerError;
        return QHttpServerResponse::StatusCode::Ok;
    }
}

// Qt 6.4 stores headers as a list and exposes value(). Qt 6.8+ returns QHttpHeaders.
QByteArray headerValue(const QHttpServerRequest &request, const QByteArray &name)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    const auto view = request.headers().value(name);
    return QByteArray(view.data(), static_cast<qsizetype>(view.size()));
#else
    return request.value(name);
#endif
}

QByteArray authorizationHeader(const QHttpServerRequest &request)
{
    return headerValue(request, QByteArrayLiteral("authorization"));
}

QString requestLocale(const QHttpServerRequest &request)
{
    return localeFromRequest(request.query().queryItemValue(QStringLiteral("lang")),
                             headerValue(request, QByteArrayLiteral("accept-language")));
}

AuthOutcome requireUser(const AppConfig &config, const QHttpServerRequest &request, RouteAccess access)
{
    return authenticate(config.database, config.dbConnectTimeoutSec, authorizationHeader(request), access);
}

} // namespace

HttpApi::HttpApi(AppConfig config)
    : m_config(std::move(config))
{
    m_server.route(QStringLiteral("/health"), QHttpServerRequest::Method::Get, [this]() {
        const HealthReport report = probeHealth(m_config.database, m_config.dbConnectTimeoutSec);
        return jsonResponse(report.body, toStatus(report.httpStatus));
    });

    m_server.route(QStringLiteral("/api/v1"), QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &request) {
                       const AuthOutcome auth = authenticate(m_config.database,
                                                             m_config.dbConnectTimeoutSec,
                                                             authorizationHeader(request),
                                                             RouteAccess::Session);
                       if (!auth.allowed)
                           return jsonResponse(auth.result.body, toStatus(auth.result.httpStatus));
                       QJsonObject body;
                       body.insert(QStringLiteral("service"), QStringLiteral("hotel-api"));
                       body.insert(QStringLiteral("api"), QStringLiteral("v1"));
                       body.insert(QStringLiteral("status"), QStringLiteral("partial"));
                       body.insert(QStringLiteral("version"), QStringLiteral(HOTEL_API_VERSION));
                       return jsonResponse(body, QHttpServerResponse::StatusCode::Ok);
                   });

    m_server.route(QStringLiteral("/api/v1/sessions"),
                   QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &request) {
                       const SessionResult result =
                           createSession(m_config.database, m_config.dbConnectTimeoutSec, request.body());
                       return jsonResponse(result.body, toStatus(result.httpStatus));
                   });

    // Logout is a session route, not a command: a user with commands_allowed
    // false must still be able to revoke the row.
    m_server.route(QStringLiteral("/api/v1/sessions"),
                   QHttpServerRequest::Method::Delete,
                   [this](const QHttpServerRequest &request) {
                       const AuthOutcome auth = authenticate(m_config.database,
                                                             m_config.dbConnectTimeoutSec,
                                                             authorizationHeader(request),
                                                             RouteAccess::Session);
                       if (!auth.allowed)
                           return jsonResponse(auth.result.body, toStatus(auth.result.httpStatus));
                       const ApiResult revoked = revokeSession(m_config.database,
                                                               m_config.dbConnectTimeoutSec,
                                                               auth.principal.sessionId);
                       return jsonResponse(revoked.body, toStatus(revoked.httpStatus));
                   });

    m_server.route(QStringLiteral("/api/v1/sessions/current"),
                   QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &request) {
                       const AuthOutcome auth = authenticate(m_config.database,
                                                             m_config.dbConnectTimeoutSec,
                                                             authorizationHeader(request),
                                                             RouteAccess::Session);
                       if (!auth.allowed)
                           return jsonResponse(auth.result.body, toStatus(auth.result.httpStatus));
                       return jsonResponse(currentSessionBody(auth.principal), QHttpServerResponse::StatusCode::Ok);
                   });

    const auto dictionaryRoute = [this](auto handler) {
        return [this, handler](const QHttpServerRequest &request) {
            const AuthOutcome auth = requireUser(m_config, request, RouteAccess::Session);
            if (!auth.allowed)
                return jsonResponse(auth.result.body, toStatus(auth.result.httpStatus));
            const QString locale = requestLocale(request);
            const ApiResult result = handler(m_config.database, m_config.dbConnectTimeoutSec, auth.principal.propertyId, locale);
            return jsonResponse(result.body, toStatus(result.httpStatus));
        };
    };
    m_server.route(QStringLiteral("/api/v1/reservations"),
                   QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &request) {
                       const AuthOutcome auth = requireUser(m_config, request, RouteAccess::Session);
                       if (!auth.allowed)
                           return jsonResponse(auth.result.body, toStatus(auth.result.httpStatus));
                       const QUrlQuery query = request.query();
                       const ApiResult result = listReservations(m_config.database,
                                                                 m_config.dbConnectTimeoutSec,
                                                                 auth.principal.propertyId,
                                                                 query.queryItemValue(QStringLiteral("from")),
                                                                 query.queryItemValue(QStringLiteral("to")),
                                                                 query.queryItemValue(QStringLiteral("guest")),
                                                                 query.queryItemValue(QStringLiteral("status")),
                                                                 query.queryItemValue(QStringLiteral("room")),
                                                                 query.queryItemValue(QStringLiteral("room_id")));
                       return jsonResponse(result.body, toStatus(result.httpStatus));
                   });
    m_server.route(QStringLiteral("/api/v1/reservations"),
                   QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &request) {
                       const AuthOutcome auth = requireUser(m_config, request, RouteAccess::Command);
                       if (!auth.allowed)
                           return jsonResponse(auth.result.body, toStatus(auth.result.httpStatus));
                       const ApiResult result = createReservation(m_config.database,
                                                                  m_config.dbConnectTimeoutSec,
                                                                  auth.principal.propertyId,
                                                                  auth.principal.userId,
                                                                  request.body());
                       if (result.httpStatus == 201)
                           publishReservation("reservation.created", result);
                       return jsonResponse(result.body, toStatus(result.httpStatus));
                   });
    m_server.route(QStringLiteral("/api/v1/reservations/<arg>"),
                   QHttpServerRequest::Method::Get,
                   [this](const QString &idText, const QHttpServerRequest &request) {
                       const AuthOutcome auth = requireUser(m_config, request, RouteAccess::Session);
                       if (!auth.allowed)
                           return jsonResponse(auth.result.body, toStatus(auth.result.httpStatus));
                       bool ok = false;
                       const qint64 id = idText.toLongLong(&ok);
                       const ApiResult result = getReservation(m_config.database,
                                                               m_config.dbConnectTimeoutSec,
                                                               auth.principal.propertyId,
                                                               ok ? id : 0);
                       return jsonResponse(result.body, toStatus(result.httpStatus));
                   });
    m_server.route(QStringLiteral("/api/v1/reservations/<arg>"),
                   QHttpServerRequest::Method::Patch,
                   [this](const QString &idText, const QHttpServerRequest &request) {
                       const AuthOutcome auth = requireUser(m_config, request, RouteAccess::Command);
                       if (!auth.allowed)
                           return jsonResponse(auth.result.body, toStatus(auth.result.httpStatus));
                       bool ok = false;
                       const qint64 id = idText.toLongLong(&ok);
                       if (!ok || id <= 0) {
                           const ApiResult missing = apiError(404, "reservation_not_found", "reservation was not found");
                           return jsonResponse(missing.body, toStatus(missing.httpStatus));
                       }
                       const ApiResult result = updateReservation(m_config.database,
                                                                  m_config.dbConnectTimeoutSec,
                                                                  auth.principal.propertyId,
                                                                  auth.principal.userId,
                                                                  id,
                                                                  request.body());
                       if (result.httpStatus == 200) {
                           const bool canceled = result.body.value(QStringLiteral("status_code")).toString()
                               == QLatin1String("canceled");
                           publishReservation(canceled ? "reservation.cancelled" : "reservation.updated", result);
                       }
                       return jsonResponse(result.body, toStatus(result.httpStatus));
                   });

    m_server.route(QStringLiteral("/api/v1/rack"),
                   QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &request) {
                       const AuthOutcome auth = requireUser(m_config, request, RouteAccess::Session);
                       if (!auth.allowed)
                           return jsonResponse(auth.result.body, toStatus(auth.result.httpStatus));
                       const ApiResult result = occupancyChart(m_config.database,
                                                               m_config.dbConnectTimeoutSec,
                                                               auth.principal.propertyId,
                                                               requestLocale(request),
                                                               request.query().queryItemValue(QStringLiteral("from")),
                                                               request.query().queryItemValue(QStringLiteral("to")));
                       return jsonResponse(result.body, toStatus(result.httpStatus));
                   });

    m_server.route(QStringLiteral("/api/v1/rooms"), QHttpServerRequest::Method::Get, dictionaryRoute(listRooms));
    m_server.route(QStringLiteral("/api/v1/room-types"), QHttpServerRequest::Method::Get, dictionaryRoute(listRoomTypes));
    m_server.route(QStringLiteral("/api/v1/buildings"), QHttpServerRequest::Method::Get, dictionaryRoute(listBuildings));

    using CreateFn = ApiResult (*)(const DatabaseTarget &, int, qint64, const QByteArray &);
    using UpdateFn = ApiResult (*)(const DatabaseTarget &, int, qint64, qint64, const QByteArray &);
    using DeleteFn = ApiResult (*)(const DatabaseTarget &, int, qint64, qint64);
    const auto postDictionary = [this](const QString &path, CreateFn handler, const char *dictionary) {
        m_server.route(path, QHttpServerRequest::Method::Post, [this, handler, dictionary](const QHttpServerRequest &request) {
            const AuthOutcome auth = requireUser(m_config, request, RouteAccess::Command);
            if (!auth.allowed)
                return jsonResponse(auth.result.body, toStatus(auth.result.httpStatus));
            const ApiResult result = handler(m_config.database, m_config.dbConnectTimeoutSec, auth.principal.propertyId, request.body());
            if (result.httpStatus == 201)
                publishDictionary(dictionary, "created", result.body.value(QStringLiteral("id")).toInteger());
            return jsonResponse(result.body, toStatus(result.httpStatus));
        });
    };
    const auto patchDictionary = [this](const QString &path, UpdateFn handler, const char *dictionary, const char *missingCode) {
        m_server.route(path,
                       QHttpServerRequest::Method::Patch,
                       [this, handler, dictionary, missingCode](const QString &idText, const QHttpServerRequest &request) {
                           const AuthOutcome auth = requireUser(m_config, request, RouteAccess::Command);
                           if (!auth.allowed)
                               return jsonResponse(auth.result.body, toStatus(auth.result.httpStatus));
                           bool ok = false;
                           const qint64 id = idText.toLongLong(&ok);
                           if (!ok || id <= 0) {
                               const ApiResult missing = apiError(404, missingCode, "not found");
                               return jsonResponse(missing.body, toStatus(missing.httpStatus));
                           }
                           const ApiResult result = handler(m_config.database,
                                                            m_config.dbConnectTimeoutSec,
                                                            auth.principal.propertyId,
                                                            id,
                                                            request.body());
                           if (result.httpStatus == 200)
                               publishDictionary(dictionary, "updated", id);
                           return jsonResponse(result.body, toStatus(result.httpStatus));
                       });
    };
    const auto deleteDictionary = [this](const QString &path, DeleteFn handler, const char *dictionary, const char *missingCode) {
        m_server.route(path,
                       QHttpServerRequest::Method::Delete,
                       [this, handler, dictionary, missingCode](const QString &idText, const QHttpServerRequest &request) {
                           const AuthOutcome auth = requireUser(m_config, request, RouteAccess::Command);
                           if (!auth.allowed)
                               return jsonResponse(auth.result.body, toStatus(auth.result.httpStatus));
                           bool ok = false;
                           const qint64 id = idText.toLongLong(&ok);
                           if (!ok || id <= 0) {
                               const ApiResult missing = apiError(404, missingCode, "not found");
                               return jsonResponse(missing.body, toStatus(missing.httpStatus));
                           }
                           const ApiResult result =
                               handler(m_config.database, m_config.dbConnectTimeoutSec, auth.principal.propertyId, id);
                           if (result.httpStatus == 200)
                               publishDictionary(dictionary, "deleted", id);
                           return jsonResponse(result.body, toStatus(result.httpStatus));
                       });
    };
    postDictionary(QStringLiteral("/api/v1/room-types"), createRoomType, "room_types");
    patchDictionary(QStringLiteral("/api/v1/room-types/<arg>"), updateRoomType, "room_types", "room_type_not_found");
    deleteDictionary(QStringLiteral("/api/v1/room-types/<arg>"), deleteRoomType, "room_types", "room_type_not_found");
    postDictionary(QStringLiteral("/api/v1/buildings"), createBuilding, "buildings");
    patchDictionary(QStringLiteral("/api/v1/buildings/<arg>"), updateBuilding, "buildings", "building_not_found");
    deleteDictionary(QStringLiteral("/api/v1/buildings/<arg>"), deleteBuilding, "buildings", "building_not_found");
    postDictionary(QStringLiteral("/api/v1/rooms"), createRoom, "rooms");
    patchDictionary(QStringLiteral("/api/v1/rooms/<arg>"), updateRoom, "rooms", "room_not_found");
    deleteDictionary(QStringLiteral("/api/v1/rooms/<arg>"), deleteRoom, "rooms", "room_not_found");

    m_server.route(QStringLiteral("/api/v1/room-statuses"),
                   QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &request) {
                       const AuthOutcome auth = requireUser(m_config, request, RouteAccess::Session);
                       if (!auth.allowed)
                           return jsonResponse(auth.result.body, toStatus(auth.result.httpStatus));
                       const ApiResult result = listRoomStatuses(requestLocale(request));
                       return jsonResponse(result.body, toStatus(result.httpStatus));
                   });

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // 6.8 replaced HeaderList with QHttpHeaders, and the responder argument is an lvalue.
    // setMissingHandler / addAfterRequestHandler also require a QObject context.
    m_server.setMissingHandler(&m_server, [](const QHttpServerRequest &request, QHttpServerResponder &responder) {
        const QByteArray payload = notFoundPayload(request);
        QHttpHeaders headers;
        headers.append(QHttpHeaders::WellKnownHeader::ContentType, QByteArrayLiteral("application/json"));
        headers.append(QHttpHeaders::WellKnownHeader::CacheControl, QByteArrayLiteral("no-store"));
        responder.write(payload, headers, QHttpServerResponder::StatusCode::NotFound);
    });
    m_server.addAfterRequestHandler(&m_server, [](const QHttpServerRequest &request, QHttpServerResponse &response) {
        qInfo().noquote() << "http" << request.url().path() << static_cast<int>(response.statusCode());
    });
#else
    m_server.setMissingHandler([](const QHttpServerRequest &request, QHttpServerResponder &&responder) {
        const QByteArray payload = notFoundPayload(request);
        responder.write(payload,
                        {{QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")},
                         {QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store")}},
                        QHttpServerResponder::StatusCode::NotFound);
    });
    m_server.afterRequest([](QHttpServerResponse &&response, const QHttpServerRequest &request) {
        qInfo().noquote() << "http" << request.url().path() << static_cast<int>(response.statusCode());
        return std::move(response);
    });
#endif
}

void HttpApi::setRealtime(RealtimeHub *hub)
{
    m_hub = hub;
}

void HttpApi::publishDictionary(const char *dictionary, const char *action, qint64 id)
{
    if (!m_hub || id <= 0)
        return;
    QJsonObject event;
    event.insert(QStringLiteral("type"), QStringLiteral("dictionary.changed"));
    event.insert(QStringLiteral("dictionary"), QLatin1String(dictionary));
    event.insert(QStringLiteral("action"), QLatin1String(action));
    event.insert(QStringLiteral("id"), QJsonValue(id));
    QMetaObject::invokeMethod(m_hub,
                              "publish",
                              Qt::QueuedConnection,
                              Q_ARG(QByteArray, QJsonDocument(event).toJson(QJsonDocument::Compact)));
}

void HttpApi::publishReservation(const char *type, const ApiResult &result)
{
    if (!m_hub || result.httpStatus >= 400 || !result.body.contains(QStringLiteral("id")))
        return;
    QJsonObject event;
    event.insert(QStringLiteral("type"), QLatin1String(type));
    event.insert(QStringLiteral("reservation_id"), result.body.value(QStringLiteral("id")));
    event.insert(QStringLiteral("status_code"), result.body.value(QStringLiteral("status_code")));
    const QJsonArray stays = result.body.value(QStringLiteral("stays")).toArray();
    const QJsonObject stay = stays.isEmpty() ? QJsonObject() : stays.at(0).toObject();
    if (!stay.isEmpty()) {
        event.insert(QStringLiteral("stay_id"), stay.value(QStringLiteral("id")));
        event.insert(QStringLiteral("room_id"), stay.value(QStringLiteral("room_id")));
        event.insert(QStringLiteral("state_code"), stay.value(QStringLiteral("state_code")));
    }
    const auto send = [this](const QJsonObject &body) {
        QMetaObject::invokeMethod(m_hub,
                                  "publish",
                                  Qt::QueuedConnection,
                                  Q_ARG(QByteArray, QJsonDocument(body).toJson(QJsonDocument::Compact)));
    };
    send(event);
    const QString state = stay.value(QStringLiteral("state_code")).toString();
    if (state == QLatin1String("in_house") || state == QLatin1String("checked_out")) {
        QJsonObject room;
        room.insert(QStringLiteral("type"), QStringLiteral("room.status_changed"));
        room.insert(QStringLiteral("room_id"), stay.value(QStringLiteral("room_id")));
        room.insert(QStringLiteral("status_code"),
                    state == QLatin1String("in_house") ? QStringLiteral("occupied") : QStringLiteral("vacant_dirty"));
        send(room);
    }
}

bool HttpApi::listen(QString *errorMessage)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // QAbstractHttpServer::listen(QHostAddress, port) was removed in 6.8.
    // Listen on a QTcpServer, then hand it to bind(). The HTTP server takes ownership.
    auto *tcpServer = new QTcpServer(&m_server);
    if (!tcpServer->listen(m_config.http.address, m_config.http.port) || !m_server.bind(tcpServer)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("failed to bind HTTP %1:%2 (%3)")
                                .arg(m_config.http.address.toString())
                                .arg(m_config.http.port)
                                .arg(tcpServer->errorString());
        }
        return false;
    }
    m_port = tcpServer->serverPort();
#else
    const quint16 bound = m_server.listen(m_config.http.address, m_config.http.port);
    if (bound == 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("failed to bind HTTP %1:%2")
                                .arg(m_config.http.address.toString())
                                .arg(m_config.http.port);
        }
        return false;
    }
    m_port = bound;
#endif
    qInfo().noquote() << "hotel-api http" << m_config.http.address.toString() << m_port;
    qInfo().noquote() << "hotel-api database" << databaseStartupDetail(m_config.database);
    return true;
}

quint16 HttpApi::port() const
{
    return m_port;
}
