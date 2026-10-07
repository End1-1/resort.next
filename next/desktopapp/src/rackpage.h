#pragma once

#include "apiclient.h"
#include "rackchart.h"

#include <QDate>
#include <QWidget>

class QComboBox;
class QDateEdit;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

// Rack tab. The chart fills the page. Day and room scrolling is by buttons,
// and room-type / building / floor filters apply on the client immediately.
class RackPage : public QWidget {
    Q_OBJECT

public:
    explicit RackPage(ApiClient *api, QWidget *parent = nullptr);
    void reload();

signals:
    void reservationActivated(qint64 reservationId);
    void createReservationRequested(qint64 roomId, const QDate &arrival, const QDate &departure);

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslateUi();
    void ensureData();
    void applyResponse(const ApiResponse &response);
    void rebuildFilters();
    void applyFilter();
    void updateNav();
    QString roomStatusText(const QString &status) const;

    ApiClient *m_api = nullptr;
    quint64 m_requestId = 0;
    bool m_loaded = false;
    bool m_haveData = false;
    bool m_failed = false;
    bool m_force = false;
    bool m_adjusting = false;
    QDate m_dataFrom;
    QDate m_dataTo;
    QVector<RackRoom> m_rooms;
    QString m_filterSignature;

    QLabel *m_title = nullptr;
    QLabel *m_dateLabel = nullptr;
    QDateEdit *m_date = nullptr;
    QPushButton *m_today = nullptr;
    QPushButton *m_prevDay = nullptr;
    QPushButton *m_nextDay = nullptr;
    QPushButton *m_prevRoom = nullptr;
    QPushButton *m_nextRoom = nullptr;
    QLineEdit *m_roomQuery = nullptr;
    QComboBox *m_statusFilter = nullptr;
    QLabel *m_status = nullptr;
    QWidget *m_filterHost = nullptr;
    QHBoxLayout *m_filterLayout = nullptr;
    RackChart *m_chart = nullptr;
    QTimer *m_loadTimer = nullptr;
};
