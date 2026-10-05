#include "httpserver.h"

#include "healthcheck.h"
#include "version.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace {

QHttpServerResponse jsonResponse(const QJsonObject &body, QHttpServerResponse::StatusCode status)
{
    QHttpServerResponse response(body, status);
    response.setHeader(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store"));
    return response;
}

QHttpServerResponse::StatusCode toStatus(int httpStatus)
{
    if (httpStatus == 503)
        return QHttpServerResponse::StatusCode::ServiceUnavailable;
    return QHttpServerResponse::StatusCode::Ok;
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
        body.insert(QStringLiteral("status"), QStringLiteral("skeleton"));
        body.insert(QStringLiteral("version"), QStringLiteral(HOTEL_API_VERSION));
        return jsonResponse(body, QHttpServerResponse::StatusCode::Ok);
    });

    m_server.route(QStringLiteral("/api/v1/sessions"), QHttpServerRequest::Method::Post, []() {
        QJsonObject body;
        body.insert(QStringLiteral("error"), QStringLiteral("not_implemented"));
        body.insert(QStringLiteral("message"),
                    QStringLiteral("session auth is a later phase; this build does not check or store passwords"));
        return jsonResponse(body, QHttpServerResponse::StatusCode::NotImplemented);
    });

    m_server.setMissingHandler([](const QHttpServerRequest &request, QHttpServerResponder &&responder) {
        QJsonObject body;
        body.insert(QStringLiteral("error"), QStringLiteral("not_found"));
        body.insert(QStringLiteral("path"), request.url().path());
        const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
        qInfo().noquote() << "http" << request.url().path() << 404;
        responder.write(payload,
                        {{QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")},
                         {QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store")}},
                        QHttpServerResponder::StatusCode::NotFound);
    });

    m_server.afterRequest([](QHttpServerResponse &&response, const QHttpServerRequest &request) {
        qInfo().noquote() << "http" << request.url().path() << static_cast<int>(response.statusCode());
        return std::move(response);
    });
}

bool HttpApi::listen(QString *errorMessage)
{
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
    qInfo().noquote() << "hotel-api http" << m_config.http.address.toString() << m_port;
    qInfo().noquote() << "hotel-api database" << (m_config.database.configured ? "configured" : "not configured");
    return true;
}

quint16 HttpApi::port() const
{
    return m_port;
}
