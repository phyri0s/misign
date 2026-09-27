#include "domain/ink.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <vector>

using misign::domain::CubicBezier;
using misign::domain::InkOptions;
using misign::domain::inkPieces;
using misign::domain::InputMode;
using misign::domain::Point;
using misign::domain::ScreenPoint;
using misign::domain::smooth;
using misign::domain::Stroke;
using misign::domain::strokeWidths;
using namespace std::chrono_literals;

namespace {

// A horizontal stroke of `count` points, `spacing` apart, `interval` apart in
// time, all at `pressure`.
Stroke line(InputMode mode, int count, double spacing, std::chrono::microseconds interval,
            double pressure = 0.5)
{
    Stroke stroke(mode);
    for (int i = 0; i < count; ++i) {
        stroke.addPoint(Point(i * spacing, 10.0, pressure, i * interval));
    }
    return stroke;
}

double last(const std::vector<double> &values)
{
    return values.empty() ? 0.0 : values.back();
}

bool near(ScreenPoint a, ScreenPoint b)
{
    return std::hypot(a.x - b.x, a.y - b.y) < 1e-9;
}

// The length of the curve, from a fine polyline (never longer than the curve).
double arcLength(const CubicBezier &c)
{
    constexpr int kSteps = 256;
    double length = 0.0;
    ScreenPoint previous = c.start;
    for (int i = 1; i <= kSteps; ++i) {
        const ScreenPoint p = c.at(static_cast<double>(i) / kSteps);
        length += std::hypot(p.x - previous.x, p.y - previous.y);
        previous = p;
    }
    return length;
}

// Whether every piece lies on its parent segment: the pieces of a segment
// split into k equal parts are, at their own parameter 0.5, the segment at
// (j + 0.5) / k.
bool piecesLieOnTheChain(const std::vector<misign::domain::InkPiece> &pieces,
                         const std::vector<CubicBezier> &chain)
{
    std::size_t index = 0;
    for (const CubicBezier &segment : chain) {
        std::size_t k = 0;
        while (index + k < pieces.size() &&
               pieces[index + k].curve.firstPoint == segment.firstPoint &&
               pieces[index + k].curve.lastPoint == segment.lastPoint) {
            ++k;
        }
        if (k == 0) {
            return false;
        }
        for (std::size_t j = 0; j < k; ++j) {
            const double t = (static_cast<double>(j) + 0.5) / static_cast<double>(k);
            if (!near(pieces[index + j].curve.at(0.5), segment.at(t))) {
                return false;
            }
        }
        index += k;
    }
    return index == pieces.size();
}

} // namespace

class TestInk : public QObject {
    Q_OBJECT

private slots:
    void widthsStayWithinBounds_data();
    void widthsStayWithinBounds();
    void harderPressDrawsWider();
    void slowerMouseDrawsWider();
    void touchpadFollowsSpeedLikeTheMouse();
    void burstsWithTheSameTimestampKeepTheSpeed();
    void mouseStrokeStartsAtItsSpeed();
    void smoothsASuddenPressureChange();
    void piecesFollowTheSmoothedCurveAndJoin();
    void piecesAreShortAndTakeTheLocalWidth();
    void piecesStayShortOnLongHandles();
    void dotGivesOnePiece();
};

void TestInk::widthsStayWithinBounds_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<double>("spacing");
    QTest::addColumn<double>("pressure");

    QTest::newRow("stylus, no pressure") << static_cast<int>(InputMode::Stylus) << 3.0 << 0.0;
    QTest::newRow("stylus, full pressure") << static_cast<int>(InputMode::Stylus) << 3.0 << 1.0;
    QTest::newRow("mouse, very fast") << static_cast<int>(InputMode::Mouse) << 500.0 << 1.0;
    QTest::newRow("mouse, still") << static_cast<int>(InputMode::Mouse) << 0.0 << 1.0;
}

void TestInk::widthsStayWithinBounds()
{
    QFETCH(int, mode);
    QFETCH(double, spacing);
    QFETCH(double, pressure);
    const InkOptions options;

    const auto widths =
        strokeWidths(line(static_cast<InputMode>(mode), 30, spacing, 5ms, pressure), options);

    QCOMPARE(widths.size(), 30U);
    for (const double w : widths) {
        QVERIFY(w >= options.minWidth && w <= options.maxWidth);
    }
}

void TestInk::harderPressDrawsWider()
{
    const double light = last(strokeWidths(line(InputMode::Stylus, 20, 3.0, 5ms, 0.1)));
    const double hard = last(strokeWidths(line(InputMode::Stylus, 20, 3.0, 5ms, 0.8)));

    QVERIFY(hard > light);
}

void TestInk::slowerMouseDrawsWider()
{
    const double slow = last(strokeWidths(line(InputMode::Mouse, 20, 1.0, 10ms)));
    const double fast = last(strokeWidths(line(InputMode::Mouse, 20, 20.0, 10ms)));

    QVERIFY(slow > fast);
}

// The touchpad's pressure is constant, like the mouse's: only speed counts.
void TestInk::touchpadFollowsSpeedLikeTheMouse()
{
    QCOMPARE(strokeWidths(line(InputMode::Touchpad, 20, 6.0, 8ms, 1.0)),
             strokeWidths(line(InputMode::Mouse, 20, 6.0, 8ms, 1.0)));
}

