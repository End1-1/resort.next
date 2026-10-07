#include "healthmonitor.h"

#include "urlutil.h"

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QEvent>
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
{
    m_healthText = tr("Checking connection…");
    m_socketText = socketPhrase();
    if (qApp)
        qApp->installEventFilter(this);
    connect(m_api, &ApiClient::healthFinished, this, &HealthMonitor::onHealth);
    m_timer.setInterval(kPollMs);
    connect(&m_timer, &QTimer::timeout, this, &HealthMonitor::poll);
    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, &HealthMonitor::ensureSocket);
}

HealthMonitor::~HealthMonitor()
{
    if (qApp)
        qApp->removeEventFilter(this);
    closeSocket();
}

void HealthMonitor::setWebSocketUrl(const QString &url)
{
    if (m_wsUrl == url)
        return;
    m_wsUrl = url;
    m_helloSeen = false;
    closeSocket();
    m_phase = m_wsUrl.isEmpty() ? SocketPhase::NotConfigured : SocketPhase::Connecting;
    m_socketText = socketPhrase();
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
    m_haveHealth = true;
    m_lastHealth = status;
    if (status.error.reparseBaseUrl) {
        const UrlParse parsed = parseServerBase(m_api->baseUrl());
        m_healthText = parsed.ok ? healthSummary(status) : parsed.error;
    } else {
        m_healthText = healthSummary(status);
    }
    emit statusChanged();
}

bool HealthMonitor::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::LanguageChange)
        retranslate();
    return QObject::eventFilter(watched, event);
}

void HealthMonitor::retranslate()
{
    if (!m_haveHealth)
        m_healthText = tr("Checking connection…");
    else if (m_lastHealth.error.reparseBaseUrl) {
        const UrlParse parsed = parseServerBase(m_api->baseUrl());
        m_healthText = parsed.error;
    } else {
        m_healthText = healthSummary(m_lastHealth);
    }
    m_socketText = socketPhrase();
    emit statusChanged();
}

QString HealthMonitor::socketPhrase() const
{
    switch (m_phase) {
    case SocketPhase::NotConfigured:
        return tr("WebSocket: not configured");
    case SocketPhase::Connecting:
        return tr("WebSocket: connecting…");
    case SocketPhase::InvalidAddress:
        return tr("WebSocket: invalid address");
    case SocketPhase::WaitingHello:
        return tr("WebSocket: connected, waiting for hello");
    case SocketPhase::NotSignedIn:
        return tr("WebSocket: not signed in");
    case SocketPhase::ConnectedHello:
        return tr("WebSocket: connected (hello)");
    case SocketPhase::Connected:
        return tr("WebSocket: connected");
    case SocketPhase::Down:
        return tr("WebSocket: no connection");
    }
    return tr("WebSocket: not configured");
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
    m_phase = m_api->hasToken() ? SocketPhase::Down : SocketPhase::NotSignedIn;
    m_socketText = socketPhrase();
    emit statusChanged();
    if (!m_running || !m_api->hasToken())
        return;
    if (!m_reconnect.isActive())
        m_reconnect.start(m_backoffMs);
    m_backoffMs = qMin(30000, m_backoffMs * 2);
}

void HealthMonitor::reconnectNow()
{
    m_backoffMs = 1000;
    m_reconnect.stop();
    closeSocket();
    if (m_running)
        ensureSocket();
}

void HealthMonitor::ensureSocket()
{
    if (m_wsUrl.isEmpty())
        return;
    if (!m_api->hasToken()) {
        closeSocket();
        m_phase = SocketPhase::NotSignedIn;
        m_socketText = socketPhrase();
        emit statusChanged();
        return;
    }
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
        m_phase = SocketPhase::InvalidAddress;
        m_socketText = socketPhrase();
        emit statusChanged();
        return;
    }

    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_socket, &QWebSocket::connected, this, [this]() {
        m_phase = SocketPhase::WaitingHello;
        m_socketText = socketPhrase();
        emit statusChanged();
        QJsonObject auth;
        auth.insert(QStringLiteral("type"), QStringLiteral("auth"));
        auth.insert(QStringLiteral("token"), m_api->token());
        m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(auth).toJson(QJsonDocument::Compact)));
    });
    connect(m_socket, &QWebSocket::textMessageReceived, this, [this](const QString &message) {
        const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8());
        const QString type = document.object().value(QStringLiteral("type")).toString();
        if (type == QLatin1String("hello")) {
            m_helloSeen = true;
            m_backoffMs = 1000;
            m_phase = SocketPhase::ConnectedHello;
            m_socketText = socketPhrase();
            emit statusChanged();
            return;
        }
        if (type.startsWith(QLatin1String("reservation.")) || type == QLatin1String("room.status_changed"))
            emit hotelEvent(type);
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

    m_phase = SocketPhase::Connecting;
    m_socketText = socketPhrase();
    emit statusChanged();
    m_socket->open(url);
}
