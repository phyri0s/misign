#include "domain/signature.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace misign::domain {

void Signature::addStroke(Stroke stroke)
{
    if (!stroke.isEmpty()) {
        m_strokes.push_back(std::move(stroke));
    }
}

std::optional<ScreenRect> Signature::boundingBox() const noexcept
{
    std::optional<ScreenRect> box;
    for (const Stroke &stroke : m_strokes) {
        for (const Point &point : stroke.points()) {
            if (!box) {
                box = ScreenRect{point.x(), point.y(), point.x(), point.y()};
                continue;
            }
            box->left = std::min(box->left, point.x());
            box->top = std::min(box->top, point.y());
            box->right = std::max(box->right, point.x());
            box->bottom = std::max(box->bottom, point.y());
        }
    }
    return box;
}

std::optional<AffineMatrix> Signature::fitInto(const PdfRect &box, double inkMargin) const noexcept
{
    const std::optional<ScreenRect> bounds = boundingBox();
    if (!bounds || box.isEmpty()) {
        return std::nullopt;
    }
    const double width = bounds->right - bounds->left;
    const double height = bounds->bottom - bounds->top;
    // The ink's extent, which must fit: the points plus the margin on each
    // side. Without a margin, a straight line fits along its length only.
    const double inkWidth = width + (2.0 * inkMargin);
    const double inkHeight = height + (2.0 * inkMargin);
    double scale = 1.0;
    if (width > 0.0 || height > 0.0) {
        scale = std::numeric_limits<double>::infinity();
        if (inkWidth > 0.0) {
            scale = std::min(scale, box.width() / inkWidth);
        }
        if (inkHeight > 0.0) {
            scale = std::min(scale, box.height() / inkHeight);
        }
    }

    // Centre on centre, with y flipped: x' = boxX + (x - x0) s, y' = boxY - (y - y0) s.
    const double x0 = (bounds->left + bounds->right) / 2.0;
    const double y0 = (bounds->top + bounds->bottom) / 2.0;
    const double boxX = (box.left + box.right) / 2.0;
    const double boxY = (box.bottom + box.top) / 2.0;
    return AffineMatrix{scale, 0.0, 0.0, -scale, boxX - (x0 * scale), boxY + (y0 * scale)};
}

} // namespace misign::domain
