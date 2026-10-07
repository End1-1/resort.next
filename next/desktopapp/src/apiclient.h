#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUrlQuery>

// HTTP client for hotel-api. No Qt Sql and no MariaDB connection.
// The bearer token is kept in memory only. Logout sends DELETE /api/v1/sessions
// and then clearToken() drops it. A 401 on an authenticated call is sessionRejected.

struct ApiError {
    bool transportFailure = false;
    bool timedOut = false;
    // True when userMessage came from parseServerBase / parseWebSocketUrl.
    // Those strings are translated at the call, so a language switch must parse again.
    bool reparseBaseUrl = false;
    int networkError = 0;
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

// Maps a server error code and HTTP status to the current UI language.
// 401 from the login form is "incorrect login or password", not a dead session.
QString loginErrorMessage(int httpStatus, const QString &code);

// Phrase for a session that the server no longer accepts, or for logout_unconfirmed.
// code is unauthorized, session_expired, user_disabled, commands_not_allowed, or logout_unconfirmed.
QString sessionEndedMessage(const QString &code);

// Maps an authenticated call's error. 401 uses sessionEndedMessage, not the login phrase.
QString apiErrorMessage(int httpStatus, const QString &code);

enum class HttpVerb { Get, Post, Patch, Delete };

struct ApiResponse {
    bool current = true;
    quint64 id = 0;
    QString path;
    bool ok = false;
    int httpStatus = 0;
    QByteArray rawBody;
    QJsonDocument json;
    ApiError error;
};

// Maps a stored ApiError (network, HTTP, or URL parse) to the current UI language.
// URL-parse failures (reparseBaseUrl) keep the message already stored in userMessage.
QString userMessageFor(const ApiError &error);

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
    QString token() const;

    void requestHealth(int timeoutMs = 5000);
    void requestLogin(const QString &login, const QString &password, int timeoutMs = 8000);

    // withAuth sends Authorization: Bearer. An empty token rejects locally with 401.
    // The returned id is ApiResponse::id. In-flight calls become current=false if the base URL changes.
    quint64 request(HttpVerb verb,
                    const QString &path,
                    const QUrlQuery &query,
                    const QByteArray &body,
                    bool withAuth,
                    int timeoutMs = 8000);

signals:
    void healthFinished(const HealthStatus &status);
    void loginFinished(const SessionResult &result);
    void responseFinished(const ApiResponse &response);
    // Authenticated call returned 401. code is the server error field.
    void sessionRejected(const QString &code);

private:
    // body.isNull() is unused. Get and Delete send no body. Post and Patch send body.
    class QNetworkReply *send(HttpVerb verb,
                              const QString &path,
                              const QUrlQuery &query,
                              const QByteArray &body,
                              int timeoutMs,
                              bool withAuth,
                              QString *userMessage,
                              QString *technical);

    void finishHealth(quint64 generation, const HealthStatus &status);
    void finishLogin(quint64 generation, const SessionResult &result);
    void finishResponse(quint64 id, quint64 generation, bool withAuth, const ApiResponse &response);

    QString m_baseUrl;
    QString m_token;
    class QNetworkAccessManager *m_nam = nullptr;
    quint64 m_healthGen = 0;
    quint64 m_loginGen = 0;
    quint64 m_requestGen = 0;
    quint64 m_requestSeq = 0;
};

Q_DECLARE_METATYPE(HealthStatus)
Q_DECLARE_METATYPE(SessionResult)
Q_DECLARE_METATYPE(ApiResponse)
