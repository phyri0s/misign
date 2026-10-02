#include "adapters/podofo_pdf_inspector.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <cmath>
#include <filesystem>
#include <optional>
#include <podofo/podofo.h>
#include <variant>
#include <vector>

using misign::adapters::PodofoPdfInspector;
using misign::application::DocumentInfo;
using misign::application::InspectionError;
using misign::application::InspectionResult;
using misign::application::PageInfo;
using misign::domain::PageGeometry;
using misign::domain::PdfRect;

namespace {

constexpr double kTolerance = 1e-6;

bool near(double actual, double expected)
{
    return std::abs(actual - expected) < kTolerance;
}

bool near(const PdfRect &actual, const PdfRect &expected)
{
    return near(actual.left, expected.left) && near(actual.bottom, expected.bottom) &&
           near(actual.right, expected.right) && near(actual.top, expected.top);
}

bool near(const std::optional<PdfRect> &actual, const std::optional<PdfRect> &expected)
{
    if (!actual || !expected) {
        return actual.has_value() == expected.has_value();
    }
    return near(*actual, *expected);
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

// What a page is expected to say: boxes and /Rotate as written.
struct ExpectedPage {
    PdfRect mediaBox{};
    std::optional<PdfRect> cropBox;
    int rotate = 0;
};

ExpectedPage expectedFromManifest(const QJsonObject &page)
{
    return {rectFromJson(page[QStringLiteral("media_box")].toArray()),
            optionalRectFromJson(page[QStringLiteral("crop_box")]),
            page[QStringLiteral("rotate")].toInt()};
}

bool matches(const PageInfo &page, const ExpectedPage &expected)
{
    return near(page.mediaBox, expected.mediaBox) && near(page.cropBox, expected.cropBox) &&
           page.rotate == expected.rotate;
}

// The page is displayed at the size the manifest gives.
bool hasDisplayedSize(const PageInfo &page, const QJsonArray &size)
{
    const PageGeometry geometry = page.geometry();
    return near(geometry.displayedWidth(), size.at(0).toDouble()) &&
           near(geometry.displayedHeight(), size.at(1).toDouble());
}

// The number, from 1, of the first page whose boxes, /Rotate or displayed size
// differ from the manifest; 0 when every page matches.
qsizetype firstPageUnlikeTheManifest(const DocumentInfo &document, const QJsonArray &pages)
{
    for (qsizetype index = 0; index < pages.size(); ++index) {
        const QJsonObject expected = pages.at(index).toObject();
        const PageInfo &page = document.pages.at(static_cast<std::size_t>(index));
        if (!matches(page, expectedFromManifest(expected)) ||
            !hasDisplayedSize(page, expected[QStringLiteral("displayed_size")].toArray())) {
            return index + 1;
        }
    }
    return 0;
}

// DocMDP /P, 0 when the document is not certified.
int certificationLevel(const DocumentInfo &document)
{
    return document.certification ? static_cast<int>(*document.certification) : 0;
}

std::vector<PdfRect> signatureWidgets(const DocumentInfo &document)
{
    std::vector<PdfRect> widgets;
    for (const PageInfo &page : document.pages) {
        widgets.insert(widgets.end(), page.signatureWidgets.begin(), page.signatureWidgets.end());
    }
    return widgets;
}

QByteArray pageLabel(qsizetype index)
{
    return QByteArray("page ") + QByteArray::number(index + 1);
}

QString fixture(const QString &name)
{
    return QStringLiteral(MISIGN_FIXTURES_DIR "/") + name;
}

std::filesystem::path toPath(const QString &file)
{
    return {file.toStdU16String()};
}

InspectionResult inspect(const QString &file)
{
    return PodofoPdfInspector().inspect(toPath(file));
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

QByteArray contentHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}

// The signature field that generate.py adds to the signed and certified files.
constexpr PdfRect kSignatureWidget{60.0, 60.0, 260.0, 120.0};

} // namespace

class TestPodofoPdfInspector : public QObject {
    Q_OBJECT

private slots:
    void readsTheCorpusAsTheManifestDescribesIt_data();
    void readsTheCorpusAsTheManifestDescribesIt();
    void followsPageTreeInheritance();
    void readsTheCertificationLevel_data();
    void readsTheCertificationLevel();
    void findsSignatureWidgets_data();
    void findsSignatureWidgets();
    void leavesTheFileUntouched();
    void readsAFileWithANonAsciiName();
    void reportsAMissingFile();
    void reportsAFileThatIsNotAPdf();
    void reportsATruncatedPdf();
    void reportsAPasswordProtectedPdf();
};

void TestPodofoPdfInspector::readsTheCorpusAsTheManifestDescribesIt_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QJsonArray>("pages");

