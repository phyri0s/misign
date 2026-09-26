#include "domain/smoothing.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

using misign::domain::CubicBezier;
using misign::domain::InputMode;
using misign::domain::Point;
using misign::domain::ScreenPoint;
using misign::domain::smooth;
using misign::domain::Stroke;
using namespace std::chrono_literals;

namespace {

constexpr double kTolerance = 1.0;

Stroke strokeThrough(const std::vector<ScreenPoint> &positions)
{
    Stroke stroke(InputMode::Stylus);
    std::chrono::microseconds time = 0us;
    for (const ScreenPoint &p : positions) {
        stroke.addPoint(Point(p.x, p.y, 0.5, time));
        time += 5ms;
    }
    return stroke;
}

// A signature-like loop sampled every few pixels and rounded to whole pixels,
// as Windows Ink delivers window positions (the adapter should get sub-pixel
// ones, PHY-97), plus a deterministic jitter of up to half a pixel.
std::vector<ScreenPoint> jitteryLoop()
{
    std::vector<ScreenPoint> positions;
    constexpr int kSamples = 240;
    for (int i = 0; i < kSamples; ++i) {
        const double t = 2.0 * std::numbers::pi * i / kSamples;
        const double jitter = 0.5 * std::sin(i * 12.9898);
        positions.push_back({std::round(200.0 + (150.0 * std::sin(t)) + jitter),
                             std::round(100.0 + (60.0 * std::sin(2.0 * t)) - jitter)});
    }
    return positions;
}

double distance(ScreenPoint a, ScreenPoint b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

// Distance from a point to the chain, sampling each segment finely.
double distanceToChain(ScreenPoint p, const std::vector<CubicBezier> &chain)
{
    double best = std::numeric_limits<double>::infinity();
    for (const CubicBezier &segment : chain) {
        constexpr int kSteps = 400;
        for (int i = 0; i <= kSteps; ++i) {
            best = std::min(best, distance(p, segment.at(static_cast<double>(i) / kSteps)));
        }
    }
    return best;
}

// How far the chain goes outside the bounding box of the input positions.
double overshoot(const std::vector<CubicBezier> &chain, const std::vector<ScreenPoint> &positions)
{
    constexpr double kInfinity = std::numeric_limits<double>::infinity();
    double left = kInfinity;
    double top = kInfinity;
    double right = -kInfinity;
    double bottom = -kInfinity;
    for (const ScreenPoint &p : positions) {
        left = std::min(left, p.x);
        top = std::min(top, p.y);
        right = std::max(right, p.x);
        bottom = std::max(bottom, p.y);
    }
    double worst = 0.0;
    for (const CubicBezier &segment : chain) {
        constexpr int kSteps = 100;
        for (int i = 0; i <= kSteps; ++i) {
            const ScreenPoint q = segment.at(static_cast<double>(i) / kSteps);
            worst = std::max({worst, left - q.x, q.x - right, top - q.y, q.y - bottom});
        }
    }
    return worst;
}

bool allFinite(const std::vector<CubicBezier> &chain)
{
    for (const CubicBezier &s : chain) {
        for (const ScreenPoint &p : {s.start, s.control1, s.control2, s.end}) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

class TestSmoothing : public QObject {
    Q_OBJECT

private slots:
    void emptyStrokeGivesNothing();
    void singlePointGivesADot();
    void pointsWithinSpacingGiveADot();
    void twoPointsGiveAStraightSegment();
    void staysWithinToleranceOfEveryPoint();
    void isContinuousInPositionAndTangent();
    void jitteryStrokeGivesFewerSegmentsThanPoints();
    void survivesDuplicatesAndSharpCorners();
    void segmentsPointBackToTheirInputPoints();
    void segmentsCoverTheStrokeInOrder();
    void doesNotBulgeAtATightCorner();
    void staysCloseOnRandomZigzags();
    void startsAlongTheStrokeNotItsFirstPixelStep();
};

void TestSmoothing::emptyStrokeGivesNothing()
{
    QVERIFY(smooth(Stroke(InputMode::Mouse)).empty());
}

void TestSmoothing::singlePointGivesADot()
{
    const auto chain = smooth(strokeThrough({{3.0, 4.0}}));

    QCOMPARE(chain.size(), 1U);
    QCOMPARE(chain.front(), (CubicBezier{{3.0, 4.0}, {3.0, 4.0}, {3.0, 4.0}, {3.0, 4.0}, 0, 0}));
}

void TestSmoothing::pointsWithinSpacingGiveADot()
{
    const auto chain = smooth(strokeThrough({{3.0, 4.0}, {3.2, 4.1}, {3.0, 4.0}}));

    QCOMPARE(chain.size(), 1U);
    QCOMPARE(chain.front().start, (ScreenPoint{3.0, 4.0}));
    QCOMPARE(chain.front().end, chain.front().start);
    QCOMPARE(chain.front().lastPoint, 2U);
}

void TestSmoothing::twoPointsGiveAStraightSegment()
{
    const auto chain = smooth(strokeThrough({{0.0, 0.0}, {30.0, 0.0}}));

    QCOMPARE(chain.size(), 1U);
    const CubicBezier &s = chain.front();
    QCOMPARE(s.start, (ScreenPoint{0.0, 0.0}));
    QCOMPARE(s.end, (ScreenPoint{30.0, 0.0}));
    QCOMPARE(s.control1, (ScreenPoint{10.0, 0.0}));
    QCOMPARE(s.control2, (ScreenPoint{20.0, 0.0}));
}

void TestSmoothing::staysWithinToleranceOfEveryPoint()
{
    const auto positions = jitteryLoop();
    const auto chain =
        smooth(strokeThrough(positions), {.minSpacing = 0.5, .tolerance = kTolerance});

    QCOMPARE(chain.front().start, positions.front());
    QCOMPARE(chain.back().end, positions.back());
    for (const ScreenPoint &p : positions) {
        // Dropped points (within minSpacing of a kept one) may add that much.
        const double d = distanceToChain(p, chain);
        QVERIFY2(d <= kTolerance + 0.5,
                 qPrintable(QStringLiteral("(%1, %2) is %3 away").arg(p.x).arg(p.y).arg(d)));
    }
}

void TestSmoothing::isContinuousInPositionAndTangent()
{
    const auto chain = smooth(strokeThrough(jitteryLoop()));

    QVERIFY(chain.size() > 1);
    for (std::size_t i = 1; i < chain.size(); ++i) {
        const CubicBezier &before = chain[i - 1];
        const CubicBezier &after = chain[i];
        QCOMPARE(after.start, before.end);
        // Incoming and outgoing tangents point the same way.
        const ScreenPoint in{before.end.x - before.control2.x, before.end.y - before.control2.y};
        const ScreenPoint out{after.control1.x - after.start.x, after.control1.y - after.start.y};
        const double cross = (in.x * out.y) - (in.y * out.x);
        const double lengths = std::hypot(in.x, in.y) * std::hypot(out.x, out.y);
        QVERIFY(lengths > 0.0);
        QVERIFY(std::abs(cross) <= 1e-9 * lengths);
        QVERIFY((in.x * out.x) + (in.y * out.y) > 0.0);
    }
}

void TestSmoothing::jitteryStrokeGivesFewerSegmentsThanPoints()
{
    const auto positions = jitteryLoop();
    const auto chain = smooth(strokeThrough(positions));

    QVERIFY2(
        chain.size() * 4 < positions.size(),
        qPrintable(
            QStringLiteral("%1 segments for %2 points").arg(chain.size()).arg(positions.size())));
}

void TestSmoothing::survivesDuplicatesAndSharpCorners()
{
    // A V with repeated points at the tip, as a pen pausing on a turn gives.
    const auto chain = smooth(strokeThrough({{0.0, 0.0},
                                             {10.0, 20.0},
                                             {20.0, 40.0},
                                             {20.0, 40.0},
                                             {20.0, 40.0},
                                             {30.0, 20.0},
                                             {40.0, 0.0}}));

    QVERIFY(allFinite(chain));
    QCOMPARE(chain.front().start, (ScreenPoint{0.0, 0.0}));
    QCOMPARE(chain.back().end, (ScreenPoint{40.0, 0.0}));
    QVERIFY(distanceToChain({20.0, 40.0}, chain) <= kTolerance);
}

void TestSmoothing::segmentsPointBackToTheirInputPoints()
{
    const auto positions = jitteryLoop();
    const Stroke stroke = strokeThrough(positions);
    const auto chain = smooth(stroke);

    for (const CubicBezier &s : chain) {
        const Point &first = stroke.points()[s.firstPoint];
        const Point &last = stroke.points()[s.lastPoint];
        QCOMPARE(s.start, (ScreenPoint{first.x(), first.y()}));
        QCOMPARE(s.end, (ScreenPoint{last.x(), last.y()}));
    }
}

void TestSmoothing::segmentsCoverTheStrokeInOrder()
{
    const auto positions = jitteryLoop();
    const auto chain = smooth(strokeThrough(positions));

    QCOMPARE(chain.front().firstPoint, 0U);
    QCOMPARE(chain.back().lastPoint, positions.size() - 1);
    for (std::size_t i = 1; i < chain.size(); ++i) {
        QVERIFY(chain[i].firstPoint < chain[i].lastPoint);
        QCOMPARE(chain[i].firstPoint, chain[i - 1].lastPoint);
    }
}

// Three points turning a corner: the tangent shared at the split points away
// from the stroke, and least squares answered with a 162 px handle on a 7 px
// chord, sending the curve 72 px outside the input. Handles longer than the
// chord now fall back to a third of it.
void TestSmoothing::doesNotBulgeAtATightCorner()
{
    const std::vector<ScreenPoint> positions{{0.0, 0.0}, {8.0, 6.0}, {8.0, -1.0}, {7.0, -1.0}};

    QVERIFY(overshoot(smooth(strokeThrough(positions)), positions) <= kTolerance);
}

// Short strokes with random steps of up to 8 px each way, the worst case for
// a smooth fit: the curve may round the turns off, but never goes more than
// one step outside the input.
void TestSmoothing::staysCloseOnRandomZigzags()
{
    constexpr int kStep = 8;
    std::uint32_t state = 12345; // Deterministic: a fixed linear congruential sequence.
    const auto next = [&state] {
        state = (state * 1664525U) + 1013904223U;
        return static_cast<int>((state >> 16U) % (2 * kStep + 1)) - kStep;
    };
    double worst = 0.0;
    for (int trial = 0; trial < 2000; ++trial) {
        std::vector<ScreenPoint> positions{{0.0, 0.0}};
        for (int i = 0; i < 3 + (trial % 8); ++i) {
            positions.push_back({positions.back().x + next(), positions.back().y + next()});
        }
        const auto chain = smooth(strokeThrough(positions));
        QVERIFY(allFinite(chain));
        worst = std::max(worst, overshoot(chain, positions));
    }
    QVERIFY2(worst <= kStep, qPrintable(QStringLiteral("worst overshoot %1 px").arg(worst)));
}

// A slow start at about 27 degrees, in whole-pixel steps: the first step is
// horizontal. The start tangent follows the stroke, not that first step.
void TestSmoothing::startsAlongTheStrokeNotItsFirstPixelStep()
{
    std::vector<ScreenPoint> positions;
    positions.reserve(20);
    for (int i = 0; i < 20; ++i) {
        positions.push_back({static_cast<double>(i), std::floor(i / 2.0)});
    }

    const CubicBezier first = smooth(strokeThrough(positions)).front();

    const double angle =
        std::atan2(first.control1.y - first.start.y, first.control1.x - first.start.x) * 180.0 /
        std::numbers::pi;
    QVERIFY2(std::abs(angle - 26.57) < 10.0,
             qPrintable(QStringLiteral("starts at %1 degrees").arg(angle)));
}

QTEST_APPLESS_MAIN(TestSmoothing)
#include "tst_smoothing.moc"
