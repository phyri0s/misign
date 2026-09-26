#include "domain/page_geometry.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <cmath>
#include <optional>

using misign::domain::AffineMatrix;
using misign::domain::PageGeometry;
using misign::domain::PdfPoint;
using misign::domain::PdfRect;
using misign::domain::Rotation;
using misign::domain::rotationFromDegrees;
using misign::domain::ScreenPoint;
using misign::domain::ScreenRect;

namespace {

constexpr double kA4Width = 595.276;
constexpr double kA4Height = 841.89;
constexpr double kTolerance = 1e-6;

bool near(double actual, double expected)
{
    return std::abs(actual - expected) < kTolerance;
}

PdfRect rectFromJson(const QJsonArray &values)
{
    return {values.at(0).toDouble(), values.at(1).toDouble(), values.at(2).toDouble(),
            values.at(3).toDouble()};
}

// A box of the manifest, null when absent.
std::optional<PdfRect> optionalRectFromJson(const QJsonValue &value)
{
    if (value.isNull()) {
        return std::nullopt;
    }
    return rectFromJson(value.toArray());
}

bool near(const PdfRect &actual, const PdfRect &expected)
{
    return near(actual.left, expected.left) && near(actual.bottom, expected.bottom) &&
           near(actual.right, expected.right) && near(actual.top, expected.top);
}

PageGeometry a4(Rotation rotation)
{
    return {{0.0, 0.0, kA4Width, kA4Height}, std::nullopt, rotation};
}

// Every page of tests/fixtures/pdf/manifest.json, named "<file> page <n>".
// Empty when the manifest cannot be read.
QList<std::pair<QString, QJsonObject>> corpusPages()
{
    QFile file(QStringLiteral(MISIGN_FIXTURES_DIR "/manifest.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonObject manifest = QJsonDocument::fromJson(file.readAll()).object();
    QList<std::pair<QString, QJsonObject>> result;
    for (auto it = manifest.begin(); it != manifest.end(); ++it) {
        const QJsonArray pages = it.value().toArray();
        for (qsizetype index = 0; index < pages.size(); ++index) {
            result.emplace_back(QStringLiteral("%1 page %2").arg(it.key()).arg(index + 1),
                                pages.at(index).toObject());
        }
    }
    return result;
}

// A manifest page, built from its boxes and /Rotate as written in the file
// (after inheritance), as the PDF adapter will read them.
PageGeometry geometryOf(const QJsonObject &page)
{
    return {rectFromJson(page[QStringLiteral("media_box")].toArray()),
            optionalRectFromJson(page[QStringLiteral("crop_box")]),
            rotationFromDegrees(page[QStringLiteral("rotate")].toInt())};
}

} // namespace

class TestPageGeometry : public QObject {
    Q_OBJECT

private slots:
    void readsRotateLikePdfium_data();
    void readsRotateLikePdfium();
    void visibleBoxIsTheCropBoxClippedToTheMediaBox();
    void handlesDegenerateBoxesLikePdfium();
    void swapsDisplayedSizeWhenSideways();
    void matchesVisibleAreaOfTheCorpus_data();
    void matchesVisibleAreaOfTheCorpus();
    void mapsDisplayedCornersOfTheCorpus_data();
    void mapsDisplayedCornersOfTheCorpus();
    void convertsASelectedRectangle();
    void screenAndUserSpaceRoundTrip_data();
    void screenAndUserSpaceRoundTrip();
    void inverseMatrixUndoesTheRotation();
};

void TestPageGeometry::readsRotateLikePdfium_data()
{
    QTest::addColumn<int>("degrees");
    QTest::addColumn<int>("expected");

    QTest::newRow("0") << 0 << 0;
    QTest::newRow("90") << 90 << 90;
    QTest::newRow("180") << 180 << 180;
    QTest::newRow("270") << 270 << 270;
    QTest::newRow("360") << 360 << 0;
    QTest::newRow("450") << 450 << 90;
    QTest::newRow("-90") << -90 << 270;
    QTest::newRow("-180") << -180 << 180;
    QTest::newRow("-450") << -450 << 270;
    QTest::newRow("135 truncated") << 135 << 90;
    QTest::newRow("45 truncated") << 45 << 0;
    QTest::newRow("-45 truncated") << -45 << 0;
}

void TestPageGeometry::readsRotateLikePdfium()
{
    QFETCH(int, degrees);
    QFETCH(int, expected);

    QCOMPARE(static_cast<int>(rotationFromDegrees(degrees)), expected);
}

void TestPageGeometry::visibleBoxIsTheCropBoxClippedToTheMediaBox()
{
    const PdfRect media{0.0, 0.0, 700.0, 950.0};

    QCOMPARE(PageGeometry(media, std::nullopt, Rotation::None).visibleBox(), media);
    QCOMPARE(PageGeometry(media, PdfRect{60.0, 90.0, 655.0, 931.0}, Rotation::None).visibleBox(),
             (PdfRect{60.0, 90.0, 655.0, 931.0}));
    // Partly outside the MediaBox: clipped.
    QCOMPARE(PageGeometry(media, PdfRect{-20.0, 100.0, 800.0, 900.0}, Rotation::None).visibleBox(),
             (PdfRect{0.0, 100.0, 700.0, 900.0}));
    // Corners given in reverse order are normalized.
    QCOMPARE(
        PageGeometry({700.0, 950.0, 0.0, 0.0}, PdfRect{655.0, 931.0, 60.0, 90.0}, Rotation::None)
            .visibleBox(),
        (PdfRect{60.0, 90.0, 655.0, 931.0}));
}

// Degenerate boxes follow PDFium, as observed in Qt PDF (PHY-83 spike).
void TestPageGeometry::handlesDegenerateBoxesLikePdfium()
{
    const PdfRect media{0.0, 0.0, 700.0, 950.0};

    // Entirely outside: nothing is visible.
    const PageGeometry outside(media, PdfRect{800.0, 800.0, 900.0, 900.0}, Rotation::None);
    QVERIFY(!outside.hasVisibleArea());
    QCOMPARE(outside.displayedWidth(), 0.0);
    // An empty CropBox: the MediaBox is visible.
    QCOMPARE(PageGeometry(media, PdfRect{60.0, 90.0, 60.0, 90.0}, Rotation::None).visibleBox(),
             media);
    // An empty MediaBox stands for US Letter, then clips the CropBox.
    QCOMPARE(PageGeometry({0.0, 0.0, 0.0, 0.0}, PdfRect{60.0, 90.0, 655.0, 931.0}, Rotation::None)
                 .visibleBox(),
             (PdfRect{60.0, 90.0, 612.0, 792.0}));
    QVERIFY(PageGeometry(media, std::nullopt, Rotation::None).hasVisibleArea());
}

void TestPageGeometry::swapsDisplayedSizeWhenSideways()
{
    QCOMPARE(a4(Rotation::None).displayedWidth(), kA4Width);
    QCOMPARE(a4(Rotation::Clockwise180).displayedHeight(), kA4Height);
    QCOMPARE(a4(Rotation::Clockwise90).displayedWidth(), kA4Height);
    QCOMPARE(a4(Rotation::Clockwise270).displayedHeight(), kA4Width);
}

// Every page of the corpus manifest: the visible area and the displayed size
// computed from the raw boxes and /Rotate are the ones the manifest expects,
// including degenerate boxes and unusual /Rotate values.
void TestPageGeometry::matchesVisibleAreaOfTheCorpus_data()
{
    QTest::addColumn<QJsonObject>("page");

    const auto pages = corpusPages();
    QVERIFY(!pages.isEmpty());
    for (const auto &[name, page] : pages) {
        QTest::newRow(qPrintable(name)) << page;
    }
}

void TestPageGeometry::matchesVisibleAreaOfTheCorpus()
{
    QFETCH(QJsonObject, page);

    const PageGeometry geometry = geometryOf(page);
    const std::optional<PdfRect> visible =
        optionalRectFromJson(page[QStringLiteral("visible_box")]);
    QCOMPARE(geometry.hasVisibleArea(), visible.has_value());
    if (visible) {
        QVERIFY(near(geometry.visibleBox(), *visible));
    }
    const QJsonArray size = page[QStringLiteral("displayed_size")].toArray();
    QVERIFY(near(geometry.displayedWidth(), size.at(0).toDouble()));
    QVERIFY(near(geometry.displayedHeight(), size.at(1).toDouble()));
}

// Every page of the corpus manifest: the corners of the page as displayed,
// selected on screen at several zoom levels, land on the user-space coordinates
// that the fixtures print on each page.
void TestPageGeometry::mapsDisplayedCornersOfTheCorpus_data()
{
    QTest::addColumn<QJsonObject>("page");
    QTest::addColumn<double>("scale");

    const auto pages = corpusPages();
    QVERIFY(!pages.isEmpty());
    for (const auto &[name, page] : pages) {
        for (const double scale : {0.5, 1.0, 2.5}) {
            QTest::newRow(qPrintable(QStringLiteral("%1 x%2").arg(name).arg(scale)))
                << page << scale;
        }
    }
}

void TestPageGeometry::mapsDisplayedCornersOfTheCorpus()
{
    QFETCH(QJsonObject, page);
    QFETCH(double, scale);

    const PageGeometry geometry = geometryOf(page);
    if (!geometry.hasVisibleArea()) {
        return; // Nothing displayed: no corner to select (see matchesVisibleAreaOfTheCorpus).
    }
    const double right = geometry.displayedWidth() * scale;
    const double bottom = geometry.displayedHeight() * scale;
    const QJsonObject corners = page[QStringLiteral("displayed_corners_in_user_space")].toObject();
    const QList<std::pair<QString, ScreenPoint>> screenCorners{
        {QStringLiteral("top_left"), {0.0, 0.0}},
        {QStringLiteral("top_right"), {right, 0.0}},
        {QStringLiteral("bottom_left"), {0.0, bottom}},
        {QStringLiteral("bottom_right"), {right, bottom}},
    };
    for (const auto &[name, screen] : screenCorners) {
        const QJsonArray expected = corners[name].toArray();
        const PdfPoint user = geometry.toUser(screen, scale);
        QVERIFY2(near(user.x, expected.at(0).toDouble()) && near(user.y, expected.at(1).toDouble()),
                 qPrintable(QStringLiteral("%1: got (%2, %3)").arg(name).arg(user.x).arg(user.y)));
    }
}

// A4 portrait with /Rotate 90 is displayed as landscape. At 2 pixels per point,
// a selection from (100, 200) to (500, 300) pixels covers displayed x 50..250
// and displayed y (from the top) 100..150.
void TestPageGeometry::convertsASelectedRectangle()
{
    const PageGeometry geometry = a4(Rotation::Clockwise90);
    const ScreenRect selection = ScreenRect::fromCorners({500.0, 300.0}, {100.0, 200.0});

    const PdfRect user = geometry.toUser(selection, 2.0);

    // Displayed x runs along user y; the displayed top edge is user x = 0.
    QVERIFY(near(user.left, 100.0));
    QVERIFY(near(user.right, 150.0));
    QVERIFY(near(user.bottom, 50.0));
    QVERIFY(near(user.top, 250.0));
    QCOMPARE(geometry.toScreen(user, 2.0), (ScreenRect{100.0, 200.0, 500.0, 300.0}));
}

void TestPageGeometry::screenAndUserSpaceRoundTrip_data()
{
    QTest::addColumn<int>("rotation");
    QTest::addColumn<bool>("offsetCropBox");

    for (const int rotation : {0, 90, 180, 270}) {
        QTest::newRow(qPrintable(QStringLiteral("%1").arg(rotation))) << rotation << false;
        QTest::newRow(qPrintable(QStringLiteral("%1 offset CropBox").arg(rotation)))
            << rotation << true;
    }
}

void TestPageGeometry::screenAndUserSpaceRoundTrip()
{
    QFETCH(int, rotation);
    QFETCH(bool, offsetCropBox);

    const std::optional<PdfRect> crop =
        offsetCropBox ? std::optional<PdfRect>(PdfRect{20.0, 30.0, 632.0, 822.0}) : std::nullopt;
    const PageGeometry geometry({-50.0, -40.0, 700.0, 860.0}, crop, rotationFromDegrees(rotation));

    for (const ScreenPoint screen :
         {ScreenPoint{0.0, 0.0}, ScreenPoint{123.4, 56.7}, ScreenPoint{801.0, 333.3}}) {
        const ScreenPoint back = geometry.toScreen(geometry.toUser(screen, 1.75), 1.75);
        QVERIFY(near(back.x, screen.x));
        QVERIFY(near(back.y, screen.y));
    }
}

void TestPageGeometry::inverseMatrixUndoesTheRotation()
{
    for (const Rotation rotation :
         {Rotation::None, Rotation::Clockwise90, Rotation::Clockwise180, Rotation::Clockwise270}) {
        const PageGeometry geometry({0.0, 0.0, 700.0, 950.0}, PdfRect{60.0, 90.0, 655.0, 931.0},
                                    rotation);
        const AffineMatrix forward = geometry.displayedToUser();
        const AffineMatrix inverse = geometry.userToDisplayed();
        const PdfPoint point{12.0, 345.0};
        const PdfPoint back = inverse.map(forward.map(point));
        QVERIFY(near(back.x, point.x));
        QVERIFY(near(back.y, point.y));
    }
}

QTEST_APPLESS_MAIN(TestPageGeometry)
#include "tst_page_geometry.moc"
