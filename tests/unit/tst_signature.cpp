#include "domain/signature.h"

#include <QTest>

#include <cmath>
#include <initializer_list>
#include <optional>
#include <utility>

using misign::domain::AffineMatrix;
using misign::domain::InkOptions;
using misign::domain::InputMode;
using misign::domain::PageGeometry;
using misign::domain::PdfPoint;
using misign::domain::PdfRect;
using misign::domain::Point;
using misign::domain::Rotation;
using misign::domain::ScreenPoint;
using misign::domain::ScreenRect;
using misign::domain::Signature;
using misign::domain::Stroke;
using namespace std::chrono_literals;

namespace {

constexpr double kTolerance = 1e-9;

bool near(PdfPoint actual, PdfPoint expected)
{
    return std::abs(actual.x - expected.x) < kTolerance &&
           std::abs(actual.y - expected.y) < kTolerance;
}

// A stroke through the given drawing-surface positions, 5 ms apart.
Stroke stroke(std::initializer_list<std::pair<double, double>> positions,
              InputMode inputMode = InputMode::Stylus)
{
    Stroke result(inputMode);
    std::chrono::microseconds time = 0us;
    for (const auto &[x, y] : positions) {
        result.addPoint(Point(x, y, 0.5, time));
        time += 5ms;
    }
    return result;
}

// The fitting matrix, or an all-zero matrix when there is none, which fails
// every position check. By default it fits the points alone, without room for
// the ink.
AffineMatrix fitted(const Signature &sig, const PdfRect &box, double inkMargin = 0.0)
{
    const std::optional<AffineMatrix> matrix = sig.fitInto(box, inkMargin);
    return matrix ? *matrix : AffineMatrix{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
}

Signature signature(std::initializer_list<Stroke> strokes)
{
    Signature result;
    for (const Stroke &s : strokes) {
        result.addStroke(s);
    }
    return result;
}

} // namespace

class TestSignature : public QObject {
    Q_OBJECT

private slots:
    void strokeKeepsItsPointsInOrder();
    void ignoresEmptyStrokes();
    void boundingBoxCoversEveryStroke();
    void emptySignatureHasNoBoundingBoxAndNoFit();
    void emptyBoxHasNoFit();
    void fitsAWideSignature();
    void fitsATallSignature();
    void keepsTheAspectRatioAndTurnsYUp();
    void fitsAStraightLineAlongItsLength();
    void movesASingleDotToTheCentre();
    void staysUprightOnARotatedPage();
    void leavesRoomForTheInk();
    void fitsAStraightLineAndItsInkThickness();
};

void TestSignature::strokeKeepsItsPointsInOrder()
{
    const Stroke s = stroke({{1.0, 2.0}, {3.0, 4.0}}, InputMode::Mouse);

    QCOMPARE(s.inputMode(), InputMode::Mouse);
    QCOMPARE(s.points().size(), 2U);
    QCOMPARE(s.points().at(0), Point(1.0, 2.0, 0.5, 0us));
    QCOMPARE(s.points().at(1), Point(3.0, 4.0, 0.5, 5ms));
}

void TestSignature::ignoresEmptyStrokes()
{
    Signature sig;
    sig.addStroke(Stroke(InputMode::Touchpad));

    QVERIFY(sig.isEmpty());
    sig.addStroke(stroke({{0.0, 0.0}}));
    QCOMPARE(sig.strokes().size(), 1U);
}

void TestSignature::boundingBoxCoversEveryStroke()
{
    const Signature sig = signature({stroke({{10.0, 20.0}, {30.0, 5.0}}), stroke({{-4.0, 12.0}})});

    QCOMPARE(sig.boundingBox(), (ScreenRect{-4.0, 5.0, 30.0, 20.0}));
}

void TestSignature::emptySignatureHasNoBoundingBoxAndNoFit()
{
    const Signature sig;

    QVERIFY(!sig.boundingBox().has_value());
    QVERIFY(!sig.fitInto(PdfRect{0.0, 0.0, 100.0, 50.0}).has_value());
}

// A box with no area would squash the signature to a point: no fit.
void TestSignature::emptyBoxHasNoFit()
{
    const Signature sig = signature({stroke({{0.0, 0.0}, {20.0, 10.0}})});

    QVERIFY(!sig.fitInto(PdfRect{0.0, 0.0, 100.0, 0.0}).has_value());
}

// 200 × 50 drawn, into a 200 × 200 box at (100, 100): full width, centred
// vertically, the top-left of the drawing at the top-left of the fitted area.
void TestSignature::fitsAWideSignature()
{
    const Signature sig = signature({stroke({{0.0, 0.0}, {200.0, 50.0}})});

    const AffineMatrix m = fitted(sig, PdfRect{100.0, 100.0, 300.0, 300.0});

    QVERIFY(near(m.map({0.0, 0.0}), {100.0, 225.0}));
    QVERIFY(near(m.map({200.0, 50.0}), {300.0, 175.0}));
}

// 50 × 200 drawn, into a 200 × 100 box: half size, full height, centred
// horizontally.
void TestSignature::fitsATallSignature()
{
    const Signature sig = signature({stroke({{10.0, 10.0}, {60.0, 210.0}})});

    const AffineMatrix m = fitted(sig, PdfRect{0.0, 0.0, 200.0, 100.0});

    QVERIFY(near(m.map({10.0, 10.0}), {87.5, 100.0}));
    QVERIFY(near(m.map({60.0, 210.0}), {112.5, 0.0}));
}

void TestSignature::keepsTheAspectRatioAndTurnsYUp()
{
    const Signature sig = signature({stroke({{0.0, 0.0}, {40.0, 30.0}})});

    const AffineMatrix m = fitted(sig, PdfRect{0.0, 0.0, 400.0, 100.0});

    QCOMPARE(m.a, -m.d); // Same scale on both axes, y flipped.
    QVERIFY(m.a > 0.0);
    // Lower on the drawing surface is lower on the page.
    QVERIFY(m.map({0.0, 30.0}).y < m.map({0.0, 0.0}).y);
}

void TestSignature::fitsAStraightLineAlongItsLength()
{
    const Signature sig = signature({stroke({{0.0, 7.0}, {50.0, 7.0}})});

    const AffineMatrix m = fitted(sig, PdfRect{0.0, 0.0, 100.0, 40.0});

    QVERIFY(near(m.map({0.0, 7.0}), {0.0, 20.0}));
    QVERIFY(near(m.map({50.0, 7.0}), {100.0, 20.0}));
}

void TestSignature::movesASingleDotToTheCentre()
{
    const Signature sig = signature({stroke({{12.0, 34.0}})});

    const AffineMatrix m = fitted(sig, PdfRect{0.0, 0.0, 100.0, 40.0});

    QVERIFY(near(m.map({12.0, 34.0}), {50.0, 20.0}));
    QCOMPARE(m.a, 1.0); // Not scaled: nothing to scale it by.
}

// A4 with /Rotate 90, displayed as landscape. The user selects a wide 200 × 50
// box on screen and draws a wide signature: composed with displayedToUser(),
// as the content stream will be, the drawing's corners land on the user-space
// points toUser() gives for the box's corners, so it is upright as displayed.
// PoDoFo's convenience API got this case wrong (PHY-83).
void TestSignature::staysUprightOnARotatedPage()
{
    const PageGeometry page({0.0, 0.0, 595.276, 841.89}, std::nullopt, Rotation::Clockwise90);
    constexpr double kScale = 2.0; // Pixels per point.
    const ScreenRect selection{200.0, 300.0, 600.0, 400.0};
    // The same box in displayed coordinates (y up from the displayed bottom).
    const double displayedTop = page.displayedHeight() - (selection.top / kScale);
    const PdfRect box{selection.left / kScale,
                      displayedTop - ((selection.bottom - selection.top) / kScale),
                      selection.right / kScale, displayedTop};
    const Signature sig = signature({stroke({{0.0, 0.0}, {200.0, 50.0}})});

    const AffineMatrix fit = fitted(sig, box);
    const auto toUserSpace = [&](PdfPoint drawn) {
        return page.displayedToUser().map(fit.map(drawn));
    };

    QVERIFY(near(toUserSpace({0.0, 0.0}),
                 page.toUser(ScreenPoint{selection.left, selection.top}, kScale)));
    QVERIFY(near(toUserSpace({200.0, 50.0}),
                 page.toUser(ScreenPoint{selection.right, selection.bottom}, kScale)));
}

// The ink reaches half the widest stroke beyond the points: that edge, not the
// points, lands on the box's edges, so nothing is clipped.
void TestSignature::leavesRoomForTheInk()
{
    const Signature sig = signature({stroke({{0.0, 0.0}, {200.0, 50.0}})});
    const double margin = InkOptions{}.maxWidth / 2.0;

    // The default margin: no fitted() here, which passes its own.
    const AffineMatrix m = sig.fitInto(PdfRect{100.0, 100.0, 300.0, 300.0})
                               .value_or(AffineMatrix{0.0, 0.0, 0.0, 0.0, 0.0, 0.0});

    // The ink is (200 + 2 margin) wide: it spans the box's width, and its
    // height is centred on the box's centre, y = 200.
    const double scale = 200.0 / (200.0 + (2.0 * margin));
    const double halfHeight = (50.0 + (2.0 * margin)) * scale / 2.0;
    QVERIFY(near(m.map({-margin, -margin}), {100.0, 200.0 + halfHeight}));
    QVERIFY(near(m.map({200.0 + margin, 50.0 + margin}), {300.0, 200.0 - halfHeight}));
}

// A straight line has no height, but its ink has: a flat box limits the scale.
void TestSignature::fitsAStraightLineAndItsInkThickness()
{
    const Signature sig = signature({stroke({{0.0, 7.0}, {50.0, 7.0}})});

    const AffineMatrix m = fitted(sig, PdfRect{0.0, 0.0, 100.0, 2.0}, 2.0);

    QVERIFY(near(m.map({25.0, 5.0}), {50.0, 2.0}));
    QVERIFY(near(m.map({25.0, 9.0}), {50.0, 0.0}));
    QCOMPARE(m.a, 0.5);
}

QTEST_APPLESS_MAIN(TestSignature)
#include "tst_signature.moc"
