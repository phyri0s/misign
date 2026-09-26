#include "domain/ink.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace misign::domain {

namespace {

double distance(ScreenPoint a, ScreenPoint b) noexcept
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

ScreenPoint lerp(ScreenPoint a, ScreenPoint b, double t) noexcept
{
    return {a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t)};
}

ScreenPoint position(const Point &p) noexcept
{
    return {p.x(), p.y()};
}

// The part of `curve` between parameters t0 and t1, by de Casteljau.
CubicBezier subCurve(const CubicBezier &curve, double t0, double t1) noexcept
{
    // Right part from t0: split at t0, keep [t0, 1].
    const ScreenPoint a = lerp(curve.start, curve.control1, t0);
    const ScreenPoint b = lerp(curve.control1, curve.control2, t0);
    const ScreenPoint c = lerp(curve.control2, curve.end, t0);
    const ScreenPoint ab = lerp(a, b, t0);
    const ScreenPoint bc = lerp(b, c, t0);
    const ScreenPoint right0 = lerp(ab, bc, t0);
    // Then split [t0, 1] at the matching parameter and keep the left part.
    const double t = t0 < 1.0 ? (t1 - t0) / (1.0 - t0) : 0.0;
    const ScreenPoint p = lerp(right0, bc, t);
    const ScreenPoint q = lerp(bc, c, t);
    const ScreenPoint r = lerp(c, curve.end, t);
    const ScreenPoint pq = lerp(p, q, t);
    const ScreenPoint qr = lerp(q, r, t);
    return {right0, p, pq, lerp(pq, qr, t), curve.firstPoint, curve.lastPoint};
}

// Rough arc length: between the chord and the control polygon.
double approximateLength(const CubicBezier &c) noexcept
{
    const double polygon = distance(c.start, c.control1) + distance(c.control1, c.control2) +
                           distance(c.control2, c.end);
    return (polygon + distance(c.start, c.end)) / 2.0;
}

// The width at fraction `f` of a segment, interpolated between the widths of
// its input points placed by chord length (the same parameterization the fit
// starts from).
double widthAt(const CubicBezier &segment, const Stroke &stroke, const std::vector<double> &widths,
               double f)
{
    const auto &points = stroke.points();
    const std::size_t first = segment.firstPoint;
    const std::size_t last = segment.lastPoint;
    double total = 0.0;
    for (std::size_t i = first + 1; i <= last; ++i) {
        total += distance(position(points[i - 1]), position(points[i]));
    }
    if (total <= 0.0) {
        return widths[first];
    }
    double travelled = 0.0;
    for (std::size_t i = first + 1; i <= last; ++i) {
        const double step = distance(position(points[i - 1]), position(points[i]));
        if (step > 0.0 && travelled + step >= f * total) {
            const double t = ((f * total) - travelled) / step;
            return widths[i - 1] + ((widths[i] - widths[i - 1]) * t);
        }
        travelled += step;
    }
    return widths[last];
}

} // namespace

std::vector<double> strokeWidths(const Stroke &stroke, const InkOptions &options)
{
    const auto &points = stroke.points();
    std::vector<double> widths;
    widths.reserve(points.size());
    const double range = options.maxWidth - options.minWidth;
    const bool stylus = stroke.inputMode() == InputMode::Stylus;
    double speed = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (!stylus && i > 0) {
            // Samples often arrive in bursts with the same timestamp (PHY-97):
            // keep the previous speed rather than divide by 0.
            const double ms = std::chrono::duration<double, std::milli>(points[i].timestamp() -
                                                                        points[i - 1].timestamp())
                                  .count();
            if (ms > 0.0) {
                const double current = distance(position(points[i - 1]), position(points[i])) / ms;
                speed =
                    (options.speedSmoothing * current) + ((1.0 - options.speedSmoothing) * speed);
            }
        }
        const double raw = stylus ? options.minWidth + (range * points[i].pressure())
                                  : std::max(options.maxWidth / (1.0 + speed), options.minWidth);
        const double width = widths.empty() ? raw
                                            : (options.widthSmoothing * raw) +
                                                  ((1.0 - options.widthSmoothing) * widths.back());
        widths.push_back(std::clamp(width, options.minWidth, options.maxWidth));
    }
    return widths;
}

std::vector<InkPiece> inkPieces(const std::vector<CubicBezier> &chain, const Stroke &stroke,
                                const std::vector<double> &widths, const InkOptions &options)
{
    std::vector<InkPiece> pieces;
    for (const CubicBezier &segment : chain) {
        const double length = approximateLength(segment);
        const auto count =
            static_cast<std::size_t>(std::max(1.0, std::ceil(length / options.maxPieceLength)));
        for (std::size_t k = 0; k < count; ++k) {
            const double t0 = static_cast<double>(k) / static_cast<double>(count);
            const double t1 = static_cast<double>(k + 1) / static_cast<double>(count);
            InkPiece piece{subCurve(segment, t0, t1),
                           widthAt(segment, stroke, widths, (t0 + t1) / 2.0)};
            // Exact joins: the split points come from the parent curve.
            if (!pieces.empty() && k > 0) {
                piece.curve.start = pieces.back().curve.end;
            }
            pieces.push_back(piece);
        }
    }
    return pieces;
}

} // namespace misign::domain
