#pragma once

#include <QDate>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

// The chart fills its widget. Day columns and room rows share that space;
// they are not a fixed 14-by-N canvas inside a scroll area.
constexpr int kRackLabelWidth = 148;
constexpr int kRackHeaderHeight = 48;
constexpr int kRackMinColumnWidth = 28;
constexpr int kRackMinRowHeight = 22;

struct RackFit {
    int labelWidth = kRackLabelWidth;
    int headerHeight = kRackHeaderHeight;
    int gridWidth = 0;
    int gridHeight = 0;
    int visibleDays = 1;
    int visibleRows = 0;
};

RackFit rackFit(const QSize &size, int roomCount);

// Pixel start of item `index` when `count` items share `total` pixels.
// index == count returns total, so the pieces add up to the full width or height.
int rackSpanStart(int index, int count, int total);

inline QColor rackGridColor()
{
    return QColor(0x8d, 0x8a, 0x82);
}

inline QColor rackSelectionColor()
{
    return QColor(186, 214, 246);
}

inline QColor rackReservedColor()
{
    return QColor(0xf6, 0xc4, 0x45);
}

struct RackBlock {
    qint64 stayId = 0;
    qint64 reservationId = 0;
    QString guestName;
    QString stateCode;
    QString reservationStatus;
    QDate arrival;
    QDate departure;
};

struct RackRoom {
    qint64 id = 0;
    QString code;
    QString typeCode;
    QString typeName;
    QString buildingCode;
    QString buildingName;
    bool hasFloor = false;
    int floor = 0;
    QString statusCode;
    QVector<RackBlock> blocks;
};

// Empty lists mean "no restriction". Within one list a room matches any value.
// Lists combine: a room must match every list that is not empty.
struct RackFilter {
    QStringList typeCodes;
    QStringList buildingCodes;
    QStringList floors;
    QStringList statusCodes;
    QString roomQuery;
};

// Painted rack. Rooms come from GET /api/v1/rack. Filtering and scrolling
// the visible window do not open MariaDB and do not wait for the network.
class RackChart : public QWidget {
    Q_OBJECT

public:
    explicit RackChart(QWidget *parent = nullptr);

    void setRooms(QVector<RackRoom> rooms);
    void setFilter(const RackFilter &filter);
    void setOrigin(const QDate &origin);

    QDate origin() const;
    void scrollDays(int delta);
    void scrollRooms(int delta);
    bool canScrollUp() const;
    bool canScrollDown() const;
    int visibleDayCount() const;
    int visibleRowCount() const;
    int filteredRoomCount() const;
    QStringList roomCodes() const;

signals:
    void reservationActivated(qint64 reservationId);
    void createReservationRequested(qint64 roomId, const QDate &arrival, const QDate &departure);
    void originChanged(const QDate &origin);
    void viewportChanged();
    void navigationChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    RackFit currentFit() const;
    void rebuildFiltered();
    void clampOffset();
    bool matches(const RackRoom &room) const;
    int dayIndexAt(int x) const;
    int visualRowAt(int y) const;
    QDate dateAtDay(int dayIndex) const;
    QRect dayRect(int dayIndex) const;
    QRect rowRect(int visualRow) const;
    const RackBlock *blockAt(int filteredIndex, const QDate &date) const;
    void showContextMenu(const QPoint &pos);
    void showTip(const QPoint &pos, const QPoint &globalPos);
    QString stayText(const QString &state) const;
    QString reservationStatusText(const QString &status) const;
    QString roomStatusText(const QString &status) const;

    QDate m_origin;
    QVector<RackRoom> m_rooms;
    QVector<RackRoom> m_filtered;
    RackFilter m_filter;
    int m_roomOffset = 0;
    int m_lastVisibleDays = -1;

    bool m_dragging = false;
    bool m_pressOnBlock = false;
    qint64 m_pressReservationId = 0;
    QPoint m_pressPos;
    int m_selRoom = -1;
    QDate m_selA;
    QDate m_selB;
};
