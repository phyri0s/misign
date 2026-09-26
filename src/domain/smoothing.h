#pragma once

#include "domain/page_geometry.h"
#include "domain/stroke.h"

#include <cstddef>
#include <vector>

namespace misign::domain {

// A cubic Bézier segment on the drawing surface (same coordinates as Point).
// Being affine-invariant, it maps to the page by mapping its four points.
struct CubicBezier {
    ScreenPoint start;
    ScreenPoint control1;
    ScreenPoint control2;
    ScreenPoint end;
    // The input points this segment was fitted to, as indices into
    // Stroke::points(): the segment starts at the first and ends at the last,
    // so pressures and timestamps along it can be read from them.
    std::size_t firstPoint;
    std::size_t lastPoint;

    // The point at parameter t in [0, 1].
    [[nodiscard]] ScreenPoint at(double t) const noexcept;

    friend bool operator==(const CubicBezier &, const CubicBezier &) = default;
};

struct SmoothingOptions {
    // Input points closer than this to the previous kept point are dropped:
    // duplicates and sub-pixel jitter (drawing-surface units).
    double minSpacing = 0.5;
    // Every kept input point lies within this distance of the curve. One
    // logical pixel absorbs the whole-pixel stair steps of the input (PHY-97).
    double tolerance = 1.0;
};

// Turns a stroke into a chain of cubic Bézier segments, fitted to its points
// with a bounded error (Schneider, "An Algorithm for Automatically Fitting
// Digitized Curves", Graphics Gems, 1990). The chain starts at the first point
// and ends at the last, and is continuous in position and tangent direction.
// Fewer segments than points: the content stream stays small.
//
// A one-point stroke (or one whose points all fall within minSpacing) gives a
// single segment collapsed on that point, a dot; an empty stroke gives none.
//
// The same path is used for display and for export, so what is saved is what
// was seen.
[[nodiscard]] std::vector<CubicBezier> smooth(const Stroke &stroke,
                                              const SmoothingOptions &options = {});

} // namespace misign::domain
