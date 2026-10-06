#pragma once

#include "apiclient.h"

#include <QObject>
#include <QTimer>

class QWebSocket;

// Periodic GET /health, plus an optional WebSocket hello when a URL is set.
class HealthMonitor : public QObject {
    Q_OBJECT

public:
    explicit HealthMonitor(ApiClient *api, QObject *parent = nullptr);

    void setWebSocketUrl(const QString &url);
    void start();
    void stop();
    void refreshNow();

    QString healthText() const;
    QString socketText() const;

signals:
    void statusChanged();

private:
    void poll();
    void onHealth(const HealthStatus &status);
    void ensureSocket();
    void closeSocket();
    void markSocketDown();

    ApiClient *m_api = nullptr;
    QTimer m_timer;
    QString m_wsUrl;
    QString m_healthText;
    QString m_socketText;
    QWebSocket *m_socket = nullptr;
    bool m_running = false;
    bool m_helloSeen = false;
};
