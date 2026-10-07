#pragma once

#include "apiclient.h"

#include <QTabWidget>

class DictionariesPage;
class RackPage;
class ReservationsPage;

// Rack chart first, room list beside it. Both talk only to hotel-api.
class WorkspacePage : public QTabWidget {
    Q_OBJECT

public:
    explicit WorkspacePage(ApiClient *api, QWidget *parent = nullptr);
    void reload();
    void setCommandsAllowed(bool allowed);

signals:
    void reservationActivated(qint64 reservationId);

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslateUi();

    RackPage *m_rack = nullptr;
    ReservationsPage *m_reservations = nullptr;
    DictionariesPage *m_rooms = nullptr;
};
