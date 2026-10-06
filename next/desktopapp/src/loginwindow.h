#pragma once

#include "apiclient.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

class LoginWindow : public QWidget {
    Q_OBJECT

public:
    explicit LoginWindow(ApiClient *api, QWidget *parent = nullptr);

    void setServerAddress(const QString &baseUrl);
    void setConnectionStatus(const QString &text);
    void setLastLogin(const QString &login);
    void setError(const QString &text);
    void prepareForShow();

signals:
    void loginSucceeded(const UserSnapshot &user);
    void rememberLogin(const QString &login);
    void openSettingsRequested();

private slots:
    void submit();
    void onLoginFinished(const SessionResult &result);

protected:
    void showEvent(QShowEvent *event) override;

private:
    ApiClient *m_api = nullptr;
    QLabel *m_serverLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLineEdit *m_loginEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_loginButton = nullptr;
    QPushButton *m_settingsButton = nullptr;
};
