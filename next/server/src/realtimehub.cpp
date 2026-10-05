#include "realtimehub.h"

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

bool RealtimeHub::listen(const ListenEndpoint &endpoint, QString *errorMessage)
{
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

void RealtimeHub::acceptPending()
{
    while (m_server->hasPendingConnections()) {
        QWebSocket *socket = m_server->nextPendingConnection();
        if (!socket)
            return;

        if (socket->requestUrl().path() != QLatin1String("/api/v1/ws")) {
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated, QStringLiteral("expected /api/v1/ws"));
            socket->deleteLater();
            continue;
        }

        socket->setParent(this);
        m_clients.append(socket);
        QObject::connect(socket, &QWebSocket::disconnected, this, [this, socket]() {
            m_clients.removeAll(socket);
            socket->deleteLater();
        });
        socket->sendTextMessage(QStringLiteral("{\"type\":\"hello\",\"events\":\"not_implemented\"}"));
    }
}
