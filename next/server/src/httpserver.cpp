#include "httpserver.h"

#include "config.h"
#include "healthcheck.h"
#include "sessions.h"
#include "version.h"

#include <QJsonDocument>
#include <QJsonObject>
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
    case 400:
        return QHttpServerResponse::StatusCode::BadRequest;
    case 401:
        return QHttpServerResponse::StatusCode::Unauthorized;
    case 404:
        return QHttpServerResponse::StatusCode::NotFound;
    case 501:
        return QHttpServerResponse::StatusCode::NotImplemented;
    case 503:
        return QHttpServerResponse::StatusCode::ServiceUnavailable;
    default:
        return QHttpServerResponse::StatusCode::Ok;
    }
}

} // namespace

HttpApi::HttpApi(AppConfig config)
    : m_config(std::move(config))
{
    m_server.route(QStringLiteral("/health"), QHttpServerRequest::Method::Get, [this]() {
        const HealthReport report = probeHealth(m_config.database, m_config.dbConnectTimeoutSec);
        return jsonResponse(report.body, toStatus(report.httpStatus));
    });

    m_server.route(QStringLiteral("/api/v1"), QHttpServerRequest::Method::Get, []() {
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
