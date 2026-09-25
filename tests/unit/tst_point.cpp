#include "domain/point.h"

#include <QTest>

using misign::domain::Point;
using namespace std::chrono_literals;

class TestPoint : public QObject {
    Q_OBJECT

private slots:
    void keepsCoordinatesAndTimestamp();
    void keepsPressureInRange_data();
    void keepsPressureInRange();
};

void TestPoint::keepsCoordinatesAndTimestamp()
{
    const Point point(12.5, -3.0, 0.5, 42ms);

    QCOMPARE(point.x(), 12.5);
    QCOMPARE(point.y(), -3.0);
    QCOMPARE(point.timestamp(), 42ms);
}

void TestPoint::keepsPressureInRange_data()
{
    QTest::addColumn<double>("pressure");
    QTest::addColumn<double>("expected");

    QTest::newRow("no pressure") << 0.0 << 0.0;
    QTest::newRow("half pressure") << 0.5 << 0.5;
    QTest::newRow("full pressure") << 1.0 << 1.0;
    QTest::newRow("below range") << -0.2 << 0.0;
    QTest::newRow("above range") << 1.7 << 1.0;
}

void TestPoint::keepsPressureInRange()
{
    QFETCH(double, pressure);
    QFETCH(double, expected);

    QCOMPARE(Point(0.0, 0.0, pressure, 0ms).pressure(), expected);
}

QTEST_APPLESS_MAIN(TestPoint)
#include "tst_point.moc"
