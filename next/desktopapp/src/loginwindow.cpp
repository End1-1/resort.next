#include "loginwindow.h"

#include "urlutil.h"

#include <QAction>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QToolButton>
#include <QVBoxLayout>

LoginWindow::LoginWindow(ApiClient *api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    resize(480, 440);
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(10);

    m_titleLabel = new QLabel;
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 6);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);

    m_languageButton = new QToolButton;
    m_languageButton->setObjectName(QStringLiteral("languageButton"));
    m_languageButton->setPopupMode(QToolButton::InstantPopup);
    m_languageButton->setAutoRaise(true);
    m_languageButton->setIconSize(QSize(22, 14));
    m_languageButton->setFixedSize(40, 28);
    m_languages = HotelLocale::makeLanguageMenu(this);
    m_languageButton->setMenu(m_languages.menu);

    auto *header = new QHBoxLayout;
    header->addWidget(m_titleLabel);
    header->addStretch();
    header->addWidget(m_languageButton);

    m_serverLabel = new QLabel;
    m_serverLabel->setObjectName(QStringLiteral("serverLabel"));
    m_serverLabel->setWordWrap(true);
    m_serverLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_statusLabel = new QLabel;
    m_statusLabel->setObjectName(QStringLiteral("connectionStatusLabel"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_loginEdit = new QLineEdit;
    m_loginEdit->setObjectName(QStringLiteral("loginEdit"));
    m_loginEdit->setClearButtonEnabled(true);

    m_passwordEdit = new QLineEdit;
    m_passwordEdit->setObjectName(QStringLiteral("passwordEdit"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    m_loginLabel = new QLabel;
    m_passwordLabel = new QLabel;
    auto *form = new QFormLayout;
    form->addRow(m_loginLabel, m_loginEdit);
    form->addRow(m_passwordLabel, m_passwordEdit);

    m_hintLabel = new QLabel;
    m_hintLabel->setWordWrap(true);

    m_errorLabel = new QLabel;
    m_errorLabel->setObjectName(QStringLiteral("errorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setMinimumHeight(48);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #a40000;"));

    m_loginButton = new QPushButton;
    m_loginButton->setObjectName(QStringLiteral("loginButton"));
    m_loginButton->setDefault(true);

    m_settingsButton = new QPushButton;
    m_settingsButton->setObjectName(QStringLiteral("settingsButton"));
    m_settingsButton->setAutoDefault(false);

    layout->addLayout(header);
    layout->addWidget(m_serverLabel);
    layout->addWidget(m_statusLabel);
    layout->addLayout(form);
    layout->addWidget(m_hintLabel);
    layout->addWidget(m_errorLabel);
    layout->addWidget(m_loginButton);
    layout->addWidget(m_settingsButton);
    layout->addStretch();

    connect(m_loginButton, &QPushButton::clicked, this, &LoginWindow::submit);
    connect(m_loginEdit, &QLineEdit::returnPressed, this, [this]() { m_passwordEdit->setFocus(); });
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LoginWindow::submit);
    connect(m_settingsButton, &QPushButton::clicked, this, &LoginWindow::openSettingsRequested);
    connect(m_api, &ApiClient::loginFinished, this, &LoginWindow::onLoginFinished);
    const auto requestLanguage = [this](QAction *action) {
        connect(action, &QAction::triggered, this, [this, action]() {
            emit languageRequested(action->data().toString());
        });
    };
    requestLanguage(m_languages.armenian);
    requestLanguage(m_languages.english);
    requestLanguage(m_languages.russian);

    retranslateUi();
}

void LoginWindow::setServerAddress(const QString &baseUrl)
{
    m_baseUrl = baseUrl;
    m_serverLabel->setText(tr("Server: %1").arg(baseUrl));
}

void LoginWindow::setConnectionStatus(const QString &text)
{
    m_statusLabel->setText(text);
}

void LoginWindow::setLastLogin(const QString &login)
{
    if (m_loginEdit->text().isEmpty())
        m_loginEdit->setText(login);
}

void LoginWindow::setError(const QString &text)
{
    m_notice = text.isEmpty() ? Notice::None : Notice::External;
    m_externalError = text;
    m_sessionEndedCode.clear();
    m_errorLabel->setText(text);
}

void LoginWindow::setSessionEnded(const QString &code)
{
    m_notice = Notice::SessionEnded;
    m_sessionEndedCode = code;
    m_externalError.clear();
    showStoredError();
}

void LoginWindow::prepareForShow()
{
    m_passwordEdit->clear();
    m_notice = Notice::None;
    m_externalError.clear();
    m_sessionEndedCode.clear();
    m_errorLabel->clear();
    m_busy = false;
    m_loginButton->setEnabled(true);
    m_settingsButton->setEnabled(true);
    retranslateUi();
}

void LoginWindow::submit()
{
    if (!m_loginButton->isEnabled())
        return;

    const QString login = m_loginEdit->text().trimmed();
    const QString password = m_passwordEdit->text();
    if (login.isEmpty() || password.isEmpty()) {
        m_notice = Notice::NeedCredentials;
        m_errorLabel->setText(tr("Enter your login and password."));
        return;
    }

    m_notice = Notice::None;
    m_errorLabel->clear();
    m_busy = true;
    m_loginButton->setEnabled(false);
    m_loginButton->setText(tr("Signing in…"));
    m_settingsButton->setEnabled(false);
    emit rememberLogin(login);
    m_api->clearToken();
    m_api->requestLogin(login, password);
}

void LoginWindow::onLoginFinished(const SessionResult &result)
{
    m_busy = false;
    m_loginButton->setEnabled(true);
    m_loginButton->setText(tr("Sign in", "button"));
    m_settingsButton->setEnabled(true);
    if (!result.current)
        return;
    if (!result.ok) {
        m_notice = Notice::Server;
        m_lastError = result.error;
        showStoredError();
        if (result.error.httpStatus == 401) {
            m_passwordEdit->clear();
            m_passwordEdit->setFocus();
        }
        return;
    }

    m_passwordEdit->clear();
    m_notice = Notice::None;
    m_errorLabel->clear();
    m_api->setToken(result.token);
    emit loginSucceeded(userSnapshotFrom(result));
}

void LoginWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_loginEdit->text().isEmpty())
        m_loginEdit->setFocus();
    else
        m_passwordEdit->setFocus();
}

void LoginWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void LoginWindow::retranslateUi()
{
    setWindowTitle(tr("Sign in", "window title"));
    m_titleLabel->setText(tr("Hotel"));
    m_loginLabel->setText(tr("Login"));
    m_passwordLabel->setText(tr("Password"));
    m_hintLabel->setText(tr("The login is remembered; the password is not."));
    m_loginButton->setText(m_busy ? tr("Signing in…") : tr("Sign in", "button"));
    m_settingsButton->setText(tr("Connection settings"));
    m_languageButton->setToolTip(tr("Language"));
    m_languageButton->setIcon(HotelLocale::flagIcon(HotelLocale::currentCode()));
    HotelLocale::syncLanguageMenu(m_languages);
    if (!m_baseUrl.isEmpty())
        m_serverLabel->setText(tr("Server: %1").arg(m_baseUrl));
    showStoredError();
}

void LoginWindow::showStoredError()
{
    switch (m_notice) {
    case Notice::None:
        m_errorLabel->clear();
        break;
    case Notice::NeedCredentials:
        m_errorLabel->setText(tr("Enter your login and password."));
        break;
    case Notice::Server:
        if (m_lastError.reparseBaseUrl) {
            const UrlParse parsed = parseServerBase(m_api->baseUrl());
            m_errorLabel->setText(parsed.ok ? userMessageFor(m_lastError) : parsed.error);
        } else {
            m_errorLabel->setText(userMessageFor(m_lastError));
        }
        break;
    case Notice::External:
        m_errorLabel->setText(m_externalError);
        break;
    case Notice::SessionEnded:
        m_errorLabel->setText(sessionEndedMessage(m_sessionEndedCode));
        break;
    }
}
