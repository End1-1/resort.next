#pragma once

#include "appconfig.h"

#include <QDialog>

class ApiClient;
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

private slots:
    void checkConnection();

private:
    void showProbe(const struct HealthStatus &status);
    bool takeForm(DesktopConfig *config, QString *error) const;
    void refreshPathHint();

    DesktopConfig m_initial;
    DesktopConfig m_result;
    int m_probeId = 0;

    QLineEdit *m_baseEdit = nullptr;
    QLineEdit *m_webSocketEdit = nullptr;
    QPushButton *m_checkButton = nullptr;
    QLabel *m_probeStatus = nullptr;
    QLabel *m_pathLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
};
