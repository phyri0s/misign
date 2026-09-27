#pragma once

#include "domain/ink.h"
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

    // The matrix mapping the signature into `box`: scaled uniformly to the
    // largest size that fits, centred, and turned the right way up, since the
    // drawing surface has y down. A signature with no height or no width (a
    // straight line) is scaled to fit its length and its ink's thickness, and
    // a single dot is only moved to the centre. None for an empty signature or
    // an empty box.
    //
    // `box` is in displayed coordinates (points, origin at the bottom left of
    // the page as displayed, see PageGeometry), not in user space: on a rotated
    // page the signature must be upright as displayed. The content stream
    // applies PageGeometry::displayedToUser(), then this matrix (PHY-83).
    //
    // Widths set inside this transformation are in drawing units and scale
    // with it. The ink reaches `inkMargin` (drawing units, half the widest
    // stroke) beyond the points on every side, so the fit leaves that much
    // room, scaled, inside `box`: the ink is not clipped at its edges. The
    // default matches InkOptions' default widths; pass 0 to fit the points
    // alone.
    [[nodiscard]] std::optional<AffineMatrix>
    fitInto(const PdfRect &box, double inkMargin = InkOptions{}.maxWidth / 2.0) const noexcept;

    friend bool operator==(const Signature &, const Signature &) = default;

private:
    std::vector<Stroke> m_strokes;
};

} // namespace misign::domain
