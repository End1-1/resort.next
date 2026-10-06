#pragma once

#include "apiclient.h"
#include "appconfig.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

class ConnectionDialog : public QDialog {
    Q_OBJECT

public:
    explicit ConnectionDialog(const DesktopConfig &current, QWidget *parent = nullptr);

    DesktopConfig settings() const;

public slots:
    void accept() override;

protected:
    void changeEvent(QEvent *event) override;

private slots:
    void checkConnection();

private:
    void showProbe(const HealthStatus &status);
    bool takeForm(DesktopConfig *config, QString *error) const;
    void refreshPathHint();
    void retranslateUi();

    DesktopConfig m_initial;
    DesktopConfig m_result;
    int m_probeId = 0;
    bool m_checking = false;
    bool m_haveHealth = false;
    bool m_urlError = false;
    HealthStatus m_lastHealth;

    QLabel *m_baseLabel = nullptr;
    QLabel *m_baseHint = nullptr;
    QLabel *m_wsLabel = nullptr;
    QLabel *m_wsHint = nullptr;
    QLineEdit *m_baseEdit = nullptr;
    QLineEdit *m_webSocketEdit = nullptr;
    QPushButton *m_checkButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QLabel *m_probeStatus = nullptr;
    QLabel *m_pathLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
};
