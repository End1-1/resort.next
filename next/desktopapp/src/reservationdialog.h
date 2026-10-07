#pragma once

#include "apiclient.h"

#include <QDate>
#include <QDialog>

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;

// Create or edit one reservation. The server checks overlap, status changes, and version.
class ReservationDialog : public QDialog {
    Q_OBJECT

public:
    explicit ReservationDialog(ApiClient *api, QWidget *parent = nullptr);
    void load(qint64 reservationId);
    void loadNew(qint64 roomId, const QDate &arrival, const QDate &departure);

private:
    ApiResponse waitFor(quint64 requestId);
    void applyDetail(const QJsonObject &body);
    void fillRooms(const QJsonArray &items);
    void save();
    void sendState(const QString &stateCode);
    void sendStatus(const QString &statusCode);
    void showFailure(const ApiResponse &response);
    void retranslateUi();

    ApiClient *m_api = nullptr;
    qint64 m_id = 0;
    int m_version = 0;
    qint64 m_stayId = 0;
    QString m_stayState;
    bool m_hasPreset = false;
    qint64 m_presetRoomId = 0;
    QDate m_presetArrival;
    QDate m_presetDeparture;

    QLabel *m_error = nullptr;
    QLineEdit *m_lastName = nullptr;
    QLineEdit *m_firstName = nullptr;
    QComboBox *m_room = nullptr;
    QDateEdit *m_arrival = nullptr;
    QDateEdit *m_departure = nullptr;
    QComboBox *m_status = nullptr;
    QLineEdit *m_remarks = nullptr;
    QLabel *m_stay = nullptr;
    QPushButton *m_checkIn = nullptr;
    QPushButton *m_checkOut = nullptr;
    QPushButton *m_cancelStay = nullptr;
    QPushButton *m_save = nullptr;
};
