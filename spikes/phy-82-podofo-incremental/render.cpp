// PHY-82 spike: render page 1 with Qt PDF, with and without annotations, and
// count the pixels of the drawing's blue ink. Throwaway code, see README.md.
//
// usage: render <file.pdf> <out-prefix>
// prints: "<pixels without annotations> <pixels with annotations>"

#include <QGuiApplication>
#include <QImage>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>

#include <iostream>

namespace {

// The spike draws in RGB (0, 0.1, 0.6): count clearly blue pixels.
int countInk(const QImage &image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = image.pixelColor(x, y);
            if (color.blue() > 100 && color.red() < 80 && color.green() < 80) {
                ++count;
            }
        }
    }
    return count;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList args = QGuiApplication::arguments();
    if (args.size() < 3) {
        std::cerr << "usage: render <file.pdf> <out-prefix>\n";
        return 2;
    }

    QPdfDocument document;
    if (document.load(args.at(1)) != QPdfDocument::Error::None) {
        std::cerr << "cannot load " << args.at(1).toStdString() << '\n';
        return 1;
    }
    const QSize size = document.pagePointSize(0).toSize() * 2; // 144 dpi

    QPdfDocumentRenderOptions withoutAnnotations;
    QPdfDocumentRenderOptions withAnnotations;
    withAnnotations.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);

    const QImage plain = document.render(0, size, withoutAnnotations);
    const QImage annotated = document.render(0, size, withAnnotations);
    plain.save(args.at(2) + QStringLiteral("-plain.png"));
    annotated.save(args.at(2) + QStringLiteral("-annotations.png"));

    std::cout << countInk(plain) << ' ' << countInk(annotated) << '\n';
    return 0;
}
