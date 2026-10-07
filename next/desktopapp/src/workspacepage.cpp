#include "workspacepage.h"

#include "dictionariespage.h"
#include "rackpage.h"
#include "reservationspage.h"

#include <QEvent>

WorkspacePage::WorkspacePage(ApiClient *api, QWidget *parent)
    : QTabWidget(parent)
{
    setObjectName(QStringLiteral("workspaceTabs"));
    m_rack = new RackPage(api, this);
    m_reservations = new ReservationsPage(api, this);
    m_rooms = new DictionariesPage(api, this);
    addTab(m_rack, QString());
    addTab(m_reservations, QString());
    addTab(m_rooms, QString());
    connect(m_rack, &RackPage::reservationActivated, m_reservations, &ReservationsPage::openReservation);
    connect(m_reservations, &ReservationsPage::reservationsChanged, m_rack, &RackPage::reload);
    connect(m_rack, &RackPage::reservationActivated, this, &WorkspacePage::reservationActivated);
    retranslateUi();
}

void WorkspacePage::reload()
{
    if (m_rack)
        m_rack->reload();
    if (m_reservations)
        m_reservations->reload();
    if (m_rooms)
        m_rooms->reload();
}

void WorkspacePage::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QTabWidget::changeEvent(event);
}

void WorkspacePage::retranslateUi()
{
    setTabText(indexOf(m_rack), tr("Rack"));
    setTabText(indexOf(m_reservations), tr("Reservations"));
    setTabText(indexOf(m_rooms), tr("Rooms"));
}
