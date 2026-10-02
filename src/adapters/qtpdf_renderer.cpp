#include "adapters/qtpdf_renderer.h"

#include <QImage>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QSize>
#include <QString>

#include <cmath>
#include <cstring>

namespace misign::adapters {

namespace {

using application::DocumentError;
using application::PageImage;
using application::PageSize;

// Qt PDF does not tell a file that is not a PDF from a damaged one.
std::optional<DocumentError> errorOf(QPdfDocument::Error error)
{
    switch (error) {
    case QPdfDocument::Error::None:
        return std::nullopt;
    case QPdfDocument::Error::FileNotFound:
        return DocumentError::Unreadable;
    case QPdfDocument::Error::IncorrectPassword:
    case QPdfDocument::Error::UnsupportedSecurityScheme:
        return DocumentError::PasswordProtected;
    default:
        return DocumentError::Damaged;
    }
}

PageImage toPageImage(const QImage &image)
{
    PageImage result;
    result.width = image.width();
    result.height = image.height();
    const auto rowLength = static_cast<std::size_t>(image.width());
    result.pixels.resize(rowLength * static_cast<std::size_t>(image.height()));
    for (int row = 0; row < image.height(); ++row) {
        // Row by row: QImage pads its scan lines.
        std::memcpy(&result.pixels[static_cast<std::size_t>(row) * rowLength],
                    image.constScanLine(row), rowLength * sizeof(std::uint32_t));
    }
    return result;
}

} // namespace

QtPdfRenderer::QtPdfRenderer() : m_document(std::make_unique<QPdfDocument>()) {}

QtPdfRenderer::~QtPdfRenderer() = default;

std::optional<DocumentError> QtPdfRenderer::open(const std::filesystem::path &file)
{
    m_document->close();
    const std::optional<DocumentError> error =
        errorOf(m_document->load(QString::fromStdU16String(file.u16string())));
    if (error) {
        m_document->close();
    }
    return error;
}

void QtPdfRenderer::close()
{
    m_document->close();
}

int QtPdfRenderer::pageCount() const
{
    return m_document->pageCount();
}

PageSize QtPdfRenderer::pageSize(int page) const
{
    if (page < 0 || page >= m_document->pageCount()) {
        return {};
    }
    const QSizeF size = m_document->pagePointSize(page);
    return {size.width(), size.height()};
}

PageImage QtPdfRenderer::render(int page, double scale)
{
    const PageSize size = pageSize(page);
    if (size.isEmpty() || !(scale > 0.0)) {
        return {};
    }
    const QSize pixels(static_cast<int>(std::lround(size.width * scale)),
                       static_cast<int>(std::lround(size.height * scale)));
    if (pixels.isEmpty()) {
        return {};
    }
    QPdfDocumentRenderOptions options;
    options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
    const QImage rendered = m_document->render(page, pixels, options);
    if (rendered.isNull()) {
        return {};
    }
    // Qt PDF leaves the paper transparent.
    QImage opaque(rendered.size(), QImage::Format_RGB32);
    opaque.fill(Qt::white);
    QPainter painter(&opaque);
    painter.drawImage(0, 0, rendered);
    painter.end();
    return toPageImage(opaque);
}

} // namespace misign::adapters
