#include "rackchart.h"

#include <QEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>

namespace {

int compareCode(const QString &left, const QString &right)
{
    bool leftOk = false;
    bool rightOk = false;
    const qlonglong leftNumber = left.toLongLong(&leftOk);
    const qlonglong rightNumber = right.toLongLong(&rightOk);
    if (leftOk && rightOk && leftNumber != rightNumber)
        return leftNumber < rightNumber ? -1 : 1;
    return left.localeAwareCompare(right);
}

bool roomBefore(const RackRoom &left, const RackRoom &right)
{
    if (left.buildingCode != right.buildingCode)
        return left.buildingCode < right.buildingCode;
    const int code = compareCode(left.code, right.code);
    if (code != 0)
        return code < 0;
    return left.id < right.id;
}

int stateRank(const QString &state)
{
    if (state == QLatin1String("in_house"))
        return 4;
    if (state == QLatin1String("reserved"))
        return 3;
    if (state == QLatin1String("out_of_order") || state == QLatin1String("out_of_inventory"))
        return 2;
    return 1;
}

QColor blockColor(const QString &state)
{
    if (state == QLatin1String("reserved"))
        return rackReservedColor();
    if (state == QLatin1String("in_house"))
        return QColor(0x3f, 0x8f, 0xd6);
    if (state == QLatin1String("checked_out"))
        return QColor(0xb0, 0xb4, 0xba);
    if (state == QLatin1String("out_of_order"))
        return QColor(0xc4, 0x7b, 0x2b);
    if (state == QLatin1String("out_of_inventory"))
        return QColor(0x6b, 0x72, 0x7c);
    return rackReservedColor();
}

bool lightText(const QString &state)
{
    return state == QLatin1String("in_house") || state == QLatin1String("out_of_inventory");
}

QColor roomStatusColor(const QString &status)
{
    if (status == QLatin1String("occupied"))
        return QColor(0xb7, 0xd4, 0xff);
    if (status == QLatin1String("vacant_dirty"))
        return QColor(0xff, 0xf3, 0xa0);
    if (status == QLatin1String("out_of_order"))
        return QColor(0xd0, 0xd0, 0xd0);
    if (status == QLatin1String("out_of_inventory"))
        return QColor(0x9a, 0x9a, 0x9a);
    if (status == QLatin1String("house_use"))
        return QColor(0xe4, 0xd4, 0xf5);
    if (status == QLatin1String("complimentary"))
        return QColor(0xd4, 0xf5, 0xe4);
    return QColor(0xff, 0xff, 0xff);
}

QColor confirmationColor(const QString &status)
{
    if (status == QLatin1String("confirmed"))
        return QColor(0x2e, 0x8b, 0x57);
    if (status == QLatin1String("guaranteed"))
        return QColor(0xf0, 0xc4, 0x2e);
    if (status == QLatin1String("tentative"))
        return QColor(0x88, 0x88, 0x88);
    if (status == QLatin1String("blocked"))
        return Qt::white;
    return QColor(0x3b, 0x6f, 0xd8);
}

QString floorKey(const RackRoom &room)
{
    return room.hasFloor ? QString::number(room.floor) : QString();
}

} // namespace

RackFit rackFit(const QSize &size, int roomCount)
{
    RackFit fit;
    fit.gridWidth = qMax(0, size.width() - fit.labelWidth);
    fit.gridHeight = qMax(0, size.height() - fit.headerHeight);
    fit.visibleDays = qMax(1, fit.gridWidth / kRackMinColumnWidth);
    const int rowsThatFit = qMax(1, fit.gridHeight / kRackMinRowHeight);
    fit.visibleRows = roomCount <= 0 ? 0 : qMin(roomCount, rowsThatFit);
    return fit;
}

int rackSpanStart(int index, int count, int total)
{
    if (count <= 0 || total <= 0 || index <= 0)
        return 0;
    if (index >= count)
        return total;
    return int((qint64(index) * total) / count);
}

