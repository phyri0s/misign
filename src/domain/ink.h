#pragma once

#include "domain/smoothing.h"
#include "domain/stroke.h"

#include <vector>

namespace misign::domain {

// Stroke width rules. Widths are in drawing-surface units, like the points:
// once the signature is fitted into its box (Signature::fitInto), they scale
// with it, so a signature keeps its proportions in any box (ADR 0010).
struct InkOptions {
    double minWidth = 1.0;
    double maxWidth = 4.5;
    // Mouse and touchpad: the width is maxWidth / (1 + speed), speed in
    // drawing units per millisecond, never below minWidth.
    // Exponential smoothing of the speed, weight of the newest value.
    double speedSmoothing = 0.7;
    // Exponential smoothing of the width along the stroke, weight of the
    // newest value, so it does not flicker from one sample to the next.
    double widthSmoothing = 0.4;
    // Pieces written per smoothed segment are at most this long.
    double maxPieceLength = 4.0;
};

// The width at each point of the stroke, in [minWidth, maxWidth]. The stylus
// follows its pressure: a harder press draws wider. The mouse and the
// touchpad, whose pressure is constant, follow their speed, computed from the
// timestamps: a slower stroke draws wider.
[[nodiscard]] std::vector<double> strokeWidths(const Stroke &stroke,
                                               const InkOptions &options = {});

// A piece of a smoothed stroke drawn at one width, with round caps. PDF has no
// variable-width stroke: a stroke is written as a chain of such pieces, each
// a part of the smoothed curve (so the shape is exactly the smoothed one) with
// its own line width. Round caps make consecutive pieces overlap without a
// visible joint; the ink is opaque, so overlaps do not darken. Unlike an
// outline around the curve, a piece can never self-intersect on a tight turn.
struct InkPiece {
    CubicBezier curve;
    double width;
};

// Splits the smoothed chain into pieces of at most maxPieceLength, each with
// the width interpolated from `widths` (one per stroke point, as returned by
// strokeWidths) at its middle. Consecutive pieces join end to start. A dot
// gives one piece collapsed on its point.
[[nodiscard]] std::vector<InkPiece> inkPieces(const std::vector<CubicBezier> &chain,
                                              const Stroke &stroke,
                                              const std::vector<double> &widths,
                                              const InkOptions &options = {});

} // namespace misign::domain
