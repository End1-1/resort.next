#pragma once

#include "config.h"

#include <QByteArray>
#include <QList>
#include <QObject>

class QWebSocket;
class QWebSocketServer;

// Optional hint channel. Started only when HOTEL_WS_LISTEN is set.
// Clients connect to ws://<listen>/api/v1/ws and must authenticate before
// any event, including hello. See next/docs/ws-events.md.
class RealtimeHub : public QObject {
    Q_OBJECT

public:
    explicit RealtimeHub(QObject *parent = nullptr);

    bool listen(const ListenEndpoint &endpoint, const DatabaseTarget &database, int connectTimeoutSec, QString *errorMessage);

public slots:
    // JSON text for every authenticated socket. Safe to invoke queued.
    void publish(const QByteArray &json);

private:
    void acceptPending();
    bool authorize(QWebSocket *socket, const QByteArray &token);
    void reject(QWebSocket *socket);

    DatabaseTarget m_database;
    int m_connectTimeoutSec = 3;
    QWebSocketServer *m_server = nullptr;
    QList<QWebSocket *> m_clients;
};
