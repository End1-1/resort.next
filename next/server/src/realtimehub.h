#pragma once

#include "config.h"

#include <QList>
#include <QObject>

class QWebSocket;
class QWebSocketServer;

// Optional hint channel. Started only when HOTEL_WS_LISTEN is set.
// Clients connect to ws://<listen>/api/v1/ws. No PMS events yet.
class RealtimeHub : public QObject {
public:
    explicit RealtimeHub(QObject *parent = nullptr);

    bool listen(const ListenEndpoint &endpoint, QString *errorMessage);

private:
    void acceptPending();

    QWebSocketServer *m_server = nullptr;
    QList<QWebSocket *> m_clients;
};