// PHY-97: samples arrive in bursts sharing a timestamp. A zero interval must
// neither divide by zero nor lose the distance the burst covered: 3 samples
// 10 units apart every 15 ms is 2 units/ms, so 4.5 / (1 + 2) = 1.5 wide.
void TestInk::burstsWithTheSameTimestampKeepTheSpeed()
{
    Stroke stroke(InputMode::Mouse);
    for (int i = 0; i < 20; ++i) {
        stroke.addPoint(Point(i * 10.0, 0.0, 1.0, (i / 3) * 15ms));
    }

    const auto widths = strokeWidths(stroke);

    for (const double w : widths) {
        QVERIFY(std::abs(w - 1.5) < 1e-9);
    }
}

// A stroke started while the pointer is already moving (toggle mode) draws at
// its speed from the first point, without a wide blob at the start.
void TestInk::mouseStrokeStartsAtItsSpeed()
{
    const auto widths = strokeWidths(line(InputMode::Mouse, 20, 6.0, 8ms));

    // 6 units every 8 ms is 0.75 units/ms: 4.5 / 1.75.
    for (const double w : widths) {
        QVERIFY(std::abs(w - (4.5 / 1.75)) < 1e-9);
    }
}

void TestInk::smoothsASuddenPressureChange()
{
    Stroke stroke(InputMode::Stylus);
    for (int i = 0; i < 10; ++i) {
        stroke.addPoint(Point(i * 3.0, 0.0, i < 5 ? 0.0 : 1.0, i * 5ms));
    }
    const InkOptions options;

    const auto widths = strokeWidths(stroke, options);

    // The raw width jumps from min to max at point 5; the smoothed one moves
    // only part of the way, then keeps rising.
    QVERIFY(widths[5] < options.maxWidth - 1.0);
    QVERIFY(widths[6] > widths[5]);
}

void TestInk::piecesFollowTheSmoothedCurveAndJoin()
{
    Stroke stroke(InputMode::Stylus);
    for (int i = 0; i < 60; ++i) {
        const double t = i / 10.0;
        stroke.addPoint(Point(20.0 * t, 30.0 * std::sin(t), 0.2 + (i / 100.0), i * 5ms));
    }
    const auto chain = smooth(stroke);
    const auto pieces = inkPieces(chain, stroke, strokeWidths(stroke));

    QVERIFY(pieces.size() > chain.size());
    QVERIFY(near(pieces.front().curve.start, chain.front().start));
    QVERIFY(near(pieces.back().curve.end, chain.back().end));
    for (std::size_t i = 1; i < pieces.size(); ++i) {
        QVERIFY(near(pieces[i].curve.start, pieces[i - 1].curve.end));
    }
    QVERIFY(piecesLieOnTheChain(pieces, chain));
}

void TestInk::piecesAreShortAndTakeTheLocalWidth()
{
    // Pressure rising along a straight line: widths along the pieces rise too.
    Stroke stroke(InputMode::Stylus);
    for (int i = 0; i < 40; ++i) {
        stroke.addPoint(Point(i * 5.0, 0.0, i / 40.0, i * 5ms));
    }
    const InkOptions options;
    const auto pieces = inkPieces(smooth(stroke), stroke, strokeWidths(stroke, options), options);

    for (std::size_t i = 0; i < pieces.size(); ++i) {
        QVERIFY(arcLength(pieces[i].curve) <= options.maxPieceLength + 1e-9);
        QVERIFY(pieces[i].width >= options.minWidth && pieces[i].width <= options.maxWidth);
        if (i > 0) {
            QVERIFY(pieces[i].width >= pieces[i - 1].width);
        }
    }
    QVERIFY(pieces.back().width > pieces.front().width + 1.0);
}

// Handles as long as the chord, which the fitter allows: the curve is much
// longer than its chord, and its speed varies a lot along t.
void TestInk::piecesStayShortOnLongHandles()
{
    Stroke stroke(InputMode::Stylus);
    stroke.addPoint(Point(0.0, 0.0, 0.5, 0ms));
    stroke.addPoint(Point(40.0, 0.0, 0.5, 5ms));
    const std::vector<CubicBezier> chain{
        {{0.0, 0.0}, {40.0, 40.0}, {0.0, 40.0}, {40.0, 0.0}, 0, 1}};
    const InkOptions options;

    const auto pieces = inkPieces(chain, stroke, strokeWidths(stroke, options), options);

    for (const auto &piece : pieces) {
        QVERIFY(arcLength(piece.curve) <= options.maxPieceLength + 1e-9);
    }
}

void TestInk::dotGivesOnePiece()
{
    Stroke stroke(InputMode::Stylus);
    stroke.addPoint(Point(5.0, 6.0, 1.0, 0us));

    const auto pieces = inkPieces(smooth(stroke), stroke, strokeWidths(stroke));

    QCOMPARE(pieces.size(), 1U);
    QVERIFY(near(pieces.front().curve.start, ScreenPoint{5.0, 6.0}));
    QCOMPARE(pieces.front().width, InkOptions{}.maxWidth);
}

QTEST_APPLESS_MAIN(TestInk)
#include "tst_ink.moc"
