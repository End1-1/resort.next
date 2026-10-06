#pragma once

#include "apiclient.h"

#include <QMainWindow>

class QLabel;
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

private:
    QLabel *m_userLabel = nullptr;
    QLabel *m_loginLabel = nullptr;
    QLabel *m_roleLabel = nullptr;
    QLabel *m_commandsLabel = nullptr;
    QLabel *m_expiresLabel = nullptr;
    QLabel *m_serverLabel = nullptr;
    QLabel *m_healthLabel = nullptr;
    QLabel *m_socketLabel = nullptr;
    QStackedWidget *m_workspace = nullptr;
};
