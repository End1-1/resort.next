#include "connectiondialog.h"

#include "apiclient.h"
#include "urlutil.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
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
    setMinimumWidth(520);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    m_baseEdit = new QLineEdit(current.baseUrl);
    m_baseEdit->setObjectName(QStringLiteral("baseUrlEdit"));
    m_baseEdit->setPlaceholderText(QStringLiteral("127.0.0.1:8080"));
    m_baseEdit->setClearButtonEnabled(true);

    m_baseHint = new QLabel;
    m_baseHint->setWordWrap(true);

    m_webSocketEdit = new QLineEdit(current.webSocketUrl);
    m_webSocketEdit->setObjectName(QStringLiteral("webSocketUrlEdit"));
    m_webSocketEdit->setPlaceholderText(QStringLiteral("ws://127.0.0.1:8081"));
    m_webSocketEdit->setClearButtonEnabled(true);

    m_wsHint = new QLabel;
    m_wsHint->setWordWrap(true);

    m_baseLabel = new QLabel;
    m_wsLabel = new QLabel;
    form->addRow(m_baseLabel, m_baseEdit);
    form->addRow(QString(), m_baseHint);
    form->addRow(m_wsLabel, m_webSocketEdit);
    form->addRow(QString(), m_wsHint);

    m_checkButton = new QPushButton;
    m_checkButton->setObjectName(QStringLiteral("checkButton"));
    m_checkButton->setAutoDefault(false);

    m_probeStatus = new QLabel;
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
    m_saveButton = buttons->button(QDialogButtonBox::Save);
    m_saveButton->setObjectName(QStringLiteral("saveButton"));
    m_saveButton->setDefault(true);
    m_cancelButton = buttons->button(QDialogButtonBox::Cancel);
    m_cancelButton->setObjectName(QStringLiteral("cancelButton"));
    m_cancelButton->setAutoDefault(false);

    layout->addLayout(form);
    layout->addWidget(m_checkButton);
    layout->addWidget(m_probeStatus);
    layout->addWidget(m_pathLabel);
    layout->addWidget(m_hintLabel);
    layout->addWidget(buttons);

    connect(m_checkButton, &QPushButton::clicked, this, &ConnectionDialog::checkConnection);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    retranslateUi();
}

DesktopConfig ConnectionDialog::settings() const
{
    return m_result;
}

void ConnectionDialog::refreshPathHint()
{
    const QString userPath = AppConfig::userFilePath();
    const QString bundledPath = AppConfig::bundledDefaultsPath();
    m_pathLabel->setText(tr("Settings file: %1").arg(QDir::toNativeSeparators(userPath)));

    const QString passwordHint = tr("The password is not written to this file.");
    QString hint = passwordHint;
    if (!QFileInfo::exists(userPath)) {
        if (QFileInfo::exists(bundledPath)) {
            hint = tr("The personal file does not exist yet. Initial values were read from %1. "
                      "The program does not change that file. Saving writes the personal file shown above. %2")
                       .arg(QDir::toNativeSeparators(bundledPath), passwordHint);
        } else {
            hint = tr("The personal file does not exist yet and will be written to the path above when you save. %1")
                       .arg(passwordHint);
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
        m_urlError = true;
        m_haveHealth = false;
        m_checking = false;
        m_probeStatus->setText(error);
        return;
    }

    m_checking = true;
    m_urlError = false;
    m_haveHealth = false;
    m_checkButton->setEnabled(false);
    m_probeStatus->setText(tr("Checking…"));
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
    m_checking = false;
    m_urlError = false;
    m_haveHealth = true;
    m_lastHealth = status;
    m_checkButton->setEnabled(true);
    m_checkButton->setText(tr("Check connection"));
    m_probeStatus->setText(healthSummary(status));
}

void ConnectionDialog::accept()
{
    QString error;
    DesktopConfig next;
    if (!takeForm(&next, &error)) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Connection settings"));
        box.setText(error);
        box.addButton(tr("Close"), QMessageBox::AcceptRole);
        box.exec();
        return;
    }
    if (!AppConfig::save(next, &error)) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Connection settings"));
        box.setText(error);
        box.addButton(tr("Close"), QMessageBox::AcceptRole);
        box.exec();
        return;
    }
    m_result = next;
    QDialog::accept();
}

void ConnectionDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void ConnectionDialog::retranslateUi()
{
    setWindowTitle(tr("Connection settings"));
    m_baseLabel->setText(tr("Server address"));
    m_baseHint->setText(tr("Host and port, or a URL. The server speaks http today; an https address can be saved in advance."));
    m_wsLabel->setText(tr("WebSocket address"));
    m_wsHint->setText(tr("Optional. Empty means status comes only from GET /health. If the path is omitted, /api/v1/ws is used."));
    m_checkButton->setText(m_checking ? tr("Checking…") : tr("Check connection"));
    m_saveButton->setText(tr("Save"));
    m_cancelButton->setText(tr("Cancel"));
    refreshPathHint();
    if (m_haveHealth)
        m_probeStatus->setText(healthSummary(m_lastHealth));
    else if (m_urlError) {
        QString error;
        DesktopConfig draft;
        if (!takeForm(&draft, &error))
            m_probeStatus->setText(error);
    } else if (!m_checking) {
        m_probeStatus->setText(tr("Press \"Check connection\" to request GET /health."));
    }
}
