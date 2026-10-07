#pragma once

#include "apiclient.h"

#include <QWidget>

class QLabel;
class QTableWidget;

// Read-only room list. Calls GET /api/v1/rooms?lang= and does not open MariaDB.
class DictionariesPage : public QWidget {
    Q_OBJECT

public:
    explicit DictionariesPage(ApiClient *api, QWidget *parent = nullptr);
    void reload();

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslateUi();
    void applyResponse(const ApiResponse &response);
    static QString statusText(const QString &code);

    ApiClient *m_api = nullptr;
    quint64 m_requestId = 0;
    bool m_loaded = false;

    QLabel *m_title = nullptr;
    QLabel *m_status = nullptr;
    QTableWidget *m_table = nullptr;
};
