#include "realtimehub.h"

#include "auth.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QWebSocket>
#include <QWebSocketCorsAuthenticator>
#include <QWebSocketServer>

namespace {

bool originAllowed(const QString &origin)
{
    if (origin.isEmpty())
        return true;
    const QString lower = origin.toLower();
    return lower.startsWith(QLatin1String("http://127.0.0.1"))
        || lower.startsWith(QLatin1String("http://localhost"))
        || lower.startsWith(QLatin1String("https://127.0.0.1"))
        || lower.startsWith(QLatin1String("https://localhost"));
}

} // namespace

RealtimeHub::RealtimeHub(QObject *parent)
    : QObject(parent)
{
}

bool RealtimeHub::listen(const ListenEndpoint &endpoint,
                         const DatabaseTarget &database,
                         int connectTimeoutSec,
                         QString *errorMessage)
{
    m_database = database;
    m_connectTimeoutSec = connectTimeoutSec;
    m_server = new QWebSocketServer(QStringLiteral("hotel-api"), QWebSocketServer::NonSecureMode, this);
    if (!m_server->listen(endpoint.address, endpoint.port)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("failed to bind WebSocket %1:%2 (%3)")
                                .arg(endpoint.address.toString())
                                .arg(endpoint.port)
                                .arg(m_server->errorString());
        }
        return false;
    }

    QObject::connect(m_server, &QWebSocketServer::originAuthenticationRequired, this,
                     [](QWebSocketCorsAuthenticator *authenticator) {
                         authenticator->setAllowed(originAllowed(authenticator->origin()));
                     });
    QObject::connect(m_server, &QWebSocketServer::newConnection, this, [this]() { acceptPending(); });
    qInfo().noquote() << "hotel-api websocket" << endpoint.address.toString() << m_server->serverPort()
                      << "path /api/v1/ws";
    return true;
}

void RealtimeHub::publish(const QByteArray &json)
{
    const QString text = QString::fromUtf8(json);
    for (QWebSocket *socket : std::as_const(m_clients)) {
        if (socket && socket->state() == QAbstractSocket::ConnectedState)
            socket->sendTextMessage(text);
    }
}

void RealtimeHub::reject(QWebSocket *socket)
{
    m_clients.removeAll(socket);
    socket->close(QWebSocketProtocol::CloseCodePolicyViolated, QStringLiteral("unauthorized"));
    socket->deleteLater();
}

bool RealtimeHub::authorize(QWebSocket *socket, const QByteArray &token)
{
    if (socket->property("authed").toBool())
        return true;
    const QByteArray header = QByteArrayLiteral("Bearer ") + token;
    const AuthOutcome auth = authenticate(m_database, m_connectTimeoutSec, header, RouteAccess::Session);
    if (!auth.allowed) {
        reject(socket);
        return false;
    }
    socket->setProperty("authed", true);
    if (!m_clients.contains(socket))
        m_clients.append(socket);
    socket->sendTextMessage(QStringLiteral("{\"type\":\"hello\"}"));
    return true;
}

void RealtimeHub::acceptPending()
{
    while (m_server && m_server->hasPendingConnections()) {
        QWebSocket *socket = m_server->nextPendingConnection();
        if (!socket)
            return;
        if (socket->requestUrl().path() != QLatin1String("/api/v1/ws")) {
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated, QStringLiteral("expected /api/v1/ws"));
            socket->deleteLater();
            continue;
        }

        socket->setParent(this);
        QObject::connect(socket, &QWebSocket::disconnected, this, [this, socket]() {
            m_clients.removeAll(socket);
            socket->deleteLater();
        });
        QObject::connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &message) {
            if (socket->property("authed").toBool())
                return;
            const QJsonObject body = QJsonDocument::fromJson(message.toUtf8()).object();
            if (body.value(QStringLiteral("type")).toString() != QLatin1String("auth")) {
                reject(socket);
                return;
            }
            authorize(socket, body.value(QStringLiteral("token")).toString().toLatin1());
        });

        const QByteArray queryToken = QUrlQuery(socket->requestUrl()).queryItemValue(QStringLiteral("token")).toLatin1();
        if (!queryToken.isEmpty())
            authorize(socket, queryToken);
    }
}
