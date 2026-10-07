#include "apiclient.h"

#include "urlutil.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {

QString networkUserMessage(QNetworkReply::NetworkError err)
{
    switch (err) {
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
        return QCoreApplication::translate("ApiClient", "Timed out waiting for the server.");
    case QNetworkReply::ConnectionRefusedError:
        return QCoreApplication::translate("ApiClient", "Server unavailable: connection refused.");
    case QNetworkReply::HostNotFoundError:
        return QCoreApplication::translate("ApiClient", "Server unavailable: address not found.");
    case QNetworkReply::RemoteHostClosedError:
        return QCoreApplication::translate("ApiClient", "Server unavailable: connection closed.");
    case QNetworkReply::SslHandshakeFailedError:
        return QCoreApplication::translate("ApiClient", "Could not establish a secure connection (TLS).");
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyNotFoundError:
    case QNetworkReply::ProxyTimeoutError:
        return QCoreApplication::translate("ApiClient", "Server unavailable: proxy error.");
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::UnknownNetworkError:
        return QCoreApplication::translate("ApiClient", "Server unavailable.");
    default:
        break;
    }
    return QCoreApplication::translate("ApiClient", "Server unavailable.");
}

QString databaseDownPhrase(const QString &dbError)
{
    if (dbError == QLatin1String("access_denied"))
        return QCoreApplication::translate("ApiClient", "access denied (down, access_denied)");
    if (dbError == QLatin1String("unknown_database"))
        return QCoreApplication::translate("ApiClient", "database not found (down, unknown_database)");
    if (dbError == QLatin1String("cannot_connect"))
        return QCoreApplication::translate("ApiClient", "no connection (down, cannot_connect)");
    if (dbError == QLatin1String("tls_error"))
        return QCoreApplication::translate("ApiClient", "TLS error (down, tls_error)");
    if (dbError == QLatin1String("driver_not_loaded"))
        return QCoreApplication::translate("ApiClient", "driver not loaded (down, driver_not_loaded)");
    if (dbError == QLatin1String("connection_failed"))
        return QCoreApplication::translate("ApiClient", "unavailable (down, connection_failed)");
    if (dbError.isEmpty())
        return QCoreApplication::translate("ApiClient", "unavailable (down)");
    return QCoreApplication::translate("ApiClient", "unavailable (down, %1)").arg(dbError);
}

QJsonObject objectFrom(const QByteArray &body)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return {};
    return document.object();
}

ApiError transportError(QNetworkReply *reply)
{
    ApiError error;
    error.transportFailure = true;
    error.networkError = int(reply->error());
    error.timedOut = reply->error() == QNetworkReply::TimeoutError
        || reply->error() == QNetworkReply::OperationCanceledError;
    error.userMessage = networkUserMessage(reply->error());
    error.technical = reply->errorString();
    return error;
}

HealthStatus parseHealthReply(QNetworkReply *reply)
{
    HealthStatus status;
    status.rawBody = reply->readAll();
    status.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    status.error.httpStatus = status.httpStatus;
    if (reply->error() != QNetworkReply::NoError && status.httpStatus == 0) {
        status.error = transportError(reply);
        status.error.httpStatus = 0;
        return status;
    }

    status.reachable = true;
    const QJsonObject object = objectFrom(status.rawBody);
    status.serviceStatus = object.value(QStringLiteral("status")).toString();
    status.service = object.value(QStringLiteral("service")).toString();
    status.version = object.value(QStringLiteral("version")).toString();
    const QJsonObject db = object.value(QStringLiteral("db")).toObject();
    if (!db.isEmpty()) {
        status.dbConfigured = db.value(QStringLiteral("configured")).toBool();
        status.dbState = db.value(QStringLiteral("state")).toString();
        status.dbError = db.value(QStringLiteral("error")).toString();
    }
    return status;
}