    const QJsonObject files = manifest();
    QVERIFY(!files.isEmpty());
    for (auto it = files.begin(); it != files.end(); ++it) {
        QTest::newRow(qPrintable(it.key())) << it.key() << it.value().toArray();
    }
}

// Every file of the corpus: MediaBox, CropBox and /Rotate of each page are the
// ones the manifest lists, as written and with inheritance resolved, and the
// geometry built from them is displayed at the size the manifest expects.
void TestPodofoPdfInspector::readsTheCorpusAsTheManifestDescribesIt()
{
    QFETCH(QString, name);
    QFETCH(QJsonArray, pages);

    const InspectionResult result = inspect(fixture(name));

    const auto *document = std::get_if<DocumentInfo>(&result);
    QVERIFY(document != nullptr);
    QCOMPARE(static_cast<qsizetype>(document->pages.size()), pages.size());
    QCOMPARE(firstPageUnlikeTheManifest(*document, pages), 0);
}

// inherited-attributes.pdf, spelled out: the root node sets an A4 MediaBox and
// /Rotate 90, an intermediate node sets other boxes and /Rotate 180.
void TestPodofoPdfInspector::followsPageTreeInheritance()
{
    const InspectionResult result = inspect(fixture(QStringLiteral("inherited-attributes.pdf")));

    const auto *document = std::get_if<DocumentInfo>(&result);
    QVERIFY(document != nullptr);
    QCOMPARE(document->pages.size(), std::size_t{4});
    const PdfRect a4{0.0, 0.0, 595.276, 841.89};
    const PdfRect wide{0.0, 0.0, 700.0, 950.0};
    const PdfRect offset{60.0, 90.0, 655.276, 931.89};
    const std::array<ExpectedPage, 4> expected{{
        // Everything from the root.
        {a4, std::nullopt, 90},
        // Everything from the intermediate node.
        {wide, offset, 180},
        // Its own /Rotate 0 over the inherited 180.
        {wide, offset, 0},
        // Its own MediaBox, /Rotate from the root.
        {{0.0, 0.0, 792.0, 612.0}, std::nullopt, 90},
    }};

    for (std::size_t index = 0; index < expected.size(); ++index) {
        QVERIFY2(matches(document->pages.at(index), expected.at(index)),
                 pageLabel(static_cast<qsizetype>(index)).constData());
    }
}

void TestPodofoPdfInspector::readsTheCertificationLevel_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<int>("level"); // 0: not certified

    const QJsonObject files = manifest();
    QVERIFY(!files.isEmpty());
    const QHash<QString, int> certified{
        {QStringLiteral("certified-p1-a4.pdf"), 1},
        {QStringLiteral("certified-p2-a4.pdf"), 2},
        {QStringLiteral("certified-p3-a4.pdf"), 3},
    };
    for (auto it = certified.begin(); it != certified.end(); ++it) {
        QVERIFY(files.contains(it.key()));
    }
    for (auto it = files.begin(); it != files.end(); ++it) {
        QTest::newRow(qPrintable(it.key())) << it.key() << certified.value(it.key(), 0);
    }
}

// P = 1, 2, 3 for the certified files, none for every other file of the
// corpus, signed-a4.pdf included: an approval signature does not certify.
void TestPodofoPdfInspector::readsTheCertificationLevel()
{
    QFETCH(QString, name);
    QFETCH(int, level);

    const InspectionResult result = inspect(fixture(name));

    const auto *document = std::get_if<DocumentInfo>(&result);
    QVERIFY(document != nullptr);
    QCOMPARE(certificationLevel(*document), level);
}

