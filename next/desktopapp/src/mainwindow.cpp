#include "mainwindow.h"

#include <QAction>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMenuBar>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Отель"));
    resize(960, 640);

    auto *logoutAction = new QAction(QStringLiteral("Выход"), this);
    logoutAction->setObjectName(QStringLiteral("logoutAction"));
    auto *settingsAction = new QAction(QStringLiteral("Настройки подключения"), this);
    settingsAction->setObjectName(QStringLiteral("connectionSettingsAction"));

    QMenu *sessionMenu = menuBar()->addMenu(QStringLiteral("Сессия"));
    sessionMenu->addAction(logoutAction);
    QMenu *settingsMenu = menuBar()->addMenu(QStringLiteral("Настройки"));
    settingsMenu->addAction(settingsAction);

    connect(logoutAction, &QAction::triggered, this, &MainWindow::logoutRequested);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettingsRequested);

    m_userLabel = new QLabel(QStringLiteral("—"));
    m_userLabel->setObjectName(QStringLiteral("userLabel"));
    m_userLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_loginLabel = new QLabel(QStringLiteral("—"));
    m_loginLabel->setObjectName(QStringLiteral("loginLabel"));
    m_loginLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_roleLabel = new QLabel(QStringLiteral("—"));
    m_roleLabel->setObjectName(QStringLiteral("roleLabel"));

    m_commandsLabel = new QLabel(QStringLiteral("—"));
    m_commandsLabel->setObjectName(QStringLiteral("commandsLabel"));

    m_expiresLabel = new QLabel(QStringLiteral("—"));
    m_expiresLabel->setObjectName(QStringLiteral("expiresLabel"));
    m_expiresLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_serverLabel = new QLabel(QStringLiteral("—"));
    m_serverLabel->setObjectName(QStringLiteral("serverLabel"));
    m_serverLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_healthLabel = new QLabel(QStringLiteral("Проверка соединения…"));
    m_healthLabel->setObjectName(QStringLiteral("healthLabel"));
    m_healthLabel->setWordWrap(true);
    m_healthLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_socketLabel = new QLabel(QStringLiteral("WebSocket: не настроен"));
    m_socketLabel->setObjectName(QStringLiteral("socketLabel"));
    m_socketLabel->setWordWrap(true);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Пользователь"), m_userLabel);
    form->addRow(QStringLiteral("Логин"), m_loginLabel);
    form->addRow(QStringLiteral("Роль (role_id)"), m_roleLabel);
    form->addRow(QStringLiteral("Команды разрешены"), m_commandsLabel);
    form->addRow(QStringLiteral("Сессия до"), m_expiresLabel);
    form->addRow(QStringLiteral("Сервер"), m_serverLabel);
    form->addRow(QStringLiteral("Состояние"), m_healthLabel);
    form->addRow(QStringLiteral("Канал"), m_socketLabel);

    auto *sessionBox = new QGroupBox(QStringLiteral("Текущий сеанс"));
    sessionBox->setLayout(form);

    m_workspace = new QStackedWidget;
    m_workspace->setObjectName(QStringLiteral("workspace"));

    auto *placeholder = new QWidget;
    placeholder->setObjectName(QStringLiteral("workspacePlaceholder"));
    auto *placeholderLayout = new QVBoxLayout(placeholder);
    auto *placeholderLabel = new QLabel(QStringLiteral(
        "Здесь будут рабочие экраны: шахматка, бронирования и другие модули."));
    placeholderLabel->setObjectName(QStringLiteral("workspacePlaceholderLabel"));
    placeholderLabel->setAlignment(Qt::AlignCenter);
    placeholderLabel->setWordWrap(true);
    placeholderLayout->addWidget(placeholderLabel);
    m_workspace->addWidget(placeholder);

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(sessionBox);
    layout->addWidget(m_workspace, 1);
    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("Проверка соединения…"));
}

void MainWindow::showSession(const UserSnapshot &user, const QString &baseUrl)
{
    m_userLabel->setText(user.name.isEmpty() ? QStringLiteral("—") : user.name);
    m_loginLabel->setText(user.login.isEmpty() ? QStringLiteral("—") : user.login);
    m_roleLabel->setText(user.rolePresent ? QString::number(user.roleId) : QStringLiteral("не назначена"));
    m_commandsLabel->setText(user.commandsAllowed ? QStringLiteral("да") : QStringLiteral("нет"));
    m_expiresLabel->setText(user.expiresAt.isEmpty() ? QStringLiteral("—") : user.expiresAt);
    setServerAddress(baseUrl);
}

void MainWindow::setServerAddress(const QString &baseUrl)
{
    m_serverLabel->setText(baseUrl);
}

void MainWindow::setLiveStatus(const QString &health, const QString &socketText)
{
    m_healthLabel->setText(health);
    m_socketLabel->setText(socketText);
    statusBar()->showMessage(health + QStringLiteral(" | ") + socketText);
}

QStackedWidget *MainWindow::workspace() const
{
    return m_workspace;
}

void MainWindow::setWorkspacePage(QWidget *page)
{
    if (!page)
        return;
    if (m_workspace->indexOf(page) < 0)
        m_workspace->addWidget(page);
    m_workspace->setCurrentWidget(page);
}
