#include "apiclient.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

ApiClient::ApiClient(QString baseUrl, QObject *parent)
    : QObject(parent)
    , m_baseUrl(std::move(baseUrl))
{
}

ApiClient::CallResult ApiClient::request(const QString &path, const QByteArray &body, int timeoutMs)
{
    CallResult result;
    const QUrl base(m_baseUrl.trimmed());
    const QString scheme = base.scheme().toLower();
    if (!base.isValid() || (scheme != QLatin1String("http") && scheme != QLatin1String("https")) || base.host().isEmpty()) {
        result.error = QStringLiteral("HOTEL_API_BASE must be an absolute http(s) origin");
        return result;
    }
    if (!base.path().isEmpty() && base.path() != QLatin1String("/")) {
        result.error = QStringLiteral("HOTEL_API_BASE must not include a path");
        return result;
    }

    QUrl url(base);
    url.setPath(path);
    url.setQuery(QString());
    url.setFragment(QString());

    QNetworkRequest request(url);
    request.setTransferTimeout(timeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("hotel-desktop-stub/0.2"));
    QNetworkReply *reply = nullptr;
    if (body.isNull()) {
        reply = m_nam.get(request);
    } else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        reply = m_nam.post(request, body);
    }
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    result.body = reply->readAll();
    result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError && result.httpStatus == 0) {
        result.error = reply->errorString();
        result.transportOk = false;
    } else {
        result.transportOk = true;
    }
    reply->deleteLater();
    return result;
}

ApiClient::CallResult ApiClient::getHealth(int timeoutMs)
{
    return request(QStringLiteral("/health"), QByteArray(), timeoutMs);
}

ApiClient::CallResult ApiClient::postSession(const QString &login, const QString &password, int timeoutMs)
{
    QJsonObject body;
    body.insert(QStringLiteral("login"), login);
    body.insert(QStringLiteral("password"), password);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return request(QStringLiteral("/api/v1/sessions"), payload, timeoutMs);
}
