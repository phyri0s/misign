// PHY-83 spike: for every page of the given PDFs and several zoom levels, select
// a rectangle on the page as displayed, convert it to PDF user space with the
// domain (src/domain/page_geometry.h), draw it with PoDoFo as a Stamp annotation,
// render the result with Qt PDF, and compare the drawn rectangle with the
// selection by image comparison. Throwaway code, see README.md.
//
// usage: placement <out-dir> <file.pdf>...

#include "domain/page_geometry.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <podofo/podofo.h>
#include <string>
#include <vector>

using namespace misign::domain;

namespace {

// Pixels per point: 36, 72, 108 and 144 dpi.
constexpr std::array kScales{0.5, 1.0, 1.5, 2.0};
// The selection, as fractions of the displayed page: deliberately asymmetric so
// that a flipped or rotated placement cannot match by accident.
constexpr double kLeft = 0.20;
constexpr double kTop = 0.55;
constexpr double kRight = 0.60;
constexpr double kBottom = 0.70;
// Allowed error on each edge, in pixels (the rendered image size is rounded).
constexpr double kTolerance = 1.5;

enum class Mode { Raw, Naive };

const char *modeName(Mode mode)
{
    return mode == Mode::Raw ? "raw" : "naive";
}

PdfRect toPdfRect(const PoDoFo::Corners &corners)
{
    return PdfRect::fromCorners({corners.X1, corners.Y1}, {corners.X2, corners.Y2});
}

// Page geometry read from the raw page dictionary, without PoDoFo's rotation
// adjustments. The CropBox getter falls back to the MediaBox, which the domain
// treats the same way.
PageGeometry geometryOf(const PoDoFo::PdfPage &page)
{
    double rotate = 0;
    page.TryGetRotationRaw(rotate);
    return {toPdfRect(page.GetMediaBoxRaw()), toPdfRect(page.GetCropBoxRaw()),
            rotationFromDegrees(static_cast<int>(rotate))};
}

ScreenRect selection(const PageGeometry &geometry, double scale)
{
    const double width = geometry.displayedWidth() * scale;
    const double height = geometry.displayedHeight() * scale;
    return {width * kLeft, height * kTop, width * kRight, height * kBottom};
}

// Red box with a black square in its displayed top-left corner, drawn in a form
// of the given size (points).
std::unique_ptr<PoDoFo::PdfXObjectForm> drawBox(PoDoFo::PdfMemDocument &doc, double width,
                                                double height)
{
    auto form = doc.CreateXObjectForm(PoDoFo::Rect(0, 0, width, height));
    PoDoFo::PdfPainter painter;
    painter.SetCanvas(*form, PoDoFo::PdfPainterFlags::RawCoordinates);
    painter.GraphicsState.SetNonStrokingColor(PoDoFo::PdfColor(1.0, 0.0, 0.0));
    painter.DrawRectangle(0, 0, width, height, PoDoFo::PdfPathDrawMode::Fill);
    const double marker = std::min(width, height) * 0.3;
    painter.GraphicsState.SetNonStrokingColor(PoDoFo::PdfColor(0.0, 0.0, 0.0));
    painter.DrawRectangle(0, height - marker, marker, marker, PoDoFo::PdfPathDrawMode::Fill);
    painter.FinishDrawing();
    return form;
}

void placeBox(PoDoFo::PdfMemDocument &doc, PoDoFo::PdfPage &page, Mode mode, double scale)
{
    const PageGeometry geometry = geometryOf(page);
    const ScreenRect screen = selection(geometry, scale);
    const PdfRect user = geometry.toUser(screen, scale);

    if (mode == Mode::Raw) {
        // Drawn in displayed orientation; the form matrix turns it like the
        // page content, so it shows upright once /Rotate is applied.
        auto form = drawBox(doc, (screen.right - screen.left) / scale,
                            (screen.bottom - screen.top) / scale);
        const AffineMatrix m = geometry.displayedToUser();
        form->SetMatrix(PoDoFo::Matrix(m.a, m.b, m.c, m.d, 0, 0));
        auto &annotation =
            page.GetAnnotations().CreateAnnot<PoDoFo::PdfAnnotationStamp>(PoDoFo::Rect(0, 0, 1, 1));
        annotation.SetRectRaw(PoDoFo::Corners(user.left, user.bottom, user.right, user.top));
        annotation.SetFlags(PoDoFo::PdfAnnotationFlags::Print);
        annotation.SetAppearanceStreamRaw(*form);
    } else {
        // PoDoFo's convenience API, fed with the user-space rectangle as is.
        auto form = drawBox(doc, user.width(), user.height());
        auto &annotation = page.GetAnnotations().CreateAnnot<PoDoFo::PdfAnnotationStamp>(
            PoDoFo::Rect(user.left, user.bottom, user.width(), user.height()));
        annotation.SetFlags(PoDoFo::PdfAnnotationFlags::Print);
        annotation.SetAppearanceStream(*form);
    }
}

bool isRed(QRgb pixel)
{
    return qRed(pixel) > 200 && qGreen(pixel) < 60 && qBlue(pixel) < 60;
}

bool isBlack(QRgb pixel)
{
    return qRed(pixel) < 60 && qGreen(pixel) < 60 && qBlue(pixel) < 60;
}

struct Measure {
    std::optional<ScreenRect> box;     // bounding box of the red and black pixels
    std::optional<ScreenPoint> marker; // centre of the black pixels inside it
};

// The reusable part: find a solid-colour mark on a rendered page.
Measure measure(const QImage &image, const ScreenRect &expected)
{
    // Search around the expected box only: the fixtures draw black text elsewhere.
    const int margin = 40;
    const int x0 = std::max(0, static_cast<int>(expected.left) - margin);
    const int y0 = std::max(0, static_cast<int>(expected.top) - margin);
    const int x1 = std::min(image.width(), static_cast<int>(expected.right) + margin);
    const int y1 = std::min(image.height(), static_cast<int>(expected.bottom) + margin);

    int left = image.width();
    int top = image.height();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (isRed(image.pixel(x, y))) {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x + 1);
                bottom = std::max(bottom, y + 1);
            }
        }
    }
    Measure result;
    if (right < 0) {
        return result;
    }
    result.box = ScreenRect{double(left), double(top), double(right), double(bottom)};

    double sumX = 0;
    double sumY = 0;
    int count = 0;
    for (int y = std::max(y0, top); y < std::min(y1, bottom); ++y) {
        for (int x = std::max(x0, left); x < std::min(x1, right); ++x) {
            if (isBlack(image.pixel(x, y))) {
                sumX += x;
                sumY += y;
                ++count;
            }
        }
    }
    if (count > 0) {
        result.marker = ScreenPoint{sumX / count, sumY / count};
    }
    return result;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    if (argc < 3) {
        std::fprintf(stderr, "usage: placement <out-dir> <file.pdf>...\n");
        return 2;
    }
    const QDir outDir(QString::fromLocal8Bit(argv[1]));
    QDir().mkpath(outDir.path());

    int checks = 0;
    int failures = 0;
    std::printf("%-24s %4s %6s %9s %5s %-5s  %-9s %-11s %-8s %s\n", "file", "page", "rotate",
                "cropbox", "scale", "mode", "page size", "max error", "upright", "result");

    for (int arg = 2; arg < argc; ++arg) {
        const QString input = QString::fromLocal8Bit(argv[arg]);
        const std::string name = QFileInfo(input).fileName().toStdString();
        for (const Mode mode : {Mode::Raw, Mode::Naive}) {
            for (const double scale : kScales) {
                // Place the box on every page, then save.
                PoDoFo::PdfMemDocument doc;
                doc.Load(input.toStdString());
                auto &pages = doc.GetPages();
                std::vector<PageGeometry> geometries;
                for (unsigned i = 0; i < pages.GetCount(); ++i) {
                    geometries.push_back(geometryOf(pages.GetPageAt(i)));
                    placeBox(doc, pages.GetPageAt(i), mode, scale);
                }
                const QString output =
                    outDir.filePath(QStringLiteral("%1-%2-x%3.pdf")
                                        .arg(QFileInfo(input).completeBaseName(), modeName(mode))
                                        .arg(scale));
                doc.Save(output.toStdString());

                // Render with Qt PDF and compare.
                QPdfDocument rendered;
                rendered.load(output);
                QPdfDocumentRenderOptions options;
                options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
                for (int i = 0; i < rendered.pageCount(); ++i) {
                    const PageGeometry &geometry = geometries[static_cast<size_t>(i)];
                    const QSizeF qtSize = rendered.pagePointSize(i);
                    const bool sizeOk =
                        std::abs(qtSize.width() - geometry.displayedWidth()) < 0.01 &&
                        std::abs(qtSize.height() - geometry.displayedHeight()) < 0.01;
                    const QImage image =
                        rendered.render(i,
                                        QSize(qRound(geometry.displayedWidth() * scale),
                                              qRound(geometry.displayedHeight() * scale)),
                                        options);
                    image.save(QString(output).replace(QStringLiteral(".pdf"),
                                                       QStringLiteral("-p%1.png").arg(i + 1)));

                    const ScreenRect expected = selection(geometry, scale);
                    const Measure found = measure(image, expected);
                    double error = INFINITY;
                    bool upright = false;
                    if (found.box) {
                        error = std::max({std::abs(found.box->left - expected.left),
                                          std::abs(found.box->top - expected.top),
                                          std::abs(found.box->right - expected.right),
                                          std::abs(found.box->bottom - expected.bottom)});
                        const double midX = (found.box->left + found.box->right) / 2;
                        const double midY = (found.box->top + found.box->bottom) / 2;
                        upright = found.marker && found.marker->x < midX && found.marker->y < midY;
                    }
                    const bool ok = sizeOk && error <= kTolerance && upright;
                    ++checks;
                    failures += ok ? 0 : 1;
                    const PdfRect visible = geometry.visibleBox();
                    std::printf("%-24s %4d %6d %9s %5.1f %-5s  %-9s %-11s %-8s %s\n", name.c_str(),
                                i + 1, static_cast<int>(geometry.rotation()),
                                visible.left != 0 || visible.bottom != 0 ? "offset" : "origin",
                                scale, modeName(mode), sizeOk ? "match" : "MISMATCH",
                                found.box ? QString::number(error, 'f', 2).toUtf8().constData()
                                          : "not found",
                                upright ? "yes" : "no", ok ? "ok" : "FAIL");
                }
            }
        }
    }
    std::printf("\n%d checks, %d failed\n", checks, failures);
    return 0;
}
