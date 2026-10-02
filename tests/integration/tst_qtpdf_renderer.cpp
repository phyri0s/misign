#include "adapters/qtpdf_renderer.h"

#include <QColor>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <filesystem>
#include <optional>
#include <podofo/podofo.h>

using misign::adapters::QtPdfRenderer;
using misign::application::DocumentError;
using misign::application::PageImage;
using misign::application::PageSize;

namespace {

constexpr double kTolerance = 1e-3;

QString fixture(const QString &name)
{
    return QStringLiteral(MISIGN_FIXTURES_DIR "/") + name;
}

std::filesystem::path toPath(const QString &file)
{
    return {file.toStdU16String()};
}

// tests/fixtures/pdf/manifest.json: the pages of every file of the corpus.
// Empty when the manifest cannot be read.
QJsonObject manifest()
{
    QFile file(fixture(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool hasSize(const PageSize &actual, const QJsonArray &expected)
{
    return std::abs(actual.width - expected.at(0).toDouble()) < kTolerance &&
           std::abs(actual.height - expected.at(1).toDouble()) < kTolerance;
}

// The number, from 1, of the first page whose size differs from the
// `displayed_size` of the manifest; 0 when every page matches.
int firstPageOfAnotherSize(const QtPdfRenderer &renderer, const QJsonArray &pages)
{
    for (int index = 0; index < pages.size(); ++index) {
        const QJsonObject page = pages.at(index).toObject();
        if (!hasSize(renderer.pageSize(index), page[QStringLiteral("displayed_size")].toArray())) {
            return index + 1;
        }
    }
    return 0;
}

QColor pixel(const PageImage &image, int x, int y)
{
    const auto index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width)) +
                       static_cast<std::size_t>(x);
    return QColor::fromRgba(image.pixels.at(index));
}

// The number of blue pixels in the middle fifth of the image, between the
// rows at `from` and `to`, given as fractions of its height.
int bluePixelsInTheMiddle(const PageImage &image, double from, double to)
{
    int count = 0;
    for (int y = static_cast<int>(image.height * from); y < static_cast<int>(image.height * to);
         ++y) {
        for (int x = image.width * 2 / 5; x < image.width * 3 / 5; ++x) {
            const QColor colour = pixel(image, x, y);
            count += (colour.blue() > 150 && colour.red() < 80 && colour.green() < 80) ? 1 : 0;
        }
    }
    return count;
}

// A copy of a4-portrait.pdf with a red rectangle as a Stamp annotation, the
// way Misign writes a drawn signature (ADR 0005): user space [100 100 200 150].
bool writeStampedPdf(const QString &file)
{
    try {
        PoDoFo::PdfMemDocument document;
        document.Load(fixture(QStringLiteral("a4-portrait.pdf")).toStdString());
        auto form = document.CreateXObjectForm(PoDoFo::Rect(0, 0, 100, 50));
        PoDoFo::PdfPainter painter;
        painter.SetCanvas(*form, PoDoFo::PdfPainterFlags::RawCoordinates);
        painter.GraphicsState.SetNonStrokingColor(PoDoFo::PdfColor(1.0, 0.0, 0.0));
        painter.DrawRectangle(0, 0, 100, 50, PoDoFo::PdfPathDrawMode::Fill);
        painter.FinishDrawing();
        auto &annotation = document.GetPages()
                               .GetPageAt(0)
                               .GetAnnotations()
                               .CreateAnnot<PoDoFo::PdfAnnotationStamp>(PoDoFo::Rect(0, 0, 1, 1));
        annotation.SetRectRaw(PoDoFo::Corners(100, 100, 200, 150));
        annotation.SetFlags(PoDoFo::PdfAnnotationFlags::Print);
        annotation.SetAppearanceStreamRaw(*form);
        document.Save(file.toStdString());
        return true;
    } catch (const PoDoFo::PdfError &) {
        return false;
    }
}

} // namespace

class TestQtPdfRenderer : public QObject {
    Q_OBJECT

private slots:
    void givesTheDisplayedSizeOfEveryPageOfTheCorpus_data();
    void givesTheDisplayedSizeOfEveryPageOfTheCorpus();
    void rendersARotatedPageAtTheScaledDisplayedSize();
    void rendersEveryPageOfTheCorpusUpright_data();
    void rendersEveryPageOfTheCorpusUpright();
    void rendersOnOpaqueWhitePaper();
    void rendersAnnotations();
    void rendersNothingForAPageWithoutAVisibleArea();
    void rendersNothingForAPageThatDoesNotExist();
    void rendersNothingAtAScaleThatIsNotPositive();
    void hasNoPageBeforeADocumentIsOpenOrAfterItIsClosed();
    void replacesTheOpenDocument();
    void reportsAMissingFile();
    void reportsAFileThatIsNotAPdf();
    void reportsAPasswordProtectedPdf();
    void hasNoPageAfterAFailedOpen();
};

void TestQtPdfRenderer::givesTheDisplayedSizeOfEveryPageOfTheCorpus_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QJsonArray>("pages");