void TestPodofoPdfInspector::findsSignatureWidgets_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("signedFile");

    const QJsonObject files = manifest();
    QVERIFY(!files.isEmpty());
    for (auto it = files.begin(); it != files.end(); ++it) {
        const bool signedFile = it.key().startsWith(QStringLiteral("signed-")) ||
                                it.key().startsWith(QStringLiteral("certified-"));
        QTest::newRow(qPrintable(it.key())) << it.key() << signedFile;
    }
}

// The signed and certified files carry one visible signature on their only
// page; no other page of the corpus has a signature widget.
void TestPodofoPdfInspector::findsSignatureWidgets()
{
    QFETCH(QString, name);
    QFETCH(bool, signedFile);

    const InspectionResult result = inspect(fixture(name));

    const auto *document = std::get_if<DocumentInfo>(&result);
    QVERIFY(document != nullptr);
    const std::vector<PdfRect> widgets = signatureWidgets(*document);
    QCOMPARE(widgets.size(), std::size_t{signedFile ? 1U : 0U});
    if (signedFile) {
        QVERIFY(near(widgets.front(), kSignatureWidget));
    }
}

void TestPodofoPdfInspector::leavesTheFileUntouched()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString copy = directory.filePath(QStringLiteral("signed.pdf"));
    QVERIFY(QFile::copy(fixture(QStringLiteral("signed-a4.pdf")), copy));
    const QByteArray before = contentHash(copy);
    QVERIFY(!before.isEmpty());

    const InspectionResult result = inspect(copy);

    QVERIFY(std::holds_alternative<DocumentInfo>(result));
    QCOMPARE(contentHash(copy), before);
    // Nothing else appears next to it, and it can be removed: it is not kept open.
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files).size(), 1);
    QVERIFY(QFile::remove(copy));
}

void TestPodofoPdfInspector::readsAFileWithANonAsciiName()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString copy = directory.filePath(QStringLiteral("contrat signé – 契約.pdf"));
    QVERIFY(QFile::copy(fixture(QStringLiteral("a4-portrait.pdf")), copy));

    const InspectionResult result = inspect(copy);

    const auto *document = std::get_if<DocumentInfo>(&result);
    QVERIFY(document != nullptr);
    QCOMPARE(document->pages.size(), std::size_t{1});
}

void TestPodofoPdfInspector::reportsAMissingFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const InspectionResult result = inspect(directory.filePath(QStringLiteral("missing.pdf")));

    const auto *error = std::get_if<InspectionError>(&result);
    QVERIFY(error != nullptr);
    QCOMPARE(*error, InspectionError::Unreadable);
}

void TestPodofoPdfInspector::reportsAFileThatIsNotAPdf()
{
    const InspectionResult result = inspect(fixture(QStringLiteral("manifest.json")));

    const auto *error = std::get_if<InspectionError>(&result);
    QVERIFY(error != nullptr);
    QCOMPARE(*error, InspectionError::NotAPdf);
}

void TestPodofoPdfInspector::reportsATruncatedPdf()
{
    QFile source(fixture(QStringLiteral("a4-portrait.pdf")));
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray bytes = source.readAll();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile truncated(directory.filePath(QStringLiteral("truncated.pdf")));
    QVERIFY(truncated.open(QIODevice::WriteOnly));
    QCOMPARE(truncated.write(bytes.first(bytes.size() / 2)), bytes.size() / 2);
    truncated.close();

    const InspectionResult result = inspect(truncated.fileName());

    const auto *error = std::get_if<InspectionError>(&result);
    QVERIFY(error != nullptr);
    QCOMPARE(*error, InspectionError::Damaged);
}

// The corpus has no encrypted file: one is written here with PoDoFo.
void TestPodofoPdfInspector::reportsAPasswordProtectedPdf()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString file = directory.filePath(QStringLiteral("protected.pdf"));
    {
        PoDoFo::PdfMemDocument document;
        document.Load(fixture(QStringLiteral("a4-portrait.pdf")).toStdString());
        document.SetEncrypted("user password", "owner password");
        document.Save(file.toStdString());
    }

    const InspectionResult result = inspect(file);

    const auto *error = std::get_if<InspectionError>(&result);
    QVERIFY(error != nullptr);
    QCOMPARE(*error, InspectionError::PasswordProtected);
}

QTEST_APPLESS_MAIN(TestPodofoPdfInspector)
#include "tst_podofo_pdf_inspector.moc"