SessionResult parseLoginReply(QNetworkReply *reply)
{
    SessionResult result;
    const QByteArray body = reply->readAll();
    result.error.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError && result.error.httpStatus == 0) {
        result.error = transportError(reply);
        result.error.httpStatus = 0;
        return result;
    }

    const QJsonObject object = objectFrom(body);
    result.error.code = object.value(QStringLiteral("error")).toString();
    if (result.error.httpStatus != 200) {
        result.error.userMessage = loginErrorMessage(result.error.httpStatus, result.error.code);
        result.error.technical = result.error.code.isEmpty()
                                      ? QStringLiteral("HTTP %1").arg(result.error.httpStatus)
                                      : result.error.code;
        return result;
    }

    const QString token = object.value(QStringLiteral("token")).toString();
    const QJsonValue userValue = object.value(QStringLiteral("user"));
    if (token.isEmpty() || !userValue.isObject()) {
        result.error.userMessage = QCoreApplication::translate("ApiClient", "The server returned an unexpected response.");
        result.error.technical = QStringLiteral("session response missing token or user");
        return result;
    }

    const QJsonObject user = userValue.toObject();
    result.ok = true;
    result.tokenType = object.value(QStringLiteral("token_type")).toString();
    result.token = token;
    result.expiresAt = object.value(QStringLiteral("expires_at")).toString();
    result.commandsAllowed = object.value(QStringLiteral("commands_allowed")).toBool();
    result.user.id = user.value(QStringLiteral("id")).toInteger();
    result.user.login = user.value(QStringLiteral("login")).toString();
    result.user.name = user.value(QStringLiteral("name")).toString();
    const QJsonValue role = user.value(QStringLiteral("role_id"));
    if (role.isNull() || role.isUndefined()) {
        result.user.rolePresent = false;
    } else {
        result.user.rolePresent = true;
        result.user.roleId = role.toInteger();
    }
    return result;
}

QString databasePhrase(const HealthStatus &status)
{
    if (status.dbState == QLatin1String("up"))
        return QCoreApplication::translate("ApiClient", "available (up)");
    if (status.dbState == QLatin1String("skipped"))
        return QCoreApplication::translate("ApiClient", "not configured (skipped)");
    if (status.dbState == QLatin1String("down"))
        return databaseDownPhrase(status.dbError);
    if (!status.dbState.isEmpty())
        return status.dbState;
    return QString();
}

ApiResponse parseApiReply(QNetworkReply *reply)
{
    ApiResponse response;
    response.rawBody = reply->readAll();
    response.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    response.error.httpStatus = response.httpStatus;
    if (reply->error() != QNetworkReply::NoError && response.httpStatus == 0) {
        response.error = transportError(reply);
        response.error.httpStatus = 0;
        return response;
    }
    QJsonParseError parseError;
    response.json = QJsonDocument::fromJson(response.rawBody, &parseError);
    if (response.json.isObject())
        response.error.code = response.json.object().value(QStringLiteral("error")).toString();
    if (response.httpStatus >= 200 && response.httpStatus < 300) {
        response.ok = true;
        return response;
    }
    response.error.userMessage = apiErrorMessage(response.httpStatus, response.error.code);
    response.error.technical = response.error.code.isEmpty()
                                   ? QStringLiteral("HTTP %1").arg(response.httpStatus)
                                   : response.error.code;
    return response;
}

} // namespace

QString loginErrorMessage(int httpStatus, const QString &code)
{
    if (httpStatus == 401 || code == QLatin1String("unauthorized"))
        return QCoreApplication::translate("ApiClient", "Incorrect login or password.");
    if (code == QLatin1String("database_not_configured")) {
        return QCoreApplication::translate(
            "ApiClient",
            "The database is not configured (database_not_configured). Sign-in is impossible until the server has a MariaDB connection.");
    }
    if (code == QLatin1String("session_store_unavailable")) {
        return QCoreApplication::translate(
            "ApiClient",
            "The session store is unavailable (session_store_unavailable). The server has no nx_user and nx_session tables, or the session was not written.");
    }
    if (code == QLatin1String("driver_not_loaded")) {
        return QCoreApplication::translate("ApiClient",
                                            "The database driver is not loaded on the server (driver_not_loaded).");
    }
    if (code == QLatin1String("access_denied")) {
        return QCoreApplication::translate(
            "ApiClient",
            "The server refused the database login (access_denied). The MariaDB account was not accepted for this host.");
    }
    if (code == QLatin1String("unknown_database")) {
        return QCoreApplication::translate("ApiClient", "The database was not found on the server (unknown_database).");
    }
    if (code == QLatin1String("cannot_connect")) {
        return QCoreApplication::translate(
            "ApiClient",
            "The server could not connect to MariaDB (cannot_connect). Check that the service is listening on the port.");
    }
    if (code == QLatin1String("tls_error")) {
        return QCoreApplication::translate(
            "ApiClient",
            "The server could not negotiate TLS with MariaDB (tls_error). For a local database without TLS set mysql_ssl=preferred or off.");
    }
    if (code == QLatin1String("connection_failed")) {
        return QCoreApplication::translate("ApiClient", "The database connection failed (connection_failed).");
    }
    if (code == QLatin1String("database_unavailable"))
        return QCoreApplication::translate("ApiClient", "The database is unavailable (database_unavailable).");
    if (httpStatus == 400 || code == QLatin1String("invalid_request"))
        return QCoreApplication::translate("ApiClient", "Invalid request to the server.");
    if (httpStatus == 404 || code == QLatin1String("not_found"))
        return QCoreApplication::translate("ApiClient", "The server did not find the sign-in address.");
    if (httpStatus == 503)
        return QCoreApplication::translate("ApiClient", "The server is temporarily unavailable (503).");
    if (httpStatus != 0) {
        if (code.isEmpty())
            return QCoreApplication::translate("ApiClient", "Sign-in failed, HTTP %1.").arg(httpStatus);
        return QCoreApplication::translate("ApiClient", "Sign-in failed, HTTP %1 (%2).").arg(httpStatus).arg(code);
    }
    return QCoreApplication::translate("ApiClient", "Server unavailable.");
}

