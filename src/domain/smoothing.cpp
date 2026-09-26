#include "domain/smoothing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <tuple>
#include <utility>

namespace misign::domain {

namespace {

// Minimal 2D vector arithmetic on drawing-surface points.
ScreenPoint operator+(ScreenPoint a, ScreenPoint b) noexcept
{
    return {a.x + b.x, a.y + b.y};
}

ScreenPoint operator-(ScreenPoint a, ScreenPoint b) noexcept
{
    return {a.x - b.x, a.y - b.y};
}

ScreenPoint operator*(ScreenPoint a, double k) noexcept
{
    return {a.x * k, a.y * k};
}

double dot(ScreenPoint a, ScreenPoint b) noexcept
{
    return (a.x * b.x) + (a.y * b.y);
}

double length(ScreenPoint a) noexcept
{
    return std::hypot(a.x, a.y);
}

ScreenPoint normalized(ScreenPoint a) noexcept
{
    const double l = length(a);
    return l > 0.0 ? a * (1.0 / l) : ScreenPoint{0.0, 0.0};
}

// Bernstein basis of degree 3.
std::array<double, 4> bernstein(double t) noexcept
{
    const double s = 1.0 - t;
    return {s * s * s, 3.0 * s * s * t, 3.0 * s * t * t, t * t * t};
}

// A point kept for fitting, with its index in Stroke::points().
struct Sample {
    ScreenPoint position;
    std::size_t index;
};

class Fitter {
public:
    Fitter(std::span<const Sample> samples, double tolerance)
        : m_samples(samples), m_squaredTolerance(tolerance * tolerance)
    {
    }

    std::vector<CubicBezier> fit()
    {
        const std::size_t last = m_samples.size() - 1;
        const ScreenPoint startTangent = normalized(position(1) - position(0));
        const ScreenPoint endTangent = normalized(position(last - 1) - position(last));
        // Depth first with an explicit stack (no recursion): a range that does
        // not fit is replaced by its two halves, the left one on top, so the
        // segments come out in stroke order.
        std::vector<Range> pending{{0, last, startTangent, endTangent}};
        while (!pending.empty()) {
            const Range range = pending.back();
            pending.pop_back();
            if (const auto halves = fitRange(range)) {
                pending.push_back(halves->second);
                pending.push_back(halves->first);
            }
        }
        return std::move(m_segments);
    }

private:
    // Newton-Raphson reparameterization is worth trying while the error is
    // within this factor of the tolerance; beyond it, split at once.
    static constexpr double kIterationErrorFactor = 4.0;
    static constexpr int kMaxIterations = 4;

    // Samples first to last, fitted with these end tangents.
    struct Range {
        std::size_t first;
        std::size_t last;
        ScreenPoint startTangent;
        ScreenPoint endTangent;
    };

    [[nodiscard]] ScreenPoint position(std::size_t i) const { return m_samples[i].position; }

    [[nodiscard]] CubicBezier segment(std::size_t first, std::size_t last, ScreenPoint control1,
                                      ScreenPoint control2) const
    {
        return {position(first),      control1, control2, position(last), m_samples[first].index,
                m_samples[last].index};
    }

    // Fits one segment to the range and returns nothing, or returns the two
    // halves to fit instead.
    std::optional<std::pair<Range, Range>> fitRange(const Range &range)
    {
        const auto [first, last, startTangent, endTangent] = range;
        if (last - first == 1) {
            // Two points: a straight segment along the given tangents.
            const double third = length(position(last) - position(first)) / 3.0;
            m_segments.push_back(segment(first, last, position(first) + (startTangent * third),
                                         position(last) + (endTangent * third)));
            return std::nullopt;
        }

        std::vector<double> u = chordLengthParameters(first, last);
        CubicBezier curve = generate(first, last, u, startTangent, endTangent);
        auto [error, split] = maxError(first, last, curve, u);
        if (error < m_squaredTolerance) {
            m_segments.push_back(curve);
            return std::nullopt;
        }
        if (error < m_squaredTolerance * kIterationErrorFactor * kIterationErrorFactor) {
            for (int i = 0; i < kMaxIterations; ++i) {
                u = reparameterize(first, last, u, curve);
                curve = generate(first, last, u, startTangent, endTangent);
                std::tie(error, split) = maxError(first, last, curve, u);
                if (error < m_squaredTolerance) {
                    m_segments.push_back(curve);
                    return std::nullopt;
                }
            }
        }

        // Split at the worst point, with a shared tangent so the chain stays smooth.
        const ScreenPoint centre = normalized(position(split - 1) - position(split + 1));
        return std::pair{Range{first, split, startTangent, centre},
                         Range{split, last, centre * -1.0, endTangent}};
    }

    [[nodiscard]] std::vector<double> chordLengthParameters(std::size_t first,
                                                            std::size_t last) const
    {
        std::vector<double> u(last - first + 1, 0.0);
        for (std::size_t i = first + 1; i <= last; ++i) {
            u[i - first] = u[i - first - 1] + length(position(i) - position(i - 1));
        }
        const double total = u.back();
        for (double &value : u) {
            value /= total;
        }
        return u;
    }

