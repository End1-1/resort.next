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

    struct HealthResult {
        bool transportOk = false;
        int httpStatus = 0;
        QByteArray body;
        QString error;
    };

    // GET {origin}/health. baseUrl is an origin (http://127.0.0.1:8080), not a path.
    HealthResult getHealth(int timeoutMs = 5000);

private:
    QString m_baseUrl;
    QNetworkAccessManager m_nam;
};
