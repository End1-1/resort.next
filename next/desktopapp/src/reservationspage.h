#pragma once

#include "apiclient.h"

#include <QWidget>

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

// Search and open reservations. Writes go through ReservationDialog and the server.
class ReservationsPage : public QWidget {
    Q_OBJECT

public:
    explicit ReservationsPage(ApiClient *api, QWidget *parent = nullptr);
    void reload();

public slots:
    void openReservation(qint64 reservationId);

signals:
    void reservationsChanged();

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslateUi();
    void applyResponse(const ApiResponse &response);
    QString statusText(const QString &code) const;
    QString stayText(const QString &code) const;

    ApiClient *m_api = nullptr;
    quint64 m_requestId = 0;
    bool m_loaded = false;

    QLabel *m_title = nullptr;
    QLabel *m_status = nullptr;
    QDateEdit *m_from = nullptr;
    QDateEdit *m_to = nullptr;
    QLineEdit *m_guest = nullptr;
    QLineEdit *m_room = nullptr;
    QComboBox *m_statusFilter = nullptr;
    QPushButton *m_show = nullptr;
    QPushButton *m_create = nullptr;
    QTableWidget *m_table = nullptr;
};
