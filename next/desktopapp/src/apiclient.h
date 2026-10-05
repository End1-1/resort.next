#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

// HTTP client for the new desktop. It does not open MariaDB.
// Resort/ still writes SQL until a later phase moves a module behind this call.
class ApiClient : public QObject {
public:
    explicit ApiClient(QString baseUrl, QObject *parent = nullptr);

    struct CallResult {
        bool transportOk = false;
        int httpStatus = 0;
        QByteArray body;
        QString error;
    };

    // GET {origin}/health. baseUrl is an origin (http://127.0.0.1:8080), not a path.
    CallResult getHealth(int timeoutMs = 5000);

    // POST {origin}/api/v1/sessions. The caller must not log the password or the token.
    CallResult postSession(const QString &login, const QString &password, int timeoutMs = 5000);

private:
    // body.isNull() sends GET. Any other body is a JSON POST.
    CallResult request(const QString &path, const QByteArray &body, int timeoutMs);

    QString m_baseUrl;
    QNetworkAccessManager m_nam;
};
