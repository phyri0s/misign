#include "domain/page_geometry.h"

#include <algorithm>
#include <cassert>

namespace misign::domain {

namespace {

PdfRect normalized(const PdfRect &rect) noexcept
{
    return PdfRect::fromCorners({rect.left, rect.bottom}, {rect.right, rect.top});
}

// US Letter, which PDFium uses for an empty MediaBox.
constexpr PdfRect kDefaultMediaBox{0.0, 0.0, 612.0, 792.0};

// The visible area as PDFium computes it (CPDF_Page): the PDF specification
// clips the CropBox to the MediaBox, and PDFium adds the fallbacks for empty
// boxes. Checked against Qt PDF in the PHY-83 spike.
PdfRect visibleArea(const PdfRect &mediaBox, const std::optional<PdfRect> &cropBox) noexcept
{
    PdfRect media = normalized(mediaBox);
    if (media.isEmpty()) {
        media = kDefaultMediaBox;
    }
    if (!cropBox || normalized(*cropBox).isEmpty()) {
        return media;
    }
    const PdfRect crop = normalized(*cropBox);
    const PdfRect clipped{std::max(media.left, crop.left), std::max(media.bottom, crop.bottom),
                          std::min(media.right, crop.right), std::min(media.top, crop.top)};
    if (clipped.isEmpty()) {
        return {0.0, 0.0, 0.0, 0.0};
    }
    return clipped;
}

} // namespace

PdfRect PdfRect::fromCorners(PdfPoint first, PdfPoint second) noexcept
{
    return {std::min(first.x, second.x), std::min(first.y, second.y), std::max(first.x, second.x),
            std::max(first.y, second.y)};
}

ScreenRect ScreenRect::fromCorners(ScreenPoint first, ScreenPoint second) noexcept
{
    return {std::min(first.x, second.x), std::min(first.y, second.y), std::max(first.x, second.x),
            std::max(first.y, second.y)};
}

PdfPoint AffineMatrix::map(PdfPoint point) const noexcept
{
    return {(a * point.x) + (c * point.y) + e, (b * point.x) + (d * point.y) + f};
}

Rotation rotationFromDegrees(int degrees) noexcept
{
    // C++ integer division truncates towards zero, like PDFium's `rotate / 90 % 4`.
    int quarterTurns = degrees / 90 % 4;
    if (quarterTurns < 0) {
        quarterTurns += 4;
    }
    switch (quarterTurns) {
    case 1:
        return Rotation::Clockwise90;
    case 2:
        return Rotation::Clockwise180;
    case 3:
        return Rotation::Clockwise270;
    default:
        return Rotation::None;
    }
}

PageGeometry::PageGeometry(PdfRect mediaBox, std::optional<PdfRect> cropBox,
                           Rotation rotation) noexcept
    : m_visibleBox(visibleArea(mediaBox, cropBox)), m_rotation(rotation)
{
}

bool PageGeometry::isSideways() const noexcept
{
    return m_rotation == Rotation::Clockwise90 || m_rotation == Rotation::Clockwise270;
}

double PageGeometry::displayedWidth() const noexcept
{
    return isSideways() ? m_visibleBox.height() : m_visibleBox.width();
}

double PageGeometry::displayedHeight() const noexcept
{
    return isSideways() ? m_visibleBox.width() : m_visibleBox.height();
}

AffineMatrix PageGeometry::displayedToUser() const noexcept
{
    // A clockwise /Rotate turns the visible box on screen; each case maps the
    // displayed bottom-left corner to the user-space corner it shows.
    const PdfRect &box = m_visibleBox;
    switch (m_rotation) {
    case Rotation::Clockwise90:
        return {0.0, 1.0, -1.0, 0.0, box.right, box.bottom};
    case Rotation::Clockwise180:
        return {-1.0, 0.0, 0.0, -1.0, box.right, box.top};
    case Rotation::Clockwise270:
        return {0.0, -1.0, 1.0, 0.0, box.left, box.top};
    case Rotation::None:
        break;
    }
    return {1.0, 0.0, 0.0, 1.0, box.left, box.bottom};
}

AffineMatrix PageGeometry::userToDisplayed() const noexcept
{
    // The linear part is a rotation, so its inverse is its transpose.
    const AffineMatrix m = displayedToUser();
    return {m.a, m.c, m.b, m.d, -((m.a * m.e) + (m.b * m.f)), -((m.c * m.e) + (m.d * m.f))};
}

PdfPoint PageGeometry::toUser(ScreenPoint point, double scale) const noexcept
{
    assert(scale > 0.0 && hasVisibleArea());
    const PdfPoint displayed{point.x / scale, displayedHeight() - (point.y / scale)};
    return displayedToUser().map(displayed);
}

PdfRect PageGeometry::toUser(const ScreenRect &rect, double scale) const noexcept
{
    return PdfRect::fromCorners(toUser(ScreenPoint{rect.left, rect.top}, scale),
                                toUser(ScreenPoint{rect.right, rect.bottom}, scale));
}

ScreenPoint PageGeometry::toScreen(PdfPoint point, double scale) const noexcept
{
    assert(scale > 0.0 && hasVisibleArea());
    const PdfPoint displayed = userToDisplayed().map(point);
    return {displayed.x * scale, (displayedHeight() - displayed.y) * scale};
}

ScreenRect PageGeometry::toScreen(const PdfRect &rect, double scale) const noexcept
{
    return ScreenRect::fromCorners(toScreen(PdfPoint{rect.left, rect.bottom}, scale),
                                   toScreen(PdfPoint{rect.right, rect.top}, scale));
}

} // namespace misign::domain