RackChart::RackChart(QWidget *parent)
    : QWidget(parent)
    , m_origin(QDate::currentDate())
{
    setObjectName(QStringLiteral("rackChart"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(kRackLabelWidth + kRackMinColumnWidth, kRackHeaderHeight + kRackMinRowHeight);
}

void RackChart::setRooms(QVector<RackRoom> rooms)
{
    m_rooms = std::move(rooms);
    std::sort(m_rooms.begin(), m_rooms.end(), roomBefore);
    m_dragging = false;
    m_selRoom = -1;
    rebuildFiltered();
}

void RackChart::setFilter(const RackFilter &filter)
{
    m_filter = filter;
    m_dragging = false;
    m_selRoom = -1;
    rebuildFiltered();
}

void RackChart::setOrigin(const QDate &origin)
{
    if (!origin.isValid() || origin == m_origin)
        return;
    if (origin.year() < 2000 || origin.year() > 2100)
        return;
    m_origin = origin;
    update();
    emit originChanged(m_origin);
}

QDate RackChart::origin() const
{
    return m_origin;
}

void RackChart::scrollDays(int delta)
{
    setOrigin(m_origin.addDays(delta));
}

void RackChart::scrollRooms(int delta)
{
    if (delta == 0)
        return;
    const int next = qBound(0, m_roomOffset + delta, qMax(0, m_filtered.size() - currentFit().visibleRows));
    if (next == m_roomOffset)
        return;
    m_roomOffset = next;
    update();
    emit navigationChanged();
}

bool RackChart::canScrollUp() const
{
    return m_roomOffset > 0;
}

bool RackChart::canScrollDown() const
{
    return m_roomOffset + currentFit().visibleRows < m_filtered.size();
}

int RackChart::visibleDayCount() const
{
    return currentFit().visibleDays;
}

int RackChart::visibleRowCount() const
{
    return currentFit().visibleRows;
}

int RackChart::filteredRoomCount() const
{
    return m_filtered.size();
}

QStringList RackChart::roomCodes() const
{
    QStringList codes;
    codes.reserve(m_filtered.size());
    for (const RackRoom &room : m_filtered)
        codes.append(room.code);
    return codes;
}

void RackChart::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0xfa, 0xfa, 0xf8));
    const RackFit layout = currentFit();
    const QDate today = QDate::currentDate();
    const int days = layout.visibleDays;
    const int rows = layout.visibleRows;

    for (int day = 0; day < days; ++day) {
        const QRect column = dayRect(day);
        const QDate date = dateAtDay(day);
        if (date.dayOfWeek() == Qt::Saturday || date.dayOfWeek() == Qt::Sunday)
            painter.fillRect(column, QColor(0xf3, 0xef, 0xe4));
        if (date == today)
            painter.fillRect(column, QColor(255, 243, 214));
    }

    if (m_dragging && m_selRoom >= m_roomOffset && m_selRoom < m_roomOffset + rows && m_selA.isValid() && m_selB.isValid()) {
        const int visual = m_selRoom - m_roomOffset;
        const int first = qMax(0, int(m_origin.daysTo(qMin(m_selA, m_selB))));
        const int last = qMin(days - 1, int(m_origin.daysTo(qMax(m_selA, m_selB))));
        if (last >= first && visual >= 0 && visual < rows) {
            const QRect from = dayRect(first);
            const QRect to = dayRect(last);
            const QRect band = rowRect(visual);
            painter.fillRect(QRect(from.left(), band.top(), to.right() - from.left() + 1, band.height()), rackSelectionColor());
        }
    }

    for (int row = 0; row < rows; ++row) {
        const int index = m_roomOffset + row;
        const QRect band = rowRect(row);
        painter.fillRect(QRect(0, band.top(), layout.labelWidth, band.height()), roomStatusColor(m_filtered.at(index).statusCode));
    }

    painter.setPen(QPen(rackGridColor(), 1));
    painter.drawLine(layout.labelWidth, 0, layout.labelWidth, height() - 1);
    for (int day = 0; day <= days; ++day) {
        int x = layout.labelWidth + rackSpanStart(day, days, layout.gridWidth);
        x = qMin(x, width() - 1);
        painter.drawLine(x, 0, x, height() - 1);
    }
    painter.drawLine(0, layout.headerHeight, width() - 1, layout.headerHeight);
    painter.drawLine(0, 16, width() - 1, 16);
    for (int row = 0; row <= rows; ++row) {
        int y = layout.headerHeight;
        if (rows > 0)
            y += rackSpanStart(row, rows, layout.gridHeight);
        else if (row == 1)
            continue;
        y = qMin(y, height() - 1);
        painter.drawLine(0, y, width() - 1, y);
        if (row > 0 && row <= rows) {
            const int index = m_roomOffset + row;
            if (index < m_filtered.size() && m_filtered.at(index).buildingCode != m_filtered.at(index - 1).buildingCode) {
                painter.setPen(QPen(QColor(0x33, 0x33, 0x33), 2));
                painter.drawLine(0, y, width() - 1, y);
                painter.setPen(QPen(rackGridColor(), 1));
            }
        }
    }

    const QFontMetrics metrics(painter.font());
    for (int row = 0; row < rows; ++row) {
        const int index = m_roomOffset + row;
        const RackRoom &room = m_filtered.at(index);
        const QRect band = rowRect(row);
        for (const RackBlock &block : room.blocks) {
            const QDate viewEnd = m_origin.addDays(days);
            const QDate start = qMax(block.arrival, m_origin);
            const QDate end = qMin(block.departure, viewEnd);
            if (!start.isValid() || !end.isValid() || end <= start)
                continue;
            const int day0 = int(m_origin.daysTo(start));
            const int day1 = int(m_origin.daysTo(end));
            const QRect from = dayRect(day0);
            const QRect to = dayRect(qMax(day0, day1 - 1));
            const QRect bar(from.left() + 2, band.top() + 3, to.right() - from.left() - 3, qMax(4, band.height() - 6));
            painter.setPen(Qt::NoPen);
            painter.setBrush(blockColor(block.stateCode));
            painter.drawRoundedRect(bar, 3, 3);
            painter.setPen(QPen(QColor(0x44, 0x44, 0x44), 1));
            painter.setBrush(confirmationColor(block.reservationStatus));
            if (bar.width() > 18)
                painter.drawEllipse(QRect(bar.left() + 4, bar.center().y() - 4, 8, 8));
            painter.setPen(lightText(block.stateCode) ? Qt::white : QColor(0x22, 0x22, 0x22));
            const QString guest = block.guestName.isEmpty() ? tr("No guest") : block.guestName;
            const int textLeft = bar.width() > 18 ? 16 : 4;
            painter.drawText(bar.adjusted(textLeft, 0, -4, 0),
                             Qt::AlignVCenter | Qt::AlignLeft,
                             metrics.elidedText(guest, Qt::ElideRight, qMax(0, bar.width() - textLeft - 4)));
        }

        painter.setPen(QColor(0x22, 0x22, 0x22));
        QFont codeFont = painter.font();
        codeFont.setBold(true);
        painter.setFont(codeFont);
        const QRect label(4, band.top(), layout.labelWidth - 8, band.height());
        if (band.height() >= 36) {
            painter.drawText(QRect(label.left(), label.top() + 2, label.width(), label.height() / 2),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             metrics.elidedText(room.code, Qt::ElideRight, label.width()));
            painter.setFont(font());
            painter.setPen(QColor(0x44, 0x44, 0x44));
            const QString detail = room.typeName.isEmpty() ? room.buildingName : room.typeName;
            painter.drawText(QRect(label.left(), label.center().y(), label.width(), label.height() / 2),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             metrics.elidedText(detail, Qt::ElideRight, label.width()));
        } else {
            painter.drawText(label, Qt::AlignVCenter | Qt::AlignLeft, metrics.elidedText(room.code, Qt::ElideRight, label.width()));
            painter.setFont(font());
        }
    }

    painter.fillRect(QRect(0, 0, width(), layout.headerHeight), QColor(255, 255, 255, 210));
    painter.setPen(QPen(rackGridColor(), 1));
    painter.drawLine(0, layout.headerHeight, width() - 1, layout.headerHeight);
    painter.drawLine(0, 16, width() - 1, 16);
    for (int day = 0; day <= days; ++day) {
        int x = layout.labelWidth + rackSpanStart(day, days, layout.gridWidth);
        x = qMin(x, width() - 1);
        painter.drawLine(x, 0, x, layout.headerHeight);
    }

    QLocale locale;
    int monthDay = 0;
    while (monthDay < days) {
        const QDate start = dateAtDay(monthDay);
        int end = monthDay + 1;
        while (end < days) {
            const QDate next = dateAtDay(end);
            if (next.month() != start.month() || next.year() != start.year())
                break;
            ++end;
        }
        const QRect from = dayRect(monthDay);
        const QRect to = dayRect(end - 1);
        painter.setPen(QColor(0x22, 0x22, 0x22));
        painter.drawText(QRect(from.left(), 0, to.right() - from.left() + 1, 16),
                         Qt::AlignCenter,
                         metrics.elidedText(locale.toString(start, QStringLiteral("MMMM yyyy")),
                                            Qt::ElideRight,
                                            qMax(0, to.right() - from.left() - 4)));
        monthDay = end;
    }

    for (int day = 0; day < days; ++day) {
        const QDate date = dateAtDay(day);
        const QRect column = dayRect(day);
        const bool weekend = date.dayOfWeek() == Qt::Saturday || date.dayOfWeek() == Qt::Sunday;
        painter.setPen(weekend ? QColor(0xb0, 0x2a, 0x2a) : QColor(0x22, 0x22, 0x22));
        QFont dayFont = font();
        dayFont.setBold(true);
        painter.setFont(dayFont);
        painter.drawText(QRect(column.left(), 16, column.width(), 16), Qt::AlignCenter, QString::number(date.day()));
        painter.setFont(font());
        QString weekday = locale.dayName(date.dayOfWeek(), QLocale::NarrowFormat);
        if (weekday.isEmpty())
            weekday = locale.dayName(date.dayOfWeek(), QLocale::ShortFormat).left(2);
        painter.drawText(QRect(column.left(), 32, column.width(), 16), Qt::AlignCenter, weekday);
        if (date == today) {
            painter.fillRect(QRect(column.left(), 0, column.width(), 3), QColor(0xe0, 0x7a, 0x1f));
            painter.setPen(QColor(0x8a, 0x4b, 0x08));
            painter.drawText(QRect(column.left(), 16, column.width(), 16), Qt::AlignCenter, QString::number(date.day()));
        }
    }

    painter.fillRect(QRect(0, 0, layout.labelWidth, layout.headerHeight), QColor(0xff, 0xff, 0xff));
    painter.setPen(QPen(rackGridColor(), 1));
    painter.drawRect(QRect(0, 0, layout.labelWidth, layout.headerHeight));
    painter.setPen(QColor(0x22, 0x22, 0x22));
    painter.drawText(QRect(4, 0, layout.labelWidth - 8, layout.headerHeight), Qt::AlignVCenter | Qt::AlignLeft, tr("Room"));

    if (rows == 0) {
        painter.setPen(QColor(0x55, 0x55, 0x55));
        const QString message = m_rooms.isEmpty() ? tr("No rooms.") : tr("No rooms match the filter.");
        painter.drawText(QRect(layout.labelWidth, layout.headerHeight, layout.gridWidth, layout.gridHeight),
                         Qt::AlignCenter,
                         message);
    }
}

