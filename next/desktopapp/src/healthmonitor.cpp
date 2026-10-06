#include "healthmonitor.h"

#include <QAbstractSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QWebSocket>

namespace {

constexpr int kPollMs = 15000;

} // namespace

HealthMonitor::HealthMonitor(ApiClient *api, QObject *parent)
    : QObject(parent)
    , m_api(api)
    , m_healthText(QStringLiteral("Проверка соединения…"))
    , m_socketText(QStringLiteral("WebSocket: не настроен"))
{
    connect(m_api, &ApiClient::healthFinished, this, &HealthMonitor::onHealth);
    m_timer.setInterval(kPollMs);
    connect(&m_timer, &QTimer::timeout, this, &HealthMonitor::poll);
}

void HealthMonitor::setWebSocketUrl(const QString &url)
{
    if (m_wsUrl == url)
        return;
    m_wsUrl = url;
    m_helloSeen = false;
    closeSocket();
    m_socketText = m_wsUrl.isEmpty() ? QStringLiteral("WebSocket: не настроен")
                                     : QStringLiteral("WebSocket: подключение…");
    emit statusChanged();
    if (m_running)
        ensureSocket();
}

void HealthMonitor::start()
{
    m_running = true;
    poll();
    m_timer.start();
}

void HealthMonitor::stop()
{
    m_running = false;
    m_timer.stop();
    closeSocket();
}

void HealthMonitor::refreshNow()
{
    if (!m_running)
        return;
    m_api->requestHealth();
    ensureSocket();
}

QString HealthMonitor::healthText() const
{
    return m_healthText;
}

QString HealthMonitor::socketText() const
{
    return m_socketText;
}

void HealthMonitor::poll()
{
    if (!m_running)
        return;
    m_api->requestHealth();
    ensureSocket();
}

void HealthMonitor::onHealth(const HealthStatus &status)
{
    if (!status.current)
        return;
    m_healthText = healthSummary(status);
    emit statusChanged();
}

void HealthMonitor::closeSocket()
{
    if (!m_socket)
        return;
    QWebSocket *socket = m_socket;
    m_socket = nullptr;
    socket->disconnect(this);
    socket->close();
    socket->deleteLater();
}

void HealthMonitor::markSocketDown()
{
    if (m_wsUrl.isEmpty())
        return;
    m_helloSeen = false;
    m_socketText = QStringLiteral("WebSocket: нет соединения");
    emit statusChanged();
}

void HealthMonitor::ensureSocket()
{
    if (m_wsUrl.isEmpty())
        return;
    if (m_socket) {
        const QAbstractSocket::SocketState state = m_socket->state();
        if (state == QAbstractSocket::ConnectedState || state == QAbstractSocket::ConnectingState)
            return;
        closeSocket();
    }

    const QUrl url(m_wsUrl);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty()
        || (scheme != QLatin1String("ws") && scheme != QLatin1String("wss"))) {
        m_socketText = QStringLiteral("WebSocket: некорректный адрес");
        emit statusChanged();
        return;
    }

    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_socket, &QWebSocket::connected, this, [this]() {
        m_socketText = QStringLiteral("WebSocket: соединение установлено, ждём hello");
        emit statusChanged();
    });
    connect(m_socket, &QWebSocket::textMessageReceived, this, [this](const QString &message) {
        const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8());
        const bool hello = document.isObject()
            && document.object().value(QStringLiteral("type")).toString() == QLatin1String("hello");
        m_helloSeen = hello;
        m_socketText = hello ? QStringLiteral("WebSocket: подключено (hello)")
                             : QStringLiteral("WebSocket: подключено");
        emit statusChanged();
    });
    connect(m_socket, &QWebSocket::disconnected, this, &HealthMonitor::markSocketDown);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { markSocketDown(); });
#else
    connect(m_socket,
            QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this,
            [this](QAbstractSocket::SocketError) { markSocketDown(); });
#endif

    m_socketText = QStringLiteral("WebSocket: подключение…");
    emit statusChanged();
    m_socket->open(url);
}
