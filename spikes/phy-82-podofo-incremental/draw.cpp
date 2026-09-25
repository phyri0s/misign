// PHY-82 spike: draw a few Bézier curves on page 1 of a PDF with PoDoFo, then
// save them as an incremental update. Throwaway code, see README.md.
//
// usage: draw <content|annotation> <file> [--no-metadata-update] [--no-collect-garbage]
//
// <file> is modified in place: copy the signed PDF first.

#include <cstring>
#include <iostream>
#include <podofo/podofo.h>
#include <string_view>

using namespace PoDoFo;

namespace {

// Drawing area, in page user space (bottom-left origin, points).
constexpr double kX = 300;
constexpr double kY = 100;
constexpr double kWidth = 220;
constexpr double kHeight = 80;

// A signature-like stroke made of cubic Bézier curves, in coordinates relative
// to the drawing area.
void drawSignature(PdfPainter &painter)
{
    painter.GraphicsState.SetStrokingColor(PdfColor(0.0, 0.1, 0.6));
    painter.GraphicsState.SetLineWidth(1.8);
    painter.GraphicsState.SetLineCapStyle(PdfLineCapStyle::Round);
    painter.GraphicsState.SetLineJoinStyle(PdfLineJoinStyle::Round);

    PdfPainterPath path;
    path.MoveTo(10, 30);
    path.AddCubicBezierTo(30, 75, 55, 75, 60, 40);
    path.AddCubicBezierTo(65, 5, 90, 5, 100, 35);
    path.AddCubicBezierTo(110, 65, 135, 60, 140, 30);
    path.AddCubicBezierTo(145, 0, 180, 10, 210, 45);
    painter.DrawPath(path, PdfPathDrawMode::Stroke);

    PdfPainterPath underline;
    underline.MoveTo(15, 12);
    underline.AddCubicBezierTo(80, 2, 150, 20, 200, 8);
    painter.DrawPath(underline, PdfPathDrawMode::Stroke);
}

// Variant 1: append a content stream to the page.
void drawOnPageContent(PdfPage &page)
{
    PdfPainter painter;
    painter.SetCanvas(page);
    painter.GraphicsState.ConcatenateTransformationMatrix(Matrix::CreateTranslation({kX, kY}));
    drawSignature(painter);
    painter.FinishDrawing();
}

// Variant 2: a Stamp annotation whose normal appearance is a form XObject.
void drawAsAnnotation(PdfMemDocument &doc, PdfPage &page)
{
    auto form = doc.CreateXObjectForm(Rect(0, 0, kWidth, kHeight));
    PdfPainter painter;
    painter.SetCanvas(*form);
    drawSignature(painter);
    painter.FinishDrawing();

    auto &annotation =
        page.GetAnnotations().CreateAnnot<PdfAnnotationStamp>(Rect(kX, kY, kWidth, kHeight));
    annotation.SetFlags(PdfAnnotationFlags::Print | PdfAnnotationFlags::Locked);
    annotation.SetAppearanceStream(*form);
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 3) {
        std::cerr << "usage: draw <content|annotation> <file> [--no-metadata-update] "
                     "[--no-collect-garbage]\n";
        return 2;
    }
    const std::string_view variant = argv[1];
    const std::string_view file = argv[2];
    auto options = PdfSaveOptions::None;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-metadata-update") == 0) {
            options |= PdfSaveOptions::NoMetadataUpdate;
        } else if (std::strcmp(argv[i], "--no-collect-garbage") == 0) {
            options |= PdfSaveOptions::NoCollectGarbage;
        } else {
            std::cerr << "unknown option: " << argv[i] << '\n';
            return 2;
        }
    }

    try {
        PdfMemDocument doc;
        doc.Load(file);
        auto &page = doc.GetPages().GetPageAt(0);
        if (variant == "content") {
            drawOnPageContent(page);
        } else if (variant == "annotation") {
            drawAsAnnotation(doc, page);
        } else {
            std::cerr << "unknown variant: " << variant << '\n';
            return 2;
        }
        doc.SaveUpdate(file, options);
    } catch (const PdfError &error) {
        std::cerr << "PoDoFo error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
