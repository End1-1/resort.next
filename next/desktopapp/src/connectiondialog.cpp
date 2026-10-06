#include "connectiondialog.h"

#include "apiclient.h"
#include "urlutil.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

ConnectionDialog::ConnectionDialog(const DesktopConfig &current, QWidget *parent)
    : QDialog(parent)
    , m_initial(current)
    , m_result(current)
{
    setWindowTitle(QStringLiteral("Настройки подключения"));
    setMinimumWidth(520);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    m_baseEdit = new QLineEdit(current.baseUrl);
    m_baseEdit->setObjectName(QStringLiteral("baseUrlEdit"));
    m_baseEdit->setPlaceholderText(QStringLiteral("127.0.0.1:8080"));
    m_baseEdit->setClearButtonEnabled(true);

    auto *baseHint = new QLabel(QStringLiteral(
        "Хост и порт или URL. Сейчас сервер говорит по http; адрес https можно сохранить заранее."));
    baseHint->setWordWrap(true);

    m_webSocketEdit = new QLineEdit(current.webSocketUrl);
    m_webSocketEdit->setObjectName(QStringLiteral("webSocketUrlEdit"));
    m_webSocketEdit->setPlaceholderText(QStringLiteral("ws://127.0.0.1:8081"));
    m_webSocketEdit->setClearButtonEnabled(true);

    auto *wsHint = new QLabel(QStringLiteral(
        "Необязательно. Пусто — состояние только из GET /health. Если путь не указан, подставляется /api/v1/ws."));
    wsHint->setWordWrap(true);

    form->addRow(QStringLiteral("Адрес сервера"), m_baseEdit);
    form->addRow(QString(), baseHint);
    form->addRow(QStringLiteral("Адрес WebSocket"), m_webSocketEdit);
    form->addRow(QString(), wsHint);

    m_checkButton = new QPushButton(QStringLiteral("Проверить соединение"));
    m_checkButton->setObjectName(QStringLiteral("checkButton"));
    m_checkButton->setAutoDefault(false);

    m_probeStatus = new QLabel(QStringLiteral("Нажмите «Проверить соединение», чтобы запросить GET /health."));
    m_probeStatus->setObjectName(QStringLiteral("probeStatusLabel"));
    m_probeStatus->setWordWrap(true);
    m_probeStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_pathLabel = new QLabel;
    m_pathLabel->setObjectName(QStringLiteral("configPathLabel"));
    m_pathLabel->setWordWrap(true);
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_hintLabel = new QLabel;
    m_hintLabel->setObjectName(QStringLiteral("configHintLabel"));
    m_hintLabel->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    QPushButton *saveButton = buttons->button(QDialogButtonBox::Save);
    saveButton->setObjectName(QStringLiteral("saveButton"));
    saveButton->setText(QStringLiteral("Сохранить"));
    saveButton->setDefault(true);
    QPushButton *cancelButton = buttons->button(QDialogButtonBox::Cancel);
    cancelButton->setObjectName(QStringLiteral("cancelButton"));
    cancelButton->setText(QStringLiteral("Отмена"));
    cancelButton->setAutoDefault(false);

    layout->addLayout(form);
    layout->addWidget(m_checkButton);
    layout->addWidget(m_probeStatus);
    layout->addWidget(m_pathLabel);
    layout->addWidget(m_hintLabel);
    layout->addWidget(buttons);

    refreshPathHint();

    connect(m_checkButton, &QPushButton::clicked, this, &ConnectionDialog::checkConnection);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

DesktopConfig ConnectionDialog::settings() const
{
    return m_result;
}

void ConnectionDialog::refreshPathHint()
{
    const QString userPath = AppConfig::userFilePath();
    const QString bundledPath = AppConfig::bundledDefaultsPath();
    m_pathLabel->setText(QStringLiteral("Файл настроек: %1").arg(QDir::toNativeSeparators(userPath)));

    QString hint = QStringLiteral("Пароль в этот файл не записывается.");
    if (!QFileInfo::exists(userPath)) {
        if (QFileInfo::exists(bundledPath)) {
            hint = QStringLiteral(
                       "Личный файл ещё не создан. Начальные значения прочитаны из %1. "
                       "Этот файл программа не изменяет. Сохранение запишет личный файл выше. %2")
                       .arg(QDir::toNativeSeparators(bundledPath), hint);
        } else {
            hint = QStringLiteral(
                       "Личный файл ещё не создан и будет записан по пути выше при сохранении. %1")
                       .arg(hint);
        }
    }
    m_hintLabel->setText(hint);
}

bool ConnectionDialog::takeForm(DesktopConfig *config, QString *error) const
{
    const UrlParse base = parseServerBase(m_baseEdit->text());
    if (!base.ok) {
        *error = base.error;
        return false;
    }
    const UrlParse socket = parseWebSocketUrl(m_webSocketEdit->text());
    if (!socket.ok) {
        *error = socket.error;
        return false;
    }
    *config = m_initial;
    config->baseUrl = base.url;
    config->webSocketUrl = socket.url;
    return true;
}

void ConnectionDialog::checkConnection()
{
    QString error;
    DesktopConfig draft;
    if (!takeForm(&draft, &error)) {
        m_probeStatus->setText(error);
        return;
    }

    m_checkButton->setEnabled(false);
    m_probeStatus->setText(QStringLiteral("Проверка…"));
    const int probeId = ++m_probeId;

    auto *client = new ApiClient(this);
    client->setBaseUrl(draft.baseUrl);
    QPointer<ConnectionDialog> guard(this);
    connect(client, &ApiClient::healthFinished, client, [guard, client, probeId](const HealthStatus &status) {
        if (!status.current)
            return;
        client->deleteLater();
        if (!guard || guard->m_probeId != probeId)
            return;
        guard->showProbe(status);
    });
    client->requestHealth();
}

void ConnectionDialog::showProbe(const HealthStatus &status)
{
    m_checkButton->setEnabled(true);
    m_probeStatus->setText(healthSummary(status));
}

void ConnectionDialog::accept()
{
    QString error;
    DesktopConfig next;
    if (!takeForm(&next, &error)) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(QStringLiteral("Настройки подключения"));
        box.setText(error);
        box.addButton(QStringLiteral("Закрыть"), QMessageBox::AcceptRole);
        box.exec();
        return;
    }
    if (!AppConfig::save(next, &error)) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(QStringLiteral("Настройки подключения"));
        box.setText(error);
        box.addButton(QStringLiteral("Закрыть"), QMessageBox::AcceptRole);
        box.exec();
        return;
    }
    m_result = next;
    QDialog::accept();
}
