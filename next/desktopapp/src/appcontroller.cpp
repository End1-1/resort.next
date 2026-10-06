#include "appcontroller.h"

#include "connectiondialog.h"

#include <QLatin1String>

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_api(this)
    , m_monitor(&m_api, this)
    , m_login(&m_api)
    , m_main()
{
    connect(&m_login, &LoginWindow::loginSucceeded, this, &AppController::onLogin);
    connect(&m_login, &LoginWindow::rememberLogin, this, &AppController::onRememberLogin);
    connect(&m_login, &LoginWindow::openSettingsRequested, this, &AppController::editSettings);
    connect(&m_main, &MainWindow::logoutRequested, this, &AppController::onLogout);
    connect(&m_main, &MainWindow::openSettingsRequested, this, &AppController::editSettings);
    connect(&m_monitor, &HealthMonitor::statusChanged, this, &AppController::refreshStatus);
}

void AppController::start()
{
    const ConfigLoad loaded = AppConfig::load();
    m_config = loaded.config;
    m_api.setBaseUrl(m_config.baseUrl);
    m_login.setLastLogin(m_config.lastLogin);
    m_login.setServerAddress(m_config.baseUrl);
    if (!loaded.warning.isEmpty())
        m_login.setError(loaded.warning);
    m_monitor.setWebSocketUrl(m_config.webSocketUrl);
    m_monitor.start();
    refreshStatus();
    m_login.show();
}

void AppController::onLogin(const UserSnapshot &user)
{
    m_main.showSession(user, m_api.baseUrl());
    refreshStatus();
    m_main.show();
    m_main.raise();
    m_main.activateWindow();
    m_login.hide();
}

void AppController::onLogout()
{
    // No DELETE /api/v1/sessions yet. The nx_session row stays until it expires.
    m_api.clearToken();
    m_login.prepareForShow();
    m_login.show();
    m_login.raise();
    m_login.activateWindow();
    m_main.hide();
}

void AppController::onRememberLogin(const QString &login)
{
    m_config.lastLogin = login;
    QString error;
    if (!AppConfig::save(m_config, &error))
        m_login.setError(QStringLiteral("Не удалось запомнить логин. %1").arg(error));
}

void AppController::editSettings()
{
    QWidget *parent = m_main.isVisible() ? static_cast<QWidget *>(&m_main) : static_cast<QWidget *>(&m_login);
    ConnectionDialog dialog(m_config, parent);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const DesktopConfig updated = dialog.settings();
    const bool baseChanged = updated.baseUrl != m_api.baseUrl();
    const bool signedIn = m_main.isVisible();
    m_config = updated;
    m_api.setBaseUrl(m_config.baseUrl);
    m_login.setServerAddress(m_config.baseUrl);
    m_main.setServerAddress(m_config.baseUrl);
    m_monitor.setWebSocketUrl(m_config.webSocketUrl);
    m_monitor.refreshNow();
    if (baseChanged && signedIn) {
        m_api.clearToken();
        m_login.prepareForShow();
        m_login.setError(QStringLiteral("Адрес сервера изменён. Войдите снова."));
        m_login.show();
        m_login.raise();
        m_login.activateWindow();
        m_main.hide();
    }
    refreshStatus();
}

void AppController::refreshStatus()
{
    const QString health = m_monitor.healthText();
    const QString socket = m_monitor.socketText();
    m_login.setConnectionStatus(health + QLatin1Char('\n') + socket);
    m_main.setLiveStatus(health, socket);
}
