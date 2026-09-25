#pragma once

#include <cstdint>
#include <optional>

namespace misign::domain {

// A point in PDF user space: points, origin at the bottom left, y up.
struct PdfPoint {
    double x;
    double y;

    friend bool operator==(const PdfPoint &, const PdfPoint &) = default;
};

// A rectangle in PDF user space, normalized: left <= right and bottom <= top.
struct PdfRect {
    double left;
    double bottom;
    double right;
    double top;

    // The rectangle spanned by two opposite corners, in any order.
    [[nodiscard]] static PdfRect fromCorners(PdfPoint first, PdfPoint second) noexcept;

    [[nodiscard]] double width() const noexcept { return right - left; }
    [[nodiscard]] double height() const noexcept { return top - bottom; }

    friend bool operator==(const PdfRect &, const PdfRect &) = default;
};

// A point on the rendered page image: pixels, origin at the top left, y down.
struct ScreenPoint {
    double x;
    double y;

    friend bool operator==(const ScreenPoint &, const ScreenPoint &) = default;
};

// A rectangle on the rendered page image, normalized: left <= right and top <= bottom.
struct ScreenRect {
    double left;
    double top;
    double right;
    double bottom;

    // The rectangle spanned by two opposite corners, in any order.
    [[nodiscard]] static ScreenRect fromCorners(ScreenPoint first, ScreenPoint second) noexcept;

    friend bool operator==(const ScreenRect &, const ScreenRect &) = default;
};

// An affine transformation, as the operands of the PDF `cm` operator:
// x' = a x + c y + e, y' = b x + d y + f.
struct AffineMatrix {
    double a;
    double b;
    double c;
    double d;
    double e;
    double f;

    [[nodiscard]] PdfPoint map(PdfPoint point) const noexcept;

    friend bool operator==(const AffineMatrix &, const AffineMatrix &) = default;
};

// Clockwise page rotation, as in the /Rotate page entry.
enum class Rotation : std::uint16_t {
    None = 0,
    Clockwise90 = 90,
    Clockwise180 = 180,
    Clockwise270 = 270,
};

// Reads a /Rotate value the way PDFium (behind Qt PDF) does, so that placement
// matches what is displayed: truncated to a multiple of 90, then brought into
// [0, 360). For example 450 -> 90, -90 -> 270, 135 -> 90.
[[nodiscard]] Rotation rotationFromDegrees(int degrees) noexcept;

// The geometry of a page as a viewer displays it: its visible area (the
// CropBox clipped to the MediaBox) turned clockwise by /Rotate.
//
// "Displayed" coordinates are points with the origin at the bottom left of
// the page as displayed. Screen coordinates are pixels on the rendered page
// image, at `scale` pixels per point, with the origin at its top left.
class PageGeometry {
public:
    // Boxes may be given with their corners in any order. Without a CropBox,
    // or when it does not overlap the MediaBox, the MediaBox is visible.
    PageGeometry(PdfRect mediaBox, std::optional<PdfRect> cropBox, Rotation rotation) noexcept;

    [[nodiscard]] PdfRect visibleBox() const noexcept { return m_visibleBox; }
    [[nodiscard]] Rotation rotation() const noexcept { return m_rotation; }

    // Size of the page as displayed, in points.
    [[nodiscard]] double displayedWidth() const noexcept;
    [[nodiscard]] double displayedHeight() const noexcept;

    // Maps displayed coordinates to user space. Also the matrix that makes
    // content drawn in displayed coordinates appear upright on the page.
    [[nodiscard]] AffineMatrix displayedToUser() const noexcept;
    // The inverse: maps user space to displayed coordinates.
    [[nodiscard]] AffineMatrix userToDisplayed() const noexcept;

    [[nodiscard]] PdfPoint toUser(ScreenPoint point, double scale) const noexcept;
    [[nodiscard]] PdfRect toUser(const ScreenRect &rect, double scale) const noexcept;
    [[nodiscard]] ScreenPoint toScreen(PdfPoint point, double scale) const noexcept;
    [[nodiscard]] ScreenRect toScreen(const PdfRect &rect, double scale) const noexcept;

private:
    // Turned by a quarter turn: the displayed width is the visible height.
    [[nodiscard]] bool isSideways() const noexcept;

    PdfRect m_visibleBox;
    Rotation m_rotation;
};

} // namespace misign::domain
