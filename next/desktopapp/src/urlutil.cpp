#include "urlutil.h"

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
        *error = fail(QStringLiteral("Некорректный адрес."),
                      QStringLiteral("URL is not valid"));
        return true;
    }
    const QString scheme = url.scheme().toLower();
    const bool http = scheme == QLatin1String("http") || scheme == QLatin1String("https");
    const bool ws = scheme == QLatin1String("ws") || scheme == QLatin1String("wss");
    if (allowWebSocket ? !ws : !http) {
        *error = fail(allowWebSocket
                          ? QStringLiteral("Адрес WebSocket должен начинаться с ws:// или wss://.")
                          : QStringLiteral("Адрес сервера должен быть http или https."),
                      QStringLiteral("unsupported URL scheme"));
        return true;
    }
    if (url.host().isEmpty()) {
        *error = fail(QStringLiteral("В адресе не указан хост."),
                      QStringLiteral("URL is missing a host"));
        return true;
    }
    if (!url.userInfo().isEmpty()) {
        *error = fail(QStringLiteral("Не указывайте логин или пароль в адресе."),
                      QStringLiteral("URL must not include user info"));
        return true;
    }
    if (url.hasQuery() || url.hasFragment()) {
        *error = fail(QStringLiteral("Адрес не должен содержать параметры или фрагмент."),
                      QStringLiteral("URL must not include a query or fragment"));
        return true;
    }
    if (url.port() == 0) {
        *error = fail(QStringLiteral("Некорректный порт."),
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
        return fail(QStringLiteral("Укажите адрес сервера."),
                    QStringLiteral("base URL is empty"));
    }
    if (trimmed.contains(QLatin1Char(' ')) || trimmed.contains(QLatin1Char('\\'))) {
        return fail(QStringLiteral("Адрес сервера не должен содержать пробелы."),
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
        return fail(QStringLiteral("Адрес сервера не должен содержать путь. Укажите только хост и порт."),
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
        return fail(QStringLiteral("Адрес WebSocket не должен содержать пробелы."),
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
        return fail(QStringLiteral("Некорректный путь WebSocket."),
                    QStringLiteral("WebSocket path is invalid"));
    }

    UrlParse result;
    result.ok = true;
    result.url = canonical(url, path);
    return result;
}