void RackChart::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const int days = currentFit().visibleDays;
    clampOffset();
    update();
    if (days != m_lastVisibleDays) {
        m_lastVisibleDays = days;
        emit viewportChanged();
    }
    emit navigationChanged();
}

void RackChart::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        showContextMenu(event->pos());
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    m_pressPos = event->pos();
    m_dragging = false;
    m_pressOnBlock = false;
    m_pressReservationId = 0;
    m_selRoom = -1;
    const int visual = visualRowAt(event->pos().y());
    const int day = dayIndexAt(event->pos().x());
    if (visual < 0 || event->pos().x() < kRackLabelWidth)
        return;
    const int index = m_roomOffset + visual;
    if (index < 0 || index >= m_filtered.size())
        return;
    const QDate date = dateAtDay(day);
    if (const RackBlock *block = blockAt(index, date)) {
        m_pressOnBlock = true;
        m_pressReservationId = block->reservationId;
        return;
    }
    m_dragging = true;
    m_selRoom = index;
    m_selA = date;
    m_selB = date;
    update();
}

void RackChart::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        m_selB = dateAtDay(dayIndexAt(event->pos().x()));
        update();
        return;
    }
    showTip(event->pos(), event->globalPosition().toPoint());
}

void RackChart::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const bool click = (event->pos() - m_pressPos).manhattanLength() < 6;
    const bool openBlock = m_pressOnBlock && click && m_pressReservationId > 0;
    const bool create = m_dragging && m_selRoom >= 0 && m_selRoom < m_filtered.size() && m_selA.isValid() && m_selB.isValid();
    qint64 roomId = 0;
    QDate arrival;
    QDate departure;
    qint64 reservationId = 0;
    if (openBlock)
        reservationId = m_pressReservationId;
    else if (create) {
        roomId = m_filtered.at(m_selRoom).id;
        arrival = qMin(m_selA, m_selB);
        departure = qMax(m_selA, m_selB).addDays(1);
    }
    m_dragging = false;
    m_pressOnBlock = false;
    m_pressReservationId = 0;
    m_selRoom = -1;
    update();
    if (openBlock)
        emit reservationActivated(reservationId);
    else if (create)
        emit createReservationRequested(roomId, arrival, departure);
}

