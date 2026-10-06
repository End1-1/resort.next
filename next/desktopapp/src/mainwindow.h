#pragma once

#include "apiclient.h"
#include "uilanguage.h"

#include <QMainWindow>

class QAction;
class QGroupBox;
class QLabel;
class QMenu;
class QStackedWidget;

// Shell after login. Later screens (rack chart, reservations) are QWidget
// pages on workspace(); they call hotel-api through ApiClient and do not
// link Qt Sql. The bearer token stays inside ApiClient.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    void showSession(const UserSnapshot &user, const QString &baseUrl);
    void setServerAddress(const QString &baseUrl);
    void setLiveStatus(const QString &health, const QString &socketText);

    QStackedWidget *workspace() const;
    void setWorkspacePage(QWidget *page);

signals:
    void openSettingsRequested();
    void logoutRequested();
    void languageRequested(const QString &code);

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslateUi();
    void applySession();

    bool m_hasSession = false;
    UserSnapshot m_session;
    QString m_baseUrl;

    QMenu *m_sessionMenu = nullptr;
    QMenu *m_settingsMenu = nullptr;
    QAction *m_logoutAction = nullptr;
    QAction *m_settingsAction = nullptr;
    HotelLocale::LanguageActions m_languages;

    QLabel *m_userCaption = nullptr;
    QLabel *m_loginCaption = nullptr;
    QLabel *m_roleCaption = nullptr;
    QLabel *m_commandsCaption = nullptr;
    QLabel *m_expiresCaption = nullptr;
    QLabel *m_serverCaption = nullptr;
    QLabel *m_healthCaption = nullptr;
    QLabel *m_socketCaption = nullptr;

    QLabel *m_userLabel = nullptr;
    QLabel *m_loginLabel = nullptr;
    QLabel *m_roleLabel = nullptr;
    QLabel *m_commandsLabel = nullptr;
    QLabel *m_expiresLabel = nullptr;
    QLabel *m_serverLabel = nullptr;
    QLabel *m_healthLabel = nullptr;
    QLabel *m_socketLabel = nullptr;
    QGroupBox *m_sessionBox = nullptr;
    QLabel *m_placeholderLabel = nullptr;
    QStackedWidget *m_workspace = nullptr;
};
