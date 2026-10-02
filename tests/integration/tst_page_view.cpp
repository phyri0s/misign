#include "adapters/qtpdf_renderer.h"
#include "ui/document_model.h"
#include "ui/page_image_provider.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <cmath>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using misign::adapters::QtPdfRenderer;
using misign::application::DocumentError;
using misign::application::IPdfRenderer;
using misign::application::PageImage;
using misign::application::PageSize;
using misign::ui::DocumentModel;
using misign::ui::PageImageProvider;

namespace {

// Logical pixels per point at 100 % zoom.
constexpr double kPointScale = 96.0 / 72.0;
constexpr double kTolerance = 0.01;
// Image.Ready
constexpr int kImageReady = 1;

QString fixture(const QString &name)
{
    return QStringLiteral(MISIGN_FIXTURES_DIR "/") + name;
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

bool isNear(double actual, double expected)
{
    return std::abs(actual - expected) < kTolerance;
}

// The real renderer, recording which pages are rendered and at which scale.
class RecordingRenderer final : public IPdfRenderer {
public:
    std::optional<DocumentError> open(const std::filesystem::path &file) override
    {
        return m_renderer.open(file);
    }
    void close() override { m_renderer.close(); }
    [[nodiscard]] int pageCount() const override { return m_renderer.pageCount(); }
    [[nodiscard]] PageSize pageSize(int page) const override { return m_renderer.pageSize(page); }
    [[nodiscard]] PageImage render(int page, double scale) override
    {
        m_renders.emplace_back(page, scale);
        return m_renderer.render(page, scale);
    }

    // The scale of the last render of the page, nothing when it was not rendered.
    [[nodiscard]] std::optional<double> lastScale(int page) const
    {
        std::optional<double> scale;
        for (const auto &[renderedPage, renderedScale] : m_renders) {
            if (renderedPage == page) {
                scale = renderedScale;
            }
        }
        return scale;
    }

private:
    QtPdfRenderer m_renderer;
    std::vector<std::pair<int, double>> m_renders;
};

// The application window on one document, set up as main.cpp does.
class Viewer {
public:
    explicit Viewer(const QString &file) : m_document(m_renderer)
    {
        m_opened = !m_document.open(file);
        m_engine.addImageProvider(QLatin1String(PageImageProvider::kProviderId),
                                  new PageImageProvider(m_renderer));
        m_engine.setInitialProperties(
            {{QStringLiteral("document"), QVariant::fromValue(&m_document)}});
        m_engine.loadFromModule("Misign", "Main");
        if (!m_engine.rootObjects().isEmpty()) {
            m_window = qobject_cast<QQuickWindow *>(m_engine.rootObjects().constFirst());
        }
        if (m_window != nullptr) {
            m_view = m_window->findChild<QQuickItem *>(QStringLiteral("pageView"));
        }
    }

    // The document is open and its window shows the page view.
    [[nodiscard]] bool isShown() const { return m_opened && m_view != nullptr; }
    [[nodiscard]] const RecordingRenderer &renderer() const { return m_renderer; }
    [[nodiscard]] double devicePixelRatio() const { return m_window->effectiveDevicePixelRatio(); }

    void scrollTo(int page)
    {
        QMetaObject::invokeMethod(m_view, "positionViewAtIndex", Q_ARG(int, page),
                                  Q_ARG(int, 0)); // ListView.Beginning
    }

    // The delegate of the page, null while the view has not created it.
    [[nodiscard]] QQuickItem *page(int index) const
    {
        QQuickItem *item = nullptr;
        QMetaObject::invokeMethod(m_view, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, item),
                                  Q_ARG(int, index));
        return item;
    }

    // Whether the page is in the view at its displayed size at 100 % zoom,
    // with its image rendered and displayed.
    [[nodiscard]] bool shows(int index, const QJsonArray &displayedSize) const
    {
        const Parts parts = partsOf(index);
        return parts.isComplete() &&
               isNear(parts.sheet->width(), displayedSize.at(0).toDouble() * kPointScale) &&
               isNear(parts.sheet->height(), displayedSize.at(1).toDouble() * kPointScale) &&
               parts.image->isVisible() && parts.image->property("status").toInt() == kImageReady &&
               !parts.placeholder->isVisible();
    }

    // Whether the page is in the view as the placeholder of a page without a
    // visible area.
    [[nodiscard]] bool showsPlaceholder(int index) const
    {
        const Parts parts = partsOf(index);
        return parts.isComplete() && parts.sheet->width() > 0.0 && parts.sheet->height() > 0.0 &&
               !parts.image->isVisible() && parts.placeholder->isVisible();
    }

private:
    // The items of a page delegate, null while the view has not created it.
    struct Parts {
        const QQuickItem *sheet = nullptr;
        const QQuickItem *image = nullptr;
        const QQuickItem *placeholder = nullptr;

        [[nodiscard]] bool isComplete() const
        {
            return sheet != nullptr && image != nullptr && placeholder != nullptr;
        }
    };

    [[nodiscard]] Parts partsOf(int index) const
    {
        const QQuickItem *delegate = page(index);
        if (delegate == nullptr) {
            return {};
        }
        return {delegate->findChild<QQuickItem *>(QStringLiteral("sheet")),
                delegate->findChild<QQuickItem *>(QStringLiteral("image")),
                delegate->findChild<QQuickItem *>(QStringLiteral("placeholder"))};
    }

    RecordingRenderer m_renderer;
    DocumentModel m_document;
    QQmlApplicationEngine m_engine;
    QQuickWindow *m_window = nullptr;
    QQuickItem *m_view = nullptr;
    bool m_opened = false;
};

} // namespace

