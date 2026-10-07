#pragma once

#include "config.h"

#include <QtHttpServer/qhttpserver.h>

#include <QString>

class RealtimeHub;
struct ApiResult;

class HttpApi {
public:
    explicit HttpApi(AppConfig config);

    void setRealtime(RealtimeHub *hub);
    bool listen(QString *errorMessage);
    quint16 port() const;

private:
    void publishReservation(const char *type, const ApiResult &result);
    void publishDictionary(const char *dictionary, const char *action, qint64 id);

    AppConfig m_config;
    QHttpServer m_server;
    quint16 m_port = 0;
    RealtimeHub *m_hub = nullptr;
};
