#pragma once

#include "apiclient.h"

#include <QObject>
#include <QTimer>

class QEvent;
class QWebSocket;

// Periodic GET /health, plus an optional WebSocket hello when a URL is set.
class HealthMonitor : public QObject {
    Q_OBJECT

public:
    explicit HealthMonitor(ApiClient *api, QObject *parent = nullptr);
    ~HealthMonitor() override;

    void setWebSocketUrl(const QString &url);
    void start();
    void stop();
    void refreshNow();
    // Drop the socket and open it again. Used after sign-in, when a token exists.
    void reconnectNow();

    QString healthText() const;
    QString socketText() const;

signals:
    void statusChanged();
    void hotelEvent(const QString &type);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class SocketPhase {
        NotConfigured,
        Connecting,
        InvalidAddress,
        WaitingHello,
        NotSignedIn,
        ConnectedHello,
        Connected,
        Down
    };

    void poll();
    void onHealth(const HealthStatus &status);
    void ensureSocket();
    void closeSocket();
    void markSocketDown();
    void retranslate();
    QString socketPhrase() const;

    ApiClient *m_api = nullptr;
    QTimer m_timer;
    QTimer m_reconnect;
    QString m_wsUrl;
    QString m_healthText;
    QString m_socketText;
    HealthStatus m_lastHealth;
    QWebSocket *m_socket = nullptr;
    SocketPhase m_phase = SocketPhase::NotConfigured;
    bool m_running = false;
    bool m_haveHealth = false;
    bool m_helloSeen = false;
    int m_backoffMs = 1000;
};