    const QJsonObject files = manifest();
    QVERIFY(!files.isEmpty());
    for (auto it = files.begin(); it != files.end(); ++it) {
        QTest::newRow(qPrintable(it.key())) << it.key() << it.value().toArray();
    }
}

// Every file of the corpus: the page count and the size of each page as
// displayed (visible area turned by /Rotate, 0 x 0 when nothing is visible)
// are the ones the manifest lists.
void TestQtPdfRenderer::givesTheDisplayedSizeOfEveryPageOfTheCorpus()
{
    QFETCH(QString, name);
    QFETCH(QJsonArray, pages);
    QtPdfRenderer renderer;

    QVERIFY(!renderer.open(toPath(fixture(name))));

    QCOMPARE(renderer.pageCount(), static_cast<int>(pages.size()));
    QCOMPARE(firstPageOfAnotherSize(renderer, pages), 0);
}

// A4 portrait with /Rotate 90 is displayed as landscape, 841.89 x 595.276 pt.
void TestQtPdfRenderer::rendersARotatedPageAtTheScaledDisplayedSize()
{
    QtPdfRenderer renderer;
    QVERIFY(!renderer.open(toPath(fixture(QStringLiteral("a4-rotate-90.pdf")))));

    const PageImage atOne = renderer.render(0, 1.0);
    const PageImage atTwo = renderer.render(0, 2.0);
    const PageImage atHalf = renderer.render(0, 0.5);

    QCOMPARE(atOne.width, 842);
    QCOMPARE(atOne.height, 595);
    QCOMPARE(atTwo.width, 1684);
    QCOMPARE(atTwo.height, 1191);
    QCOMPARE(atHalf.width, 421);
    QCOMPARE(atHalf.height, 298);
    QCOMPARE(atTwo.pixels.size(), std::size_t{1684} * std::size_t{1191});
}

void TestQtPdfRenderer::rendersEveryPageOfTheCorpusUpright_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<int>("page");

    const QJsonObject files = manifest();
    QVERIFY(!files.isEmpty());
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QJsonArray pages = it.value().toArray();
        for (int page = 0; page < pages.size(); ++page) {
            if (!pages.at(page).toObject()[QStringLiteral("visible_box")].isNull()) {
                QTest::addRow("%s page %d", qPrintable(it.key()), page + 1) << it.key() << page;
            }
        }
    }
}

// Every page of the corpus draws a blue arrow pointing to its displayed top,
// in the middle of its width: it must come out in the upper part of the
// image and nowhere in the lower part, whatever /Rotate says. The blue frame
// along the edges is left out of both.
void TestQtPdfRenderer::rendersEveryPageOfTheCorpusUpright()
{
    QFETCH(QString, name);
    QFETCH(int, page);
    QtPdfRenderer renderer;
    QVERIFY(!renderer.open(toPath(fixture(name))));

    const PageImage image = renderer.render(page, 1.0);

    QVERIFY(!image.isEmpty());
    QVERIFY(bluePixelsInTheMiddle(image, 0.04, 0.3) > 100);
    QCOMPARE(bluePixelsInTheMiddle(image, 0.7, 0.96), 0);
}

void TestQtPdfRenderer::rendersOnOpaqueWhitePaper()
{
    QtPdfRenderer renderer;
    QVERIFY(!renderer.open(toPath(fixture(QStringLiteral("a4-portrait.pdf")))));

    const PageImage image = renderer.render(0, 1.0);

    // The middle of the page is blank on this fixture.
    QCOMPARE(pixel(image, image.width / 2, image.height / 2), QColor(Qt::white));
}

// Without RenderFlag::Annotations, Qt PDF leaves annotations out, and a drawn
// signature with them (ADR 0005).
void TestQtPdfRenderer::rendersAnnotations()
{
    const QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString file = directory.filePath(QStringLiteral("stamped.pdf"));
    QVERIFY(writeStampedPdf(file));
    QtPdfRenderer renderer;
    QVERIFY(!renderer.open(toPath(file)));

    const PageImage image = renderer.render(0, 1.0);

    // User space (150, 125) is the middle of the stamp; (300, 125) is beside it.
    QCOMPARE(pixel(image, 150, image.height - 125), QColor(Qt::red));
    QCOMPARE(pixel(image, 300, image.height - 125), QColor(Qt::white));
}