void RackChart::leaveEvent(QEvent *event)
{
    QToolTip::hideText();
    QWidget::leaveEvent(event);
}

void RackChart::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Left:
        scrollDays(-1);
        break;
    case Qt::Key_Right:
        scrollDays(1);
        break;
    case Qt::Key_Up:
        scrollRooms(-1);
        break;
    case Qt::Key_Down:
        scrollRooms(1);
        break;
    default:
        QWidget::keyPressEvent(event);
        break;
    }
}

void RackChart::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        update();
    QWidget::changeEvent(event);
}

RackFit RackChart::currentFit() const
{
    return rackFit(size(), m_filtered.size());
}

void RackChart::rebuildFiltered()
{
    m_filtered.clear();
    m_filtered.reserve(m_rooms.size());
    for (const RackRoom &room : m_rooms) {
        if (matches(room))
            m_filtered.append(room);
    }
    clampOffset();
    update();
    emit navigationChanged();
}

void RackChart::clampOffset()
{
    const int maxOffset = qMax(0, m_filtered.size() - currentFit().visibleRows);
    m_roomOffset = qBound(0, m_roomOffset, maxOffset);
}

bool RackChart::matches(const RackRoom &room) const
{
    if (!m_filter.typeCodes.isEmpty() && !m_filter.typeCodes.contains(room.typeCode))
        return false;
    if (!m_filter.buildingCodes.isEmpty() && !m_filter.buildingCodes.contains(room.buildingCode))
        return false;
    if (!m_filter.floors.isEmpty() && !m_filter.floors.contains(floorKey(room)))
        return false;
    if (!m_filter.statusCodes.isEmpty() && !m_filter.statusCodes.contains(room.statusCode))
        return false;
    const QString query = m_filter.roomQuery.trimmed();
    if (!query.isEmpty() && !room.code.contains(query, Qt::CaseInsensitive))
        return false;
    return true;
}