QString sessionEndedMessage(const QString &code)
{
    if (code == QLatin1String("session_expired"))
        return QCoreApplication::translate("ApiClient", "Your session has expired. Sign in again.");
    if (code == QLatin1String("user_disabled"))
        return QCoreApplication::translate("ApiClient", "This account is disabled. Sign in again.");
    if (code == QLatin1String("logout_unconfirmed")) {
        return QCoreApplication::translate(
            "ApiClient", "Signed out on this computer. The server did not confirm logout.");
    }
    if (code == QLatin1String("commands_not_allowed"))
        return QCoreApplication::translate("ApiClient", "You do not have permission to change data.");
    return QCoreApplication::translate("ApiClient", "Sign in again. The session is no longer valid.");
}

QString apiErrorMessage(int httpStatus, const QString &code)
{
    if (httpStatus == 401 || code == QLatin1String("unauthorized") || code == QLatin1String("session_expired")
        || code == QLatin1String("user_disabled")) {
        return sessionEndedMessage(code);
    }
    if (httpStatus == 403 || code == QLatin1String("commands_not_allowed"))
        return sessionEndedMessage(QStringLiteral("commands_not_allowed"));
    if (code == QLatin1String("overlap"))
        return QCoreApplication::translate("ApiClient", "That room already has a stay on those nights.");
    if (code == QLatin1String("invalid_transition"))
        return QCoreApplication::translate("ApiClient", "That status change is not allowed.");
    if (code == QLatin1String("version_conflict"))
        return QCoreApplication::translate("ApiClient", "The reservation was changed. Reload it and try again.");
    if (code == QLatin1String("stay_in_house"))
        return QCoreApplication::translate("ApiClient", "Check the guest out before canceling the reservation.");
    if (code == QLatin1String("stay_locked"))
        return QCoreApplication::translate("ApiClient", "Dates and room cannot change while the guest is in house or checked out.");
    if (code == QLatin1String("room_not_found"))
        return QCoreApplication::translate("ApiClient", "The room was not found.");
    if (code == QLatin1String("guest_not_found"))
        return QCoreApplication::translate("ApiClient", "The guest was not found.");
    if (code == QLatin1String("reservation_not_found"))
        return QCoreApplication::translate("ApiClient", "The reservation was not found.");
    if (code == QLatin1String("schema_outdated")) {
        return QCoreApplication::translate(
            "ApiClient",
            "The server schema is missing a table or column (schema_outdated). Apply the nx_ migrations through 0004_nx_audit.sql.");
    }
    if (code == QLatin1String("database_not_configured") || code == QLatin1String("session_store_unavailable")
        || code == QLatin1String("driver_not_loaded") || code == QLatin1String("access_denied")
        || code == QLatin1String("unknown_database") || code == QLatin1String("cannot_connect")
        || code == QLatin1String("tls_error") || code == QLatin1String("connection_failed")
        || code == QLatin1String("database_unavailable")
        || code == QLatin1String("invalid_request") || code == QLatin1String("not_found")) {
        return loginErrorMessage(httpStatus, code);
    }
    if (httpStatus != 0) {
        const QString shown = code.isEmpty() ? QString::number(httpStatus) : code;
        return QCoreApplication::translate("ApiClient", "Request failed, HTTP %1 (%2).").arg(httpStatus).arg(shown);
    }
    return QCoreApplication::translate("ApiClient", "Server unavailable.");
}

