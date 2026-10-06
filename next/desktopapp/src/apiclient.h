#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QObject>
#include <QString>

// HTTP client for hotel-api. No Qt Sql and no MariaDB connection.
// The bearer token is kept in memory only. There is no logout route:
// clearToken() drops it; the server row lives until it expires.

struct ApiError {
    bool transportFailure = false;
    bool timedOut = false;
    int httpStatus = 0;
    QString code;
    QString userMessage;
    QString technical;
};

struct HealthStatus {
    bool current = true;
    bool reachable = false;
    int httpStatus = 0;
    QString serviceStatus;
    QString service;
    QString version;
    bool dbConfigured = false;
    QString dbState;
    QString dbError;
    QByteArray rawBody;
    ApiError error;
};

struct SessionUser {
    qint64 id = 0;
    QString login;
    QString name;
    bool rolePresent = false;
    qint64 roleId = 0;
};

// What the main window may show. The token is not copied here.
struct UserSnapshot {
    qint64 id = 0;
    QString login;
    QString name;
    bool rolePresent = false;
    qint64 roleId = 0;
    bool commandsAllowed = false;
    QString expiresAt;
};

struct SessionResult {
    bool current = true;
    bool ok = false;
    QString tokenType;
    QString token;
    QString expiresAt;
    SessionUser user;
    bool commandsAllowed = false;
    ApiError error;
};

QString healthSummary(const HealthStatus &status);

UserSnapshot userSnapshotFrom(const SessionResult &result);

class ApiClient : public QObject {
    Q_OBJECT

public:
    explicit ApiClient(QObject *parent = nullptr);

    void setBaseUrl(const QString &baseUrl);
    QString baseUrl() const;

    // Memory only. Never written by this class.
    void setToken(const QString &token);
    void clearToken();
    bool hasToken() const;

    void requestHealth(int timeoutMs = 5000);
    void requestLogin(const QString &login, const QString &password, int timeoutMs = 8000);

signals:
    void healthFinished(const HealthStatus &status);
    void loginFinished(const SessionResult &result);

private:
    // jsonBody.isNull() sends GET. withAuth attaches the bearer token for later screens.
    class QNetworkReply *send(const QString &path,
                              const QByteArray &jsonBody,
                              int timeoutMs,
                              bool withAuth,
                              QString *userMessage,
                              QString *technical);

    void finishHealth(quint64 generation, const HealthStatus &status);
    void finishLogin(quint64 generation, const SessionResult &result);

    QString m_baseUrl;
    QString m_token;
    class QNetworkAccessManager *m_nam = nullptr;
    quint64 m_healthGen = 0;
    quint64 m_loginGen = 0;
};

Q_DECLARE_METATYPE(HealthStatus)
Q_DECLARE_METATYPE(SessionResult)
