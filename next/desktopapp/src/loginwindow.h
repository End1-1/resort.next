#pragma once

#include "apiclient.h"
#include "uilanguage.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

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
    void languageRequested(const QString &code);

private slots:
    void submit();
    void onLoginFinished(const SessionResult &result);

protected:
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void retranslateUi();
    void showStoredError();

    enum class Notice {
        None,
        NeedCredentials,
        Server,
        External
    };

    ApiClient *m_api = nullptr;
    QString m_baseUrl;
    QString m_externalError;
    ApiError m_lastError;
    Notice m_notice = Notice::None;
    bool m_busy = false;

    QLabel *m_titleLabel = nullptr;
    QLabel *m_serverLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_loginLabel = nullptr;
    QLabel *m_passwordLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLineEdit *m_loginEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_loginButton = nullptr;
    QPushButton *m_settingsButton = nullptr;
    QToolButton *m_languageButton = nullptr;
    HotelLocale::LanguageActions m_languages;
};