QString userMessageFor(const ApiError &error)
{
    if (error.reparseBaseUrl)
        return error.userMessage;
    if (error.httpStatus == 0)
        return networkUserMessage(static_cast<QNetworkReply::NetworkError>(error.networkError));
    return loginErrorMessage(error.httpStatus, error.code);
}

QString healthSummary(const HealthStatus &status)
{
    if (!status.reachable) {
        if (status.error.reparseBaseUrl && !status.error.userMessage.isEmpty())
            return status.error.userMessage;
        if (status.error.transportFailure || status.error.networkError != 0)
            return userMessageFor(status.error);
        if (!status.error.userMessage.isEmpty())
            return status.error.userMessage;
        return QCoreApplication::translate("ApiClient", "Server unavailable.");
    }

    const QString db = databasePhrase(status);
    if (db.isEmpty() && status.serviceStatus.isEmpty()) {
        if (status.httpStatus != 0) {
            return QCoreApplication::translate("ApiClient", "Server replied HTTP %1; the database state is unknown.")
                .arg(status.httpStatus);
        }
        return QCoreApplication::translate("ApiClient", "Server replied with an unexpected body.");
    }

    QString head = QCoreApplication::translate("ApiClient", "Server");
    if (!status.service.isEmpty())
        head += QLatin1Char(' ') + status.service;
    const QString state = status.serviceStatus.isEmpty()
                              ? QCoreApplication::translate("ApiClient", "responded")
                              : status.serviceStatus;
    const QString dbText = db.isEmpty() ? QCoreApplication::translate("ApiClient", "unknown") : db;
    QString text = QCoreApplication::translate("ApiClient", "%1: running (%2). Database: %3").arg(head, state, dbText);
    if (!status.version.isEmpty())
        text += QCoreApplication::translate("ApiClient", ". Version %1").arg(status.version);
    return text;
}

UserSnapshot userSnapshotFrom(const SessionResult &result)
{
    UserSnapshot snap;
    snap.id = result.user.id;
    snap.login = result.user.login;
    snap.name = result.user.name;
    snap.rolePresent = result.user.rolePresent;
    snap.roleId = result.user.roleId;
    snap.commandsAllowed = result.commandsAllowed;
    snap.expiresAt = result.expiresAt;
    return snap;
}

ApiClient::ApiClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    qRegisterMetaType<HealthStatus>();
    qRegisterMetaType<SessionResult>();
    qRegisterMetaType<ApiResponse>();
}

void ApiClient::setBaseUrl(const QString &baseUrl)
{
    const QString trimmed = baseUrl.trimmed();
    if (m_baseUrl == trimmed)
        return;
    m_baseUrl = trimmed;
    ++m_healthGen;
    ++m_loginGen;
    ++m_requestGen;
}

QString ApiClient::baseUrl() const
{
    return m_baseUrl;
}

void ApiClient::setToken(const QString &token)
{
    if (token.contains(QLatin1Char('\r')) || token.contains(QLatin1Char('\n')))
        m_token.clear();
    else
        m_token = token;
}

void ApiClient::clearToken()
{
    m_token.clear();
}

bool ApiClient::hasToken() const
{
    return !m_token.isEmpty();
}

QString ApiClient::token() const
{
    return m_token;
}

