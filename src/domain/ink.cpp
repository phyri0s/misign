#include "domain/ink.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>

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

// The number of equal steps of t that keeps every part of `c` at most
// `maxLength` long. The derivative of a cubic is a quadratic Bézier whose
// control points are 3 times the legs of the control polygon, so the speed
// along the curve never exceeds 3 times the longest leg. An estimate of the
// whole length would not do: equal steps of t are not equal lengths, and on
// long handles the parts near the ends come out much longer.
std::size_t stepsFor(const CubicBezier &c, double maxLength) noexcept
{
    const double longestLeg =
        std::max({distance(c.start, c.control1), distance(c.control1, c.control2),
                  distance(c.control2, c.end)});
    return static_cast<std::size_t>(std::max(1.0, std::ceil(3.0 * longestLeg / maxLength)));
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

// The smoothed speed at each point of a mouse or touchpad stroke, in drawing
// units per millisecond. Samples often arrive in bursts sharing a timestamp
// (PHY-97): the distance a burst covers is added up and divided by the time to
// the next later timestamp, so it is neither lost nor divided by 0. The
// average starts from the first speed measured, not from 0, and the points
// before it take that speed: a stroke started while moving (toggle mode) does
// not open with a wide blob. With no time elapsed at all (a dot), it is 0.
std::vector<double> pointerSpeeds(const std::vector<Point> &points, double smoothing)
{
    std::vector<double> speeds(points.size(), 0.0);
    std::optional<double> speed;
    std::size_t since = 0; // The first sample of the current burst.
    double travelled = 0.0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        travelled += distance(position(points[i - 1]), position(points[i]));
        const double ms = std::chrono::duration<double, std::milli>(points[i].timestamp() -
                                                                    points[since].timestamp())
                              .count();
        if (ms > 0.0) {
            const double current = travelled / ms;
            if (!speed) {
                std::fill(speeds.begin(), speeds.begin() + static_cast<std::ptrdiff_t>(i), current);
            }
            speed = speed ? (smoothing * current) + ((1.0 - smoothing) * *speed) : current;
            travelled = 0.0;
            since = i;
        }
        speeds[i] = speed.value_or(0.0);
    }
    return speeds;
}

} // namespace

std::vector<double> strokeWidths(const Stroke &stroke, const InkOptions &options)
{
    const auto &points = stroke.points();
    std::vector<double> widths;
    widths.reserve(points.size());
    const double range = options.maxWidth - options.minWidth;
    const bool stylus = stroke.inputMode() == InputMode::Stylus;
    const std::vector<double> speeds =
        stylus ? std::vector<double>{} : pointerSpeeds(points, options.speedSmoothing);
    for (std::size_t i = 0; i < points.size(); ++i) {
        const double raw = stylus
                               ? options.minWidth + (range * points[i].pressure())
                               : std::max(options.maxWidth / (1.0 + speeds[i]), options.minWidth);
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
        const std::size_t count = stepsFor(segment, options.maxPieceLength);
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
