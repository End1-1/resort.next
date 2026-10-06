#include "urlutil.h"

#include <QCoreApplication>
#include <QLatin1String>
#include <QUrl>

namespace {

UrlParse fail(const QString &error, const QString &technical)
{
    UrlParse result;
    result.error = error;
    result.technical = technical;
    return result;
}

bool badShape(const QUrl &url, bool allowWebSocket, UrlParse *error)
{
    if (!url.isValid()) {
        *error = fail(QCoreApplication::translate("UrlUtil", "Invalid address."),
                      QStringLiteral("URL is not valid"));
        return true;
    }
    const QString scheme = url.scheme().toLower();
    const bool http = scheme == QLatin1String("http") || scheme == QLatin1String("https");
    const bool ws = scheme == QLatin1String("ws") || scheme == QLatin1String("wss");
    if (allowWebSocket ? !ws : !http) {
        *error = fail(allowWebSocket
                          ? QCoreApplication::translate("UrlUtil", "The WebSocket address must start with ws:// or wss://.")
                          : QCoreApplication::translate("UrlUtil", "The server address must be http or https."),
                      QStringLiteral("unsupported URL scheme"));
        return true;
    }
    if (url.host().isEmpty()) {
        *error = fail(QCoreApplication::translate("UrlUtil", "The address has no host."),
                      QStringLiteral("URL is missing a host"));
        return true;
    }
    if (!url.userInfo().isEmpty()) {
        *error = fail(QCoreApplication::translate("UrlUtil", "Do not put a login or password in the address."),
                      QStringLiteral("URL must not include user info"));
        return true;
    }
    if (url.hasQuery() || url.hasFragment()) {
        *error = fail(QCoreApplication::translate("UrlUtil", "The address must not contain a query or a fragment."),
                      QStringLiteral("URL must not include a query or fragment"));
        return true;
    }
    if (url.port() == 0) {
        *error = fail(QCoreApplication::translate("UrlUtil", "Invalid port."),
                      QStringLiteral("URL port is invalid"));
        return true;
    }
    return false;
}

QString canonical(const QUrl &url, const QString &path)
{
    QUrl out;
    out.setScheme(url.scheme().toLower());
    out.setHost(url.host());
    if (url.port() > 0)
        out.setPort(url.port());
    if (!path.isEmpty())
        out.setPath(path);
    return out.toString();
}

} // namespace

UrlParse parseServerBase(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return fail(QCoreApplication::translate("UrlUtil", "Enter the server address."),
                    QStringLiteral("base URL is empty"));
    }
    if (trimmed.contains(QLatin1Char(' ')) || trimmed.contains(QLatin1Char('\\'))) {
        return fail(QCoreApplication::translate("UrlUtil", "The server address must not contain spaces."),
                    QStringLiteral("base URL contains whitespace"));
    }

    QString candidate = trimmed;
    if (!candidate.contains(QLatin1String("://")))
        candidate.prepend(QLatin1String("http://"));

    const QUrl url(candidate, QUrl::StrictMode);
    UrlParse error;
    if (badShape(url, false, &error))
        return error;

    const QString path = url.path();
    if (!path.isEmpty() && path != QLatin1String("/")) {
        return fail(QCoreApplication::translate("UrlUtil", "The server address must not contain a path. Enter only the host and port."),
                    QStringLiteral("base URL must not include a path"));
    }

    UrlParse result;
    result.ok = true;
    result.url = canonical(url, QString());
    return result;
}

UrlParse parseWebSocketUrl(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        UrlParse result;
        result.ok = true;
        return result;
    }
    if (trimmed.contains(QLatin1Char(' ')) || trimmed.contains(QLatin1Char('\\'))) {
        return fail(QCoreApplication::translate("UrlUtil", "The WebSocket address must not contain spaces."),
                    QStringLiteral("WebSocket URL contains whitespace"));
    }

    QString candidate = trimmed;
    if (!candidate.contains(QLatin1String("://")))
        candidate.prepend(QLatin1String("ws://"));

    const QUrl url(candidate, QUrl::StrictMode);
    UrlParse error;
    if (badShape(url, true, &error))
        return error;

    QString path = url.path();
    if (path.isEmpty() || path == QLatin1String("/"))
        path = QStringLiteral("/api/v1/ws");
    else if (!path.startsWith(QLatin1Char('/'))) {
        return fail(QCoreApplication::translate("UrlUtil", "Invalid WebSocket path."),
                    QStringLiteral("WebSocket path is invalid"));
    }

    UrlParse result;
    result.ok = true;
    result.url = canonical(url, path);
    return result;
}