class TestPageView : public QObject {
    Q_OBJECT

private slots:
    void showsEveryPageOfTheCorpusAtItsDisplayedSize_data();
    void showsEveryPageOfTheCorpusAtItsDisplayedSize();
    void rendersAPageOnlyWhenItScrollsIntoView();
    void showsNothingWhenTheDocumentCannotBeOpened();
};

void TestPageView::showsEveryPageOfTheCorpusAtItsDisplayedSize_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<int>("page");
    QTest::addColumn<QJsonArray>("displayedSize");

    const QJsonObject files = manifest();
    QVERIFY(!files.isEmpty());
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QJsonArray pages = it.value().toArray();
        for (int page = 0; page < pages.size(); ++page) {
            QTest::addRow("%s page %d", qPrintable(it.key()), page + 1)
                << it.key() << page
                << pages.at(page).toObject()[QStringLiteral("displayed_size")].toArray();
        }
    }
}

// Every page of the corpus, scrolled into view: its sheet has the displayed
// size of the manifest at 100 % zoom, and it was rendered at that zoom times
// the device pixel ratio. A page with nothing visible shows a placeholder and
// is not rendered.
void TestPageView::showsEveryPageOfTheCorpusAtItsDisplayedSize()
{
    QFETCH(QString, name);
    QFETCH(int, page);
    QFETCH(QJsonArray, displayedSize);
    Viewer viewer(fixture(name));
    QVERIFY(viewer.isShown());

    viewer.scrollTo(page);

    if (displayedSize.at(0).toDouble() > 0.0) {
        QVERIFY(QTest::qWaitFor([&] { return viewer.shows(page, displayedSize); }));
        QVERIFY(isNear(viewer.renderer().lastScale(page).value_or(0.0),
                       kPointScale * viewer.devicePixelRatio()));
    } else {
        QVERIFY(QTest::qWaitFor([&] { return viewer.showsPlaceholder(page); }));
        QVERIFY(!viewer.renderer().lastScale(page).has_value());
    }
}

// mixed.pdf has 5 pages, each taller than the window.
void TestPageView::rendersAPageOnlyWhenItScrollsIntoView()
{
    Viewer viewer(fixture(QStringLiteral("mixed.pdf")));
    QVERIFY(viewer.isShown());

    QVERIFY(QTest::qWaitFor([&] { return viewer.renderer().lastScale(0).has_value(); }));
    QVERIFY(!viewer.renderer().lastScale(3).has_value());
    QVERIFY(!viewer.renderer().lastScale(4).has_value());

    viewer.scrollTo(4);

    QVERIFY(QTest::qWaitFor([&] { return viewer.renderer().lastScale(4).has_value(); }));
}

void TestPageView::showsNothingWhenTheDocumentCannotBeOpened()
{
    RecordingRenderer renderer;
    DocumentModel document(renderer);
    QVERIFY(!document.open(fixture(QStringLiteral("mixed.pdf"))));
    QCOMPARE(document.rowCount(), 5);
    const int generation = document.generation();

    QVERIFY(document.open(fixture(QStringLiteral("manifest.json"))).has_value());

    QCOMPARE(document.rowCount(), 0);
    QVERIFY(document.generation() != generation);
}

QTEST_MAIN(TestPageView)
#include "tst_page_view.moc"
