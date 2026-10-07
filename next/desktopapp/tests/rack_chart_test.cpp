#include "rackchart.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>
#include <QTranslator>

namespace {

RackRoom makeRoom(qint64 id, const QString &code, const QString &typeCode, const QString &building, int floor, bool hasFloor)
{
    RackRoom room;
    room.id = id;
    room.code = code;
    room.typeCode = typeCode;
    room.typeName = typeCode;
    room.buildingCode = building;
    room.buildingName = building;
    room.hasFloor = hasFloor;
    room.floor = floor;
    room.statusCode = QStringLiteral("vacant_ready");
    return room;
}

QImage render(QWidget &widget)
{
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    widget.render(&painter);
    return image;
}

void sendMouse(QWidget *widget, QEvent::Type type, const QPoint &pos, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(pos), widget->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
}

} // namespace

class RackChartTest : public QObject {
    Q_OBJECT

private slots:
    void columnsAndRowsFillTheWidget();
    void roomsStayVisibleWithoutReservations();
    void sortMatchesLegacyBuildingThenCode();
    void filterHidesRoomsImmediately();
    void shortWindowScrollsRooms();
};

void RackChartTest::columnsAndRowsFillTheWidget()
{
    const QSize size(960, 520);
    const RackFit fit = rackFit(size, 6);
    QCOMPARE(fit.visibleRows, 6);
    QVERIFY(fit.visibleDays > 14);
    int widthSum = 0;
    for (int day = 0; day < fit.visibleDays; ++day)
        widthSum += rackSpanStart(day + 1, fit.visibleDays, fit.gridWidth) - rackSpanStart(day, fit.visibleDays, fit.gridWidth);
    QCOMPARE(widthSum, fit.gridWidth);
    int heightSum = 0;
    for (int row = 0; row < fit.visibleRows; ++row)
        heightSum += rackSpanStart(row + 1, fit.visibleRows, fit.gridHeight) - rackSpanStart(row, fit.visibleRows, fit.gridHeight);
    QCOMPARE(heightSum, fit.gridHeight);

    const RackFit narrow = rackFit(QSize(kRackLabelWidth + 100, 400), 6);
    QVERIFY(narrow.visibleDays < fit.visibleDays);
    QVERIFY(narrow.visibleDays >= 1);
}

