#include "loginwindow.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

LoginWindow::LoginWindow(ApiClient *api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{
    setWindowTitle(QStringLiteral("Вход"));
    resize(480, 420);
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("Отель"));
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 6);
    titleFont.setBold(true);
    title->setFont(titleFont);

    m_serverLabel = new QLabel;
    m_serverLabel->setObjectName(QStringLiteral("serverLabel"));
    m_serverLabel->setWordWrap(true);
    m_serverLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_statusLabel = new QLabel(QStringLiteral("Проверка соединения…"));
    m_statusLabel->setObjectName(QStringLiteral("connectionStatusLabel"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_loginEdit = new QLineEdit;
    m_loginEdit->setObjectName(QStringLiteral("loginEdit"));
    m_loginEdit->setClearButtonEnabled(true);

    m_passwordEdit = new QLineEdit;
    m_passwordEdit->setObjectName(QStringLiteral("passwordEdit"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Логин"), m_loginEdit);
    form->addRow(QStringLiteral("Пароль"), m_passwordEdit);

    auto *hint = new QLabel(QStringLiteral("Логин запоминается, пароль — нет."));
    hint->setWordWrap(true);

    m_errorLabel = new QLabel;
    m_errorLabel->setObjectName(QStringLiteral("errorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setMinimumHeight(48);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #a40000;"));

    m_loginButton = new QPushButton(QStringLiteral("Войти"));
    m_loginButton->setObjectName(QStringLiteral("loginButton"));
    m_loginButton->setDefault(true);

    m_settingsButton = new QPushButton(QStringLiteral("Настройки подключения"));
    m_settingsButton->setObjectName(QStringLiteral("settingsButton"));
    m_settingsButton->setAutoDefault(false);

    layout->addWidget(title);
    layout->addWidget(m_serverLabel);
    layout->addWidget(m_statusLabel);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(m_errorLabel);
    layout->addWidget(m_loginButton);
    layout->addWidget(m_settingsButton);
    layout->addStretch();

    connect(m_loginButton, &QPushButton::clicked, this, &LoginWindow::submit);
    connect(m_loginEdit, &QLineEdit::returnPressed, this, [this]() { m_passwordEdit->setFocus(); });
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LoginWindow::submit);
    connect(m_settingsButton, &QPushButton::clicked, this, &LoginWindow::openSettingsRequested);
    connect(m_api, &ApiClient::loginFinished, this, &LoginWindow::onLoginFinished);
}

void LoginWindow::setServerAddress(const QString &baseUrl)
{
    m_serverLabel->setText(QStringLiteral("Сервер: %1").arg(baseUrl));
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
    m_errorLabel->setText(text);
}

void LoginWindow::prepareForShow()
{
    m_passwordEdit->clear();
    m_errorLabel->clear();
    m_loginButton->setEnabled(true);
    m_loginButton->setText(QStringLiteral("Войти"));
    m_settingsButton->setEnabled(true);
}

void LoginWindow::submit()
{
    if (!m_loginButton->isEnabled())
        return;

    const QString login = m_loginEdit->text().trimmed();
    const QString password = m_passwordEdit->text();
    if (login.isEmpty() || password.isEmpty()) {
        m_errorLabel->setText(QStringLiteral("Введите логин и пароль."));
        return;
    }

    m_errorLabel->clear();
    m_loginButton->setEnabled(false);
    m_loginButton->setText(QStringLiteral("Вход…"));
    m_settingsButton->setEnabled(false);
    emit rememberLogin(login);
    m_api->clearToken();
    m_api->requestLogin(login, password);
}

void LoginWindow::onLoginFinished(const SessionResult &result)
{
    m_loginButton->setEnabled(true);
    m_loginButton->setText(QStringLiteral("Войти"));
    m_settingsButton->setEnabled(true);
    if (!result.current)
        return;
    if (!result.ok) {
        m_errorLabel->setText(result.error.userMessage);
        if (result.error.httpStatus == 401) {
            m_passwordEdit->clear();
            m_passwordEdit->setFocus();
        }
        return;
    }

    m_passwordEdit->clear();
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
