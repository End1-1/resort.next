#pragma once

#include "config.h"

#include <QtHttpServer/qhttpserver.h>

#include <QString>

class HttpApi {
public:
    explicit HttpApi(AppConfig config);

    bool listen(QString *errorMessage);
    quint16 port() const;

private:
    AppConfig m_config;
    QHttpServer m_server;
    quint16 m_port = 0;
};
