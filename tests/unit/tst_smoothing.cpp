#include "domain/smoothing.h"

#include <QTest>

#include <cmath>
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
// as Windows Ink delivers window positions (PHY-97), plus a deterministic
// jitter of up to half a pixel.
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
    double best = INFINITY;
    for (const CubicBezier &segment : chain) {
        constexpr int kSteps = 400;
        for (int i = 0; i <= kSteps; ++i) {
            best = std::min(best, distance(p, segment.at(static_cast<double>(i) / kSteps)));
        }
    }
    return best;
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

QTEST_APPLESS_MAIN(TestSmoothing)
#include "tst_smoothing.moc"