    // Least-squares control points for fixed end tangents (Schneider's
    // GenerateBezier), falling back to a third of the chord when degenerate.
    [[nodiscard]] CubicBezier generate(std::size_t first, std::size_t last,
                                       const std::vector<double> &u, ScreenPoint startTangent,
                                       ScreenPoint endTangent) const
    {
        const ScreenPoint p0 = position(first);
        const ScreenPoint p3 = position(last);
        std::array<std::array<double, 2>, 2> c{};
        std::array<double, 2> x{};
        for (std::size_t i = first; i <= last; ++i) {
            const auto b = bernstein(u[i - first]);
            const ScreenPoint a1 = startTangent * b[1];
            const ScreenPoint a2 = endTangent * b[2];
            c[0][0] += dot(a1, a1);
            c[0][1] += dot(a1, a2);
            c[1][1] += dot(a2, a2);
            const ScreenPoint tmp = position(i) - ((p0 * (b[0] + b[1])) + (p3 * (b[2] + b[3])));
            x[0] += dot(a1, tmp);
            x[1] += dot(a2, tmp);
        }
        c[1][0] = c[0][1];

        const double det = (c[0][0] * c[1][1]) - (c[1][0] * c[0][1]);
        double alpha1 = 0.0;
        double alpha2 = 0.0;
        if (std::abs(det) > 1e-12) {
            alpha1 = ((x[0] * c[1][1]) - (x[1] * c[0][1])) / det;
            alpha2 = ((c[0][0] * x[1]) - (c[1][0] * x[0])) / det;
        }
        const double chord = length(p3 - p0);
        const double epsilon = 1e-6 * chord;
        if (alpha1 < epsilon || alpha2 < epsilon) {
            alpha1 = chord / 3.0;
            alpha2 = alpha1;
        }
        return segment(first, last, p0 + (startTangent * alpha1), p3 + (endTangent * alpha2));
    }

    // Largest squared distance between an input point and the curve at its
    // parameter, and where it is.
    [[nodiscard]] std::pair<double, std::size_t> maxError(std::size_t first, std::size_t last,
                                                          const CubicBezier &curve,
                                                          const std::vector<double> &u) const
    {
        double worst = 0.0;
        std::size_t split = first + ((last - first + 1) / 2);
        for (std::size_t i = first + 1; i < last; ++i) {
            const ScreenPoint d = curve.at(u[i - first]) - position(i);
            const double error = dot(d, d);
            if (error >= worst) {
                worst = error;
                split = i;
            }
        }
        return {worst, split};
    }

    // One Newton-Raphson step per parameter, towards the closest curve point.
    [[nodiscard]] std::vector<double> reparameterize(std::size_t first, std::size_t last,
                                                     const std::vector<double> &u,
                                                     const CubicBezier &curve) const
    {
        const ScreenPoint p0 = curve.start;
        const ScreenPoint p1 = curve.control1;
        const ScreenPoint p2 = curve.control2;
        const ScreenPoint p3 = curve.end;
        // First and second derivative control points.
        const std::array<ScreenPoint, 3> d1{(p1 - p0) * 3.0, (p2 - p1) * 3.0, (p3 - p2) * 3.0};
        const std::array<ScreenPoint, 2> d2{(d1[1] - d1[0]) * 2.0, (d1[2] - d1[1]) * 2.0};

        std::vector<double> result(u);
        for (std::size_t i = first; i <= last; ++i) {
            const double t = u[i - first];
            const double s = 1.0 - t;
            const ScreenPoint q = curve.at(t);
            const ScreenPoint q1 = (d1[0] * (s * s)) + (d1[1] * (2.0 * s * t)) + (d1[2] * (t * t));
            const ScreenPoint q2 = (d2[0] * s) + (d2[1] * t);
            const ScreenPoint diff = q - position(i);
            const double denominator = dot(q1, q1) + dot(diff, q2);
            if (std::abs(denominator) > 1e-12) {
                result[i - first] = std::clamp(t - (dot(diff, q1) / denominator), 0.0, 1.0);
            }
        }
        return result;
    }

    std::span<const Sample> m_samples;
    double m_squaredTolerance;
    std::vector<CubicBezier> m_segments;
};

} // namespace

ScreenPoint CubicBezier::at(double t) const noexcept
{
    const auto b = bernstein(t);
    return (start * b[0]) + (control1 * b[1]) + (control2 * b[2]) + (end * b[3]);
}

std::vector<CubicBezier> smooth(const Stroke &stroke, const SmoothingOptions &options)
{
    const std::vector<Point> &points = stroke.points();
    if (points.empty()) {
        return {};
    }

    // Drop points too close to the previous kept one, but always end on the
    // stroke's last point.
    std::vector<Sample> samples;
    samples.push_back({{points.front().x(), points.front().y()}, 0});
    for (std::size_t i = 1; i < points.size(); ++i) {
        const ScreenPoint p{points[i].x(), points[i].y()};
        if (length(p - samples.back().position) >= options.minSpacing) {
            samples.push_back({p, i});
        } else if (i == points.size() - 1 && samples.size() > 1) {
            samples.back() = {p, i};
        }
    }

    if (samples.size() == 1) {
        const ScreenPoint p = samples.front().position;
        return {CubicBezier{p, p, p, p, 0, points.size() - 1}};
    }
    return Fitter(samples, options.tolerance).fit();
}

} // namespace misign::domain
