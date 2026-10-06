#include "mainwindow.h"

#include <QAction>
#include <QEvent>
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
    resize(960, 640);

    m_logoutAction = new QAction(this);
    m_logoutAction->setObjectName(QStringLiteral("logoutAction"));
    m_settingsAction = new QAction(this);
    m_settingsAction->setObjectName(QStringLiteral("connectionSettingsAction"));

    m_sessionMenu = menuBar()->addMenu(QString());
    m_sessionMenu->addAction(m_logoutAction);
    m_settingsMenu = menuBar()->addMenu(QString());
    m_settingsMenu->setObjectName(QStringLiteral("settingsMenu"));
    m_settingsMenu->addAction(m_settingsAction);
    m_languages = HotelLocale::makeLanguageMenu(this);
    m_languages.menu->setObjectName(QStringLiteral("languageMenu"));
    m_settingsMenu->addMenu(m_languages.menu);

    connect(m_logoutAction, &QAction::triggered, this, &MainWindow::logoutRequested);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::openSettingsRequested);
    const auto requestLanguage = [this](QAction *action) {
        connect(action, &QAction::triggered, this, [this, action]() {
            emit languageRequested(action->data().toString());
        });
    };
    requestLanguage(m_languages.armenian);
    requestLanguage(m_languages.english);
    requestLanguage(m_languages.russian);

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

    m_healthLabel = new QLabel;
    m_healthLabel->setObjectName(QStringLiteral("healthLabel"));
    m_healthLabel->setWordWrap(true);
    m_healthLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_socketLabel = new QLabel;
    m_socketLabel->setObjectName(QStringLiteral("socketLabel"));
    m_socketLabel->setWordWrap(true);

    m_userCaption = new QLabel;
    m_loginCaption = new QLabel;
    m_roleCaption = new QLabel;
    m_commandsCaption = new QLabel;
    m_expiresCaption = new QLabel;
    m_serverCaption = new QLabel;
    m_healthCaption = new QLabel;
    m_socketCaption = new QLabel;

    auto *form = new QFormLayout;
    form->addRow(m_userCaption, m_userLabel);
    form->addRow(m_loginCaption, m_loginLabel);
    form->addRow(m_roleCaption, m_roleLabel);
    form->addRow(m_commandsCaption, m_commandsLabel);
    form->addRow(m_expiresCaption, m_expiresLabel);
    form->addRow(m_serverCaption, m_serverLabel);
    form->addRow(m_healthCaption, m_healthLabel);
    form->addRow(m_socketCaption, m_socketLabel);

    m_sessionBox = new QGroupBox;
    m_sessionBox->setLayout(form);

    m_workspace = new QStackedWidget;
    m_workspace->setObjectName(QStringLiteral("workspace"));

    auto *placeholder = new QWidget;
    placeholder->setObjectName(QStringLiteral("workspacePlaceholder"));
    auto *placeholderLayout = new QVBoxLayout(placeholder);
    m_placeholderLabel = new QLabel;
    m_placeholderLabel->setObjectName(QStringLiteral("workspacePlaceholderLabel"));
    m_placeholderLabel->setAlignment(Qt::AlignCenter);
    m_placeholderLabel->setWordWrap(true);
    placeholderLayout->addWidget(m_placeholderLabel);
    m_workspace->addWidget(placeholder);

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(m_sessionBox);
    layout->addWidget(m_workspace, 1);
    setCentralWidget(central);

    retranslateUi();
}

void MainWindow::showSession(const UserSnapshot &user, const QString &baseUrl)
{
    m_hasSession = true;
    m_session = user;
    m_baseUrl = baseUrl;
    applySession();
}

void MainWindow::applySession()
{
    m_userLabel->setText(m_session.name.isEmpty() ? QStringLiteral("—") : m_session.name);
    m_loginLabel->setText(m_session.login.isEmpty() ? QStringLiteral("—") : m_session.login);
    m_roleLabel->setText(m_session.rolePresent ? QString::number(m_session.roleId) : tr("not assigned"));
    m_commandsLabel->setText(m_session.commandsAllowed ? tr("yes") : tr("no"));
    m_expiresLabel->setText(m_session.expiresAt.isEmpty() ? QStringLiteral("—") : m_session.expiresAt);
    setServerAddress(m_baseUrl);
}

void MainWindow::setServerAddress(const QString &baseUrl)
{
    m_baseUrl = baseUrl;
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

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QMainWindow::changeEvent(event);
}

void MainWindow::retranslateUi()
{
    setWindowTitle(tr("Hotel"));
    m_sessionMenu->setTitle(tr("Session"));
    m_settingsMenu->setTitle(tr("Settings"));
    m_logoutAction->setText(tr("Sign out"));
    m_settingsAction->setText(tr("Connection settings"));
    m_languages.menu->setTitle(tr("Language"));
    HotelLocale::syncLanguageMenu(m_languages);

    m_userCaption->setText(tr("User"));
    m_loginCaption->setText(tr("Login"));
    m_roleCaption->setText(tr("Role (role_id)"));
    m_commandsCaption->setText(tr("Commands allowed"));
    m_expiresCaption->setText(tr("Session until"));
    m_serverCaption->setText(tr("Server"));
    m_healthCaption->setText(tr("Status"));
    m_socketCaption->setText(tr("Channel"));
    m_sessionBox->setTitle(tr("Current session"));
    m_placeholderLabel->setText(tr("Work screens will appear here: the room rack, reservations, and other modules."));
    if (m_hasSession)
        applySession();
}
