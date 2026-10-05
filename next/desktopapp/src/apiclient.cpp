#include "apiclient.h"

#include <QEventLoop>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

ApiClient::ApiClient(QString baseUrl, QObject *parent)
    : QObject(parent)
    , m_baseUrl(std::move(baseUrl))
{
}

ApiClient::HealthResult ApiClient::getHealth(int timeoutMs)
{
    HealthResult result;
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

    QUrl health(base);
    health.setPath(QStringLiteral("/health"));
    health.setQuery(QString());
    health.setFragment(QString());

    QNetworkRequest request(health);
    request.setTransferTimeout(timeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("hotel-desktop-stub/0.1"));

    QNetworkReply *reply = m_nam.get(request);
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
