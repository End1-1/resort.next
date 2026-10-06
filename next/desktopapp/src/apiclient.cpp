#include "apiclient.h"

#include "urlutil.h"

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
        return QStringLiteral("Превышено время ожидания ответа сервера.");
    case QNetworkReply::ConnectionRefusedError:
        return QStringLiteral("Сервер недоступен: соединение отклонено.");
    case QNetworkReply::HostNotFoundError:
        return QStringLiteral("Сервер недоступен: адрес не найден.");
    case QNetworkReply::RemoteHostClosedError:
        return QStringLiteral("Сервер недоступен: соединение закрыто.");
    case QNetworkReply::SslHandshakeFailedError:
        return QStringLiteral("Не удалось установить защищённое соединение (TLS).");
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyNotFoundError:
    case QNetworkReply::ProxyTimeoutError:
        return QStringLiteral("Сервер недоступен: ошибка прокси.");
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::UnknownNetworkError:
        return QStringLiteral("Сервер недоступен.");
    default:
        break;
    }
    return QStringLiteral("Сервер недоступен.");
}

QString databaseDownPhrase(const QString &dbError)
{
    if (dbError == QLatin1String("access_denied"))
        return QStringLiteral("отказ в доступе (down, access_denied)");
    if (dbError == QLatin1String("unknown_database"))
        return QStringLiteral("база не найдена (down, unknown_database)");
    if (dbError == QLatin1String("cannot_connect"))
        return QStringLiteral("нет соединения (down, cannot_connect)");
    if (dbError == QLatin1String("driver_not_loaded"))
        return QStringLiteral("драйвер не загружен (down, driver_not_loaded)");
    if (dbError == QLatin1String("connection_failed"))
        return QStringLiteral("недоступна (down, connection_failed)");
    if (dbError.isEmpty())
        return QStringLiteral("недоступна (down)");
    return QStringLiteral("недоступна (down, %1)").arg(dbError);
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
        result.error.userMessage = QStringLiteral("Сервер вернул неожиданный ответ.");
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
        return QStringLiteral("доступна (up)");
    if (status.dbState == QLatin1String("skipped"))
        return QStringLiteral("не настроена (skipped)");
    if (status.dbState == QLatin1String("down"))
        return databaseDownPhrase(status.dbError);
    if (!status.dbState.isEmpty())
        return status.dbState;
    return QString();
}

} // namespace

QString loginErrorMessage(int httpStatus, const QString &code)
{
    if (httpStatus == 401 || code == QLatin1String("unauthorized"))
        return QStringLiteral("Неверный логин или пароль.");
    if (code == QLatin1String("database_not_configured")) {
        return QStringLiteral(
            "База не настроена (database_not_configured). Вход невозможен, пока на сервере не задано подключение к MariaDB.");
    }
    if (code == QLatin1String("session_store_unavailable")) {
        return QStringLiteral(
            "Хранилище сессий недоступно (session_store_unavailable). На сервере нет таблиц nx_user и nx_session или сессия не записалась.");
    }
    if (code == QLatin1String("driver_not_loaded"))
        return QStringLiteral("Драйвер базы данных не загружен на сервере (driver_not_loaded).");
    if (code == QLatin1String("access_denied")) {
        return QStringLiteral(
            "Отказ в доступе к базе на сервере (access_denied). Учётная запись MariaDB не принята для этого хоста.");
    }
    if (code == QLatin1String("unknown_database"))
        return QStringLiteral("База на сервере не найдена (unknown_database).");
    if (code == QLatin1String("cannot_connect")) {
        return QStringLiteral(
            "Сервер не подключился к MariaDB (cannot_connect). Проверьте, что служба слушает порт.");
    }
    if (code == QLatin1String("database_unavailable") || code == QLatin1String("connection_failed"))
        return QStringLiteral("База данных недоступна (database_unavailable).");
    if (httpStatus == 400 || code == QLatin1String("invalid_request"))
        return QStringLiteral("Некорректный запрос к серверу.");
    if (httpStatus == 404 || code == QLatin1String("not_found"))
        return QStringLiteral("Сервер не нашёл адрес входа.");
    if (httpStatus == 503)
        return QStringLiteral("Сервер временно недоступен (503).");
    if (httpStatus != 0) {
        if (code.isEmpty())
            return QStringLiteral("Ошибка входа, HTTP %1.").arg(httpStatus);
        return QStringLiteral("Ошибка входа, HTTP %1 (%2).").arg(httpStatus).arg(code);
    }
    return QStringLiteral("Сервер недоступен.");
}

QString healthSummary(const HealthStatus &status)
{
    if (!status.reachable) {
        if (!status.error.userMessage.isEmpty())
            return status.error.userMessage;
        return QStringLiteral("Сервер недоступен.");
    }

    const QString db = databasePhrase(status);
    if (db.isEmpty() && status.serviceStatus.isEmpty()) {
        if (status.httpStatus != 0)
            return QStringLiteral("Сервер ответил HTTP %1, состояние базы неизвестно.").arg(status.httpStatus);
        return QStringLiteral("Сервер ответил неожиданным телом.");
    }

    QString head = QStringLiteral("Сервер");
    if (!status.service.isEmpty())
        head += QLatin1Char(' ') + status.service;
    const QString state = status.serviceStatus.isEmpty() ? QStringLiteral("ответил") : status.serviceStatus;
    QString text = QStringLiteral("%1: работает (%2). База: %3").arg(head, state, db.isEmpty() ? QStringLiteral("неизвестно") : db);
    if (!status.version.isEmpty())
        text += QStringLiteral(". Версия %1").arg(status.version);
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
}

void ApiClient::setBaseUrl(const QString &baseUrl)
{
    const QString trimmed = baseUrl.trimmed();
    if (m_baseUrl == trimmed)
        return;
    m_baseUrl = trimmed;
    ++m_healthGen;
    ++m_loginGen;
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

QNetworkReply *ApiClient::send(const QString &path,
                               const QByteArray &jsonBody,
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
    url.setQuery(QString());
    url.setFragment(QString());

    QNetworkRequest request(url);
    request.setTransferTimeout(timeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("hotel-desktop/0.1.0"));
    request.setRawHeader("Accept", "application/json");
    if (withAuth && !m_token.isEmpty())
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_token.toUtf8());

    if (jsonBody.isNull())
        return m_nam->get(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return m_nam->post(request, jsonBody);
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
    QNetworkReply *reply = send(QStringLiteral("/health"), QByteArray(), timeoutMs, false, &userMessage, &technical);
    if (!reply) {
        HealthStatus status;
        status.error.transportFailure = true;
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
    QNetworkReply *reply = send(QStringLiteral("/api/v1/sessions"), payload, timeoutMs, false, &userMessage, &technical);
    if (!reply) {
        SessionResult result;
        result.error.transportFailure = true;
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
