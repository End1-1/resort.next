#pragma once

#include "apiclient.h"
#include "appconfig.h"
#include "healthmonitor.h"
#include "loginwindow.h"
#include "mainwindow.h"

#include <QObject>

class WorkspacePage;

class AppController : public QObject {
    Q_OBJECT

public:
    explicit AppController(QObject *parent = nullptr);
    void start();

private slots:
    void onLogin(const UserSnapshot &user);
    void onLogout();
    void onRememberLogin(const QString &login);
    void onLanguage(const QString &code);
    void editSettings();
    void refreshStatus();
    void onApiResponse(const ApiResponse &response);
    void onSessionRejected(const QString &code);

private:
    void returnToLogin(const QString &sessionCode);

    ApiClient m_api;
    HealthMonitor m_monitor;
    DesktopConfig m_config;
    LoginWindow m_login;
    MainWindow m_main;
    quint64 m_logoutId = 0;
    bool m_leaving = false;
    WorkspacePage *m_workspace = nullptr;
};
