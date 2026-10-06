#pragma once

#include "apiclient.h"

#include <QWidget>

class QDateEdit;
class QLabel;

// Painted rack: one row per room, one column per night. Calls
// GET /api/v1/rack?from=&to=&lang= and does not open MariaDB.
class RackPage : public QWidget {
    Q_OBJECT

public:
    explicit RackPage(ApiClient *api, QWidget *parent = nullptr);
    void reload();

signals:
    void reservationActivated(qint64 reservationId);

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslateUi();
    void applyResponse(const ApiResponse &response);

    ApiClient *m_api = nullptr;
    quint64 m_requestId = 0;
    bool m_loaded = false;

    QLabel *m_title = nullptr;
    QLabel *m_fromLabel = nullptr;
    QLabel *m_toLabel = nullptr;
    QDateEdit *m_from = nullptr;
    QDateEdit *m_to = nullptr;
    class QPushButton *m_show = nullptr;
    QLabel *m_status = nullptr;
    class ChartGrid *m_grid = nullptr;
};
