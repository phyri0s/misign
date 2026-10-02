#include "ui/page_image_provider.h"

#include <QImage>

#include <cstring>

namespace misign::ui {

namespace {

QImage toQImage(const application::PageImage &page)
{
    if (page.isEmpty()) {
        return {};
    }
    QImage image(page.width, page.height, QImage::Format_RGB32);
    const auto rowLength = static_cast<std::size_t>(page.width);
    for (int row = 0; row < page.height; ++row) {
        std::memcpy(image.scanLine(row), &page.pixels[static_cast<std::size_t>(row) * rowLength],
                    rowLength * sizeof(std::uint32_t));
    }
    return image;
}

} // namespace

PageImageProvider::PageImageProvider(application::IPdfRenderer &renderer)
    : QQuickImageProvider(QQuickImageProvider::Image), m_renderer(renderer)
{
}

QImage PageImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    bool isNumber = false;
    const int page = id.section(QLatin1Char('/'), 1).toInt(&isNumber);
    const application::PageSize pageSize =
        isNumber ? m_renderer.pageSize(page) : application::PageSize{};
    if (pageSize.isEmpty()) {
        return {};
    }
    const double scale = requestedSize.width() > 0 ? requestedSize.width() / pageSize.width : 1.0;
    const QImage image = toQImage(m_renderer.render(page, scale));
    if (size != nullptr) {
        *size = image.size();
    }
    return image;
}

} // namespace misign::ui
