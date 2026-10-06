#pragma once

#include "apiclient.h"
#include "appconfig.h"
#include "healthmonitor.h"
#include "loginwindow.h"
#include "mainwindow.h"

#include <QObject>

class AppController : public QObject {
    Q_OBJECT

public:
    explicit AppController(QObject *parent = nullptr);
    void start();

private slots:
    void onLogin(const UserSnapshot &user);
    void onLogout();
    void onRememberLogin(const QString &login);
    void editSettings();
    void refreshStatus();

private:
    ApiClient m_api;
    HealthMonitor m_monitor;
    DesktopConfig m_config;
    LoginWindow m_login;
    MainWindow m_main;
};