void RackChartTest::roomsStayVisibleWithoutReservations()
{
    QLocale::setDefault(QLocale(QLocale::Russian, QLocale::Russia));
    QTranslator translator;
    const QString catalog = QStringLiteral(HOTEL_DESKTOP_RU_QM);
    QVERIFY2(translator.load(catalog), qPrintable(catalog));
    QCoreApplication::installTranslator(&translator);

    RackChart chart;
    QVector<RackRoom> rooms;
    for (int i = 0; i < 6; ++i)
        rooms.append(makeRoom(i + 1, QString::number(101 + i), QStringLiteral("STD"), QStringLiteral("MAIN"), 1, true));
    RackBlock stay;
    stay.reservationId = 42;
    stay.stayId = 7;
    stay.guestName = QStringLiteral("A");
    stay.stateCode = QStringLiteral("reserved");
    stay.reservationStatus = QStringLiteral("confirmed");
    stay.arrival = QDate::currentDate().addDays(1);
    stay.departure = QDate::currentDate().addDays(4);
    rooms[2].blocks.append(stay);
    chart.setRooms(rooms);
    chart.resize(1000, 560);
    chart.show();
    QCOMPARE(chart.filteredRoomCount(), 6);
    QVERIFY(chart.visibleDayCount() > 14);
    QCOMPARE(chart.visibleRowCount(), 6);

    const RackFit fit = rackFit(chart.size(), 6);
    const auto center = [&](int row, int day) {
        const int x0 = kRackLabelWidth + rackSpanStart(day, fit.visibleDays, fit.gridWidth);
        const int x1 = kRackLabelWidth + rackSpanStart(day + 1, fit.visibleDays, fit.gridWidth);
        const int y0 = kRackHeaderHeight + rackSpanStart(row, fit.visibleRows, fit.gridHeight);
        const int y1 = kRackHeaderHeight + rackSpanStart(row + 1, fit.visibleRows, fit.gridHeight);
        return QPoint((x0 + x1) / 2, (y0 + y1) / 2);
    };

    sendMouse(&chart, QEvent::MouseButtonPress, center(0, 1), Qt::LeftButton, Qt::LeftButton);
    sendMouse(&chart, QEvent::MouseMove, center(0, 4), Qt::NoButton, Qt::LeftButton);

    const QImage image = render(chart);
    const QString dir = QStringLiteral("/opt/cursor/artifacts/screenshots");
    QDir().mkpath(dir);
    QVERIFY(image.save(dir + QStringLiteral("/rack-chart.png")));

    const int gridX = kRackLabelWidth;
    const int gridY = kRackHeaderHeight + 8;
    QCOMPARE(image.pixelColor(gridX, gridY), rackGridColor());
    const int rowLineY = kRackHeaderHeight + rackSpanStart(4, fit.visibleRows, fit.gridHeight);
    QCOMPARE(image.pixelColor(kRackLabelWidth + 12, rowLineY), rackGridColor());
    QCOMPARE(image.pixelColor(center(0, 2)), rackSelectionColor());
    const int barX = kRackLabelWidth + rackSpanStart(4, fit.visibleDays, fit.gridWidth) - 6;
    QCOMPARE(image.pixelColor(barX, center(2, 2).y()), rackReservedColor());

    QSignalSpy created(&chart, &RackChart::createReservationRequested);
    sendMouse(&chart, QEvent::MouseButtonRelease, center(0, 4), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(created.count(), 1);
    QCOMPARE(created.at(0).at(0).toLongLong(), 1);
    QCOMPARE(created.at(0).at(1).toDate(), QDate::currentDate().addDays(1));
    QCOMPARE(created.at(0).at(2).toDate(), QDate::currentDate().addDays(5));

    QSignalSpy opened(&chart, &RackChart::reservationActivated);
    sendMouse(&chart, QEvent::MouseButtonPress, center(2, 2), Qt::LeftButton, Qt::LeftButton);
    sendMouse(&chart, QEvent::MouseButtonRelease, center(2, 2), Qt::LeftButton, Qt::NoButton);
    QCOMPARE(opened.count(), 1);
    QCOMPARE(opened.at(0).at(0).toLongLong(), 42);
}

void RackChartTest::sortMatchesLegacyBuildingThenCode()
{
    RackChart chart;
    QVector<RackRoom> rooms;
    rooms.append(makeRoom(1, QStringLiteral("10"), QStringLiteral("STD"), QStringLiteral("B"), 2, true));
    rooms.append(makeRoom(2, QStringLiteral("2"), QStringLiteral("STD"), QStringLiteral("A"), 1, true));
    rooms.append(makeRoom(3, QStringLiteral("5"), QStringLiteral("STD"), QString(), 0, false));
    rooms.append(makeRoom(4, QStringLiteral("3"), QStringLiteral("DLX"), QStringLiteral("A"), 1, true));
    chart.setRooms(rooms);
    const QStringList codes = chart.roomCodes();
    QCOMPARE(codes, QStringList({QStringLiteral("5"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("10")}));
}

void RackChartTest::filterHidesRoomsImmediately()
{
    RackChart chart;
    QVector<RackRoom> rooms;
    rooms.append(makeRoom(1, QStringLiteral("101"), QStringLiteral("STD"), QStringLiteral("MAIN"), 1, true));
    rooms.append(makeRoom(2, QStringLiteral("201"), QStringLiteral("DLX"), QStringLiteral("MAIN"), 2, true));
    rooms.append(makeRoom(3, QStringLiteral("102"), QStringLiteral("STD"), QStringLiteral("ANNEX"), 1, true));
    chart.setRooms(rooms);
    chart.resize(900, 400);

    RackFilter type;
    type.typeCodes.append(QStringLiteral("DLX"));
    chart.setFilter(type);
    QCOMPARE(chart.roomCodes(), QStringList({QStringLiteral("201")}));
    QCOMPARE(chart.visibleRowCount(), 1);

    RackFilter floor;
    floor.floors.append(QStringLiteral("1"));
    chart.setFilter(floor);
    QCOMPARE(chart.roomCodes(), QStringList({QStringLiteral("102"), QStringLiteral("101")}));

    RackFilter query;
    query.roomQuery = QStringLiteral("02");
    chart.setFilter(query);
    QCOMPARE(chart.roomCodes(), QStringList({QStringLiteral("102")}));

    chart.setFilter(RackFilter());
    QCOMPARE(chart.filteredRoomCount(), 3);
}

void RackChartTest::shortWindowScrollsRooms()
{
    RackChart chart;
    QVector<RackRoom> rooms;
    for (int i = 0; i < 6; ++i)
        rooms.append(makeRoom(i + 1, QString::number(i + 1), QStringLiteral("STD"), QStringLiteral("MAIN"), 1, true));
    chart.setRooms(rooms);
    chart.resize(800, kRackHeaderHeight + kRackMinRowHeight * 2);
    QCOMPARE(chart.visibleRowCount(), 2);
    QVERIFY(chart.canScrollDown());
    QVERIFY(!chart.canScrollUp());
    chart.scrollRooms(1);
    QVERIFY(chart.canScrollUp());
    chart.scrollRooms(100);
    QVERIFY(!chart.canScrollDown());
    QVERIFY(chart.canScrollUp());

    const QDate start = chart.origin();
    chart.scrollDays(3);
    QCOMPARE(chart.origin(), start.addDays(3));
    chart.scrollDays(-3);
    QCOMPARE(chart.origin(), start);
}

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);
    RackChartTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "rack_chart_test.moc"