int RackChart::dayIndexAt(int x) const
{
    const RackFit layout = currentFit();
    if (layout.visibleDays <= 1 || layout.gridWidth <= 0)
        return 0;
    const int local = qBound(0, x - layout.labelWidth, layout.gridWidth - 1);
    for (int day = 0; day < layout.visibleDays; ++day) {
        if (local < rackSpanStart(day + 1, layout.visibleDays, layout.gridWidth))
            return day;
    }
    return layout.visibleDays - 1;
}

int RackChart::visualRowAt(int y) const
{
    const RackFit layout = currentFit();
    if (y < layout.headerHeight || layout.visibleRows <= 0 || layout.gridHeight <= 0)
        return -1;
    const int local = qBound(0, y - layout.headerHeight, layout.gridHeight - 1);
    for (int row = 0; row < layout.visibleRows; ++row) {
        if (local < rackSpanStart(row + 1, layout.visibleRows, layout.gridHeight))
            return row;
    }
    return layout.visibleRows - 1;
}

QDate RackChart::dateAtDay(int dayIndex) const
{
    return m_origin.addDays(qMax(0, dayIndex));
}

QRect RackChart::dayRect(int dayIndex) const
{
    const RackFit layout = currentFit();
    const int start = rackSpanStart(dayIndex, layout.visibleDays, layout.gridWidth);
    const int end = rackSpanStart(dayIndex + 1, layout.visibleDays, layout.gridWidth);
    return QRect(layout.labelWidth + start, 0, qMax(1, end - start), height());
}

QRect RackChart::rowRect(int visualRow) const
{
    const RackFit layout = currentFit();
    const int start = rackSpanStart(visualRow, layout.visibleRows, layout.gridHeight);
    const int end = rackSpanStart(visualRow + 1, layout.visibleRows, layout.gridHeight);
    return QRect(0, layout.headerHeight + start, width(), qMax(1, end - start));
}

const RackBlock *RackChart::blockAt(int filteredIndex, const QDate &date) const
{
    if (filteredIndex < 0 || filteredIndex >= m_filtered.size() || !date.isValid())
        return nullptr;
    const RackBlock *found = nullptr;
    int rank = -1;
    for (const RackBlock &block : m_filtered.at(filteredIndex).blocks) {
        if (date < block.arrival || date >= block.departure)
            continue;
        const int next = stateRank(block.stateCode);
        if (next >= rank) {
            rank = next;
            found = &block;
        }
    }
    return found;
}

