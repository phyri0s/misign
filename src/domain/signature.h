#pragma once

#include "domain/page_geometry.h"
#include "domain/stroke.h"

#include <optional>
#include <vector>

namespace misign::domain {

// A drawn signature: its strokes, in drawing-surface coordinates (see Point).
// It stays vector data end to end, so it can be drawn once and fitted into any
// signature box.
class Signature {
public:
    // Empty strokes are ignored: they draw nothing.
    void addStroke(Stroke stroke);

    [[nodiscard]] const std::vector<Stroke> &strokes() const noexcept { return m_strokes; }
    [[nodiscard]] bool isEmpty() const noexcept { return m_strokes.empty(); }

    // The smallest rectangle containing every point, none for an empty
    // signature. Stroke widths are not included.
    [[nodiscard]] std::optional<ScreenRect> boundingBox() const noexcept;

    // The matrix mapping the signature into `box`, a rectangle in PDF
    // coordinates (y up): scaled uniformly to the largest size that fits,
    // centred, and turned the right way up, since the drawing surface has y
    // down. A signature with no height or no width (a straight line) is scaled
    // along its other dimension only, and a single dot is only moved to the
    // centre. None for an empty signature.
    //
    // Stroke widths are not part of the fit: leave room for them in `box`.
    [[nodiscard]] std::optional<AffineMatrix> fitInto(const PdfRect &box) const noexcept;

    friend bool operator==(const Signature &, const Signature &) = default;

private:
    std::vector<Stroke> m_strokes;
};

} // namespace misign::domain