// degenerate-boxes.pdf page 3: a CropBox outside the MediaBox.
void TestQtPdfRenderer::rendersNothingForAPageWithoutAVisibleArea()
{
    QtPdfRenderer renderer;
    QVERIFY(!renderer.open(toPath(fixture(QStringLiteral("degenerate-boxes.pdf")))));

    QVERIFY(renderer.pageSize(2).isEmpty());
    QVERIFY(renderer.render(2, 1.0).isEmpty());
    // The pages around it are rendered.
    QVERIFY(!renderer.render(1, 1.0).isEmpty());
    QVERIFY(!renderer.render(3, 1.0).isEmpty());
}

void TestQtPdfRenderer::rendersNothingForAPageThatDoesNotExist()
{
    QtPdfRenderer renderer;
    QVERIFY(!renderer.open(toPath(fixture(QStringLiteral("a4-portrait.pdf")))));

    QVERIFY(renderer.pageSize(-1).isEmpty());
    QVERIFY(renderer.pageSize(1).isEmpty());
    QVERIFY(renderer.render(-1, 1.0).isEmpty());
    QVERIFY(renderer.render(1, 1.0).isEmpty());
}

void TestQtPdfRenderer::rendersNothingAtAScaleThatIsNotPositive()
{
    QtPdfRenderer renderer;
    QVERIFY(!renderer.open(toPath(fixture(QStringLiteral("a4-portrait.pdf")))));

    QVERIFY(renderer.render(0, 0.0).isEmpty());
    QVERIFY(renderer.render(0, -1.0).isEmpty());
    QVERIFY(renderer.render(0, std::nan("")).isEmpty());
    // Too small to give a single pixel.
    QVERIFY(renderer.render(0, 1e-6).isEmpty());
}

void TestQtPdfRenderer::hasNoPageBeforeADocumentIsOpenOrAfterItIsClosed()
{
    QtPdfRenderer renderer;
    QCOMPARE(renderer.pageCount(), 0);
    QVERIFY(renderer.render(0, 1.0).isEmpty());

    QVERIFY(!renderer.open(toPath(fixture(QStringLiteral("mixed.pdf")))));
    QCOMPARE(renderer.pageCount(), 5);

    renderer.close();
    QCOMPARE(renderer.pageCount(), 0);
    QVERIFY(renderer.pageSize(0).isEmpty());
}

void TestQtPdfRenderer::replacesTheOpenDocument()
{
    QtPdfRenderer renderer;
    QVERIFY(!renderer.open(toPath(fixture(QStringLiteral("mixed.pdf")))));

    QVERIFY(!renderer.open(toPath(fixture(QStringLiteral("letter-landscape.pdf")))));

    QCOMPARE(renderer.pageCount(), 1);
    QCOMPARE(renderer.render(0, 1.0).width, 792);
}

void TestQtPdfRenderer::reportsAMissingFile()
{
    const QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QtPdfRenderer renderer;

    const std::optional<DocumentError> error =
        renderer.open(toPath(directory.filePath(QStringLiteral("missing.pdf"))));

    QVERIFY(error.has_value());
    QCOMPARE(error.value_or(DocumentError::Damaged), DocumentError::Unreadable);
}

// Qt PDF cannot tell a file that is not a PDF from a damaged one.
void TestQtPdfRenderer::reportsAFileThatIsNotAPdf()
{
    QtPdfRenderer renderer;

    const std::optional<DocumentError> error =
        renderer.open(toPath(fixture(QStringLiteral("manifest.json"))));

    QVERIFY(error.has_value());
    QCOMPARE(error.value_or(DocumentError::Unreadable), DocumentError::Damaged);
}

// The corpus has no encrypted file: one is written here with PoDoFo.
void TestQtPdfRenderer::reportsAPasswordProtectedPdf()
{
    const QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString file = directory.filePath(QStringLiteral("protected.pdf"));
    {
        PoDoFo::PdfMemDocument document;
        document.Load(fixture(QStringLiteral("a4-portrait.pdf")).toStdString());
        document.SetEncrypted("user password", "owner password");
        document.Save(file.toStdString());
    }
    QtPdfRenderer renderer;

    const std::optional<DocumentError> error = renderer.open(toPath(file));

    QVERIFY(error.has_value());
    QCOMPARE(error.value_or(DocumentError::Damaged), DocumentError::PasswordProtected);
}

void TestQtPdfRenderer::hasNoPageAfterAFailedOpen()
{
    QtPdfRenderer renderer;
    QVERIFY(!renderer.open(toPath(fixture(QStringLiteral("mixed.pdf")))));

    QVERIFY(renderer.open(toPath(fixture(QStringLiteral("manifest.json")))).has_value());

    QCOMPARE(renderer.pageCount(), 0);
}

QTEST_MAIN(TestQtPdfRenderer)
#include "tst_qtpdf_renderer.moc"