void RackChart::showContextMenu(const QPoint &pos)
{
    const int visual = visualRowAt(pos.y());
    if (visual < 0 || pos.x() < kRackLabelWidth)
        return;
    const int index = m_roomOffset + visual;
    if (index < 0 || index >= m_filtered.size())
        return;
    const QDate date = dateAtDay(dayIndexAt(pos.x()));
    const RackRoom &room = m_filtered.at(index);
    const RackBlock *block = blockAt(index, date);
    QMenu menu(this);
    if (block) {
        const qint64 reservationId = block->reservationId;
        QAction *open = menu.addAction(tr("Open reservation"));
        connect(open, &QAction::triggered, this, [this, reservationId]() { emit reservationActivated(reservationId); });
    }
    const qint64 roomId = room.id;
    QAction *create = menu.addAction(tr("New reservation"));
    connect(create, &QAction::triggered, this, [this, roomId, date]() {
        emit createReservationRequested(roomId, date, date.addDays(1));
    });
    menu.exec(mapToGlobal(pos));
}

void RackChart::showTip(const QPoint &pos, const QPoint &globalPos)
{
    const int visual = visualRowAt(pos.y());
    if (visual < 0) {
        QToolTip::hideText();
        return;
    }
    const int index = m_roomOffset + visual;
    if (index < 0 || index >= m_filtered.size()) {
        QToolTip::hideText();
        return;
    }
    const RackRoom &room = m_filtered.at(index);
    if (pos.x() < kRackLabelWidth) {
        const QString building = room.buildingName.isEmpty() ? tr("No building") : room.buildingName;
        const QString floor = room.hasFloor ? QString::number(room.floor) : tr("No floor");
        const QString tip = tr("%1\n%2\n%3\n%4\n%5")
                                .arg(room.code,
                                     room.typeName,
                                     building,
                                     floor,
                                     roomStatusText(room.statusCode));
        QToolTip::showText(globalPos, tip, this);
        return;
    }
    const QDate date = dateAtDay(dayIndexAt(pos.x()));
    if (const RackBlock *block = blockAt(index, date)) {
        const QString guest = block->guestName.isEmpty() ? tr("No guest") : block->guestName;
        const QString tip = tr("%1\n%2 – %3\n%4\n%5")
                                .arg(guest,
                                     block->arrival.toString(Qt::ISODate),
                                     block->departure.toString(Qt::ISODate),
                                     stayText(block->stateCode),
                                     reservationStatusText(block->reservationStatus));
        QToolTip::showText(globalPos, tip, this);
        return;
    }
    QToolTip::showText(globalPos, tr("%1\n%2").arg(room.code, date.toString(Qt::ISODate)), this);
}

QString RackChart::stayText(const QString &state) const
{
    if (state == QLatin1String("reserved"))
        return tr("Reserved");
    if (state == QLatin1String("in_house"))
        return tr("In house");
    if (state == QLatin1String("checked_out"))
        return tr("Checked out");
    if (state == QLatin1String("out_of_order"))
        return tr("Out of order");
    if (state == QLatin1String("out_of_inventory"))
        return tr("Out of inventory");
    return state;
}

QString RackChart::reservationStatusText(const QString &status) const
{
    if (status == QLatin1String("tentative"))
        return tr("Tentative");
    if (status == QLatin1String("confirmed"))
        return tr("Confirmed");
    if (status == QLatin1String("guaranteed"))
        return tr("Guaranteed");
    if (status == QLatin1String("blocked"))
        return tr("Blocked");
    if (status == QLatin1String("canceled"))
        return tr("Canceled");
    return status;
}

QString RackChart::roomStatusText(const QString &status) const
{
    if (status == QLatin1String("vacant_ready"))
        return tr("Ready");
    if (status == QLatin1String("occupied"))
        return tr("Occupied");
    if (status == QLatin1String("vacant_dirty"))
        return tr("Dirty");
    if (status == QLatin1String("out_of_order"))
        return tr("Out of order");
    if (status == QLatin1String("house_use"))
        return tr("House use");
    if (status == QLatin1String("complimentary"))
        return tr("Complimentary");
    if (status == QLatin1String("out_of_inventory"))
        return tr("Out of inventory");
    return status;
}