QNetworkReply *ApiClient::send(HttpVerb verb,
                               const QString &path,
                               const QUrlQuery &query,
                               const QByteArray &body,
                               int timeoutMs,
                               bool withAuth,
                               QString *userMessage,
                               QString *technical)
{
    const UrlParse parsed = parseServerBase(m_baseUrl);
    if (!parsed.ok) {
        if (userMessage)
            *userMessage = parsed.error;
        if (technical)
            *technical = parsed.technical;
        return nullptr;
    }

    QUrl url(parsed.url);
    url.setPath(path);
    url.setQuery(query);
    url.setFragment(QString());

    QNetworkRequest request(url);
    request.setTransferTimeout(timeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("hotel-desktop/0.1.0"));
    request.setRawHeader("Accept", "application/json");
    if (withAuth && !m_token.isEmpty())
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_token.toUtf8());

    if (verb == HttpVerb::Get)
        return m_nam->get(request);
    if (verb == HttpVerb::Delete)
        return m_nam->sendCustomRequest(request, QByteArrayLiteral("DELETE"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (verb == HttpVerb::Patch)
        return m_nam->sendCustomRequest(request, QByteArrayLiteral("PATCH"), body);
    return m_nam->post(request, body);
}

void ApiClient::finishHealth(quint64 generation, const HealthStatus &status)
{
    HealthStatus copy = status;
    copy.current = generation == m_healthGen;
    emit healthFinished(copy);
}

void ApiClient::finishLogin(quint64 generation, const SessionResult &result)
{
    SessionResult copy = result;
    copy.current = generation == m_loginGen;
    emit loginFinished(copy);
}

void ApiClient::requestHealth(int timeoutMs)
{
    const quint64 generation = ++m_healthGen;
    QString userMessage;
    QString technical;
    QNetworkReply *reply = send(HttpVerb::Get, QStringLiteral("/health"), QUrlQuery(), QByteArray(), timeoutMs, false,
                                &userMessage, &technical);
    if (!reply) {
        HealthStatus status;
        status.error.transportFailure = true;
        status.error.reparseBaseUrl = true;
        status.error.userMessage = userMessage;
        status.error.technical = technical;
        QTimer::singleShot(0, this, [this, generation, status]() { finishHealth(generation, status); });
        return;
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        const HealthStatus status = parseHealthReply(reply);
        reply->deleteLater();
        finishHealth(generation, status);
    });
}

void ApiClient::requestLogin(const QString &login, const QString &password, int timeoutMs)
{
    const quint64 generation = ++m_loginGen;
    QJsonObject body;
    body.insert(QStringLiteral("login"), login);
    body.insert(QStringLiteral("password"), password);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QString userMessage;
    QString technical;
    QNetworkReply *reply = send(HttpVerb::Post, QStringLiteral("/api/v1/sessions"), QUrlQuery(), payload, timeoutMs, false,
                                &userMessage, &technical);
    if (!reply) {
        SessionResult result;
        result.error.transportFailure = true;
        result.error.reparseBaseUrl = true;
        result.error.userMessage = userMessage;
        result.error.technical = technical;
        QTimer::singleShot(0, this, [this, generation, result]() { finishLogin(generation, result); });
        return;
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        const SessionResult result = parseLoginReply(reply);
        reply->deleteLater();
        finishLogin(generation, result);
    });
}

void ApiClient::finishResponse(quint64 id, quint64 generation, bool withAuth, const ApiResponse &response)
{
    ApiResponse copy = response;
    copy.id = id;
    copy.current = generation == m_requestGen;
    if (withAuth && copy.current && copy.error.httpStatus == 401) {
        const QString code = copy.error.code.isEmpty() ? QStringLiteral("unauthorized") : copy.error.code;
        emit sessionRejected(code);
    }
    emit responseFinished(copy);
}

quint64 ApiClient::request(HttpVerb verb,
                           const QString &path,
                           const QUrlQuery &query,
                           const QByteArray &body,
                           bool withAuth,
                           int timeoutMs)
{
    const quint64 id = ++m_requestSeq;
    const quint64 generation = m_requestGen;

    if (withAuth && m_token.isEmpty()) {
        ApiResponse response;
        response.path = path;
        response.error.httpStatus = 401;
        response.error.code = QStringLiteral("unauthorized");
        response.error.userMessage = sessionEndedMessage(response.error.code);
        QTimer::singleShot(0, this, [this, id, generation, withAuth, response]() {
            finishResponse(id, generation, withAuth, response);
        });
        return id;
    }

    QString userMessage;
    QString technical;
    QNetworkReply *reply = send(verb, path, query, body, timeoutMs, withAuth, &userMessage, &technical);
    if (!reply) {
        ApiResponse response;
        response.path = path;
        response.error.transportFailure = true;
        response.error.reparseBaseUrl = true;
        response.error.userMessage = userMessage;
        response.error.technical = technical;
        QTimer::singleShot(0, this, [this, id, generation, response]() {
            finishResponse(id, generation, false, response);
        });
        return id;
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, id, generation, withAuth, path]() {
        ApiResponse response = parseApiReply(reply);
        response.path = path;
        reply->deleteLater();
        finishResponse(id, generation, withAuth, response);
    });
    return id;
}
