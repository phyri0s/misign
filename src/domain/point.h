#pragma once

#include <chrono>

namespace misign::domain {

// A pen sample on the drawing surface.
class Point {
public:
    // Pressure is clamped to [0, 1].
    Point(double x, double y, double pressure, std::chrono::milliseconds timestamp) noexcept;

    [[nodiscard]] double x() const noexcept { return m_x; }
    [[nodiscard]] double y() const noexcept { return m_y; }
    [[nodiscard]] double pressure() const noexcept { return m_pressure; }
    [[nodiscard]] std::chrono::milliseconds timestamp() const noexcept { return m_timestamp; }

    friend bool operator==(const Point &, const Point &) = default;

private:
    double m_x;
    double m_y;
    double m_pressure;
    std::chrono::milliseconds m_timestamp;
};

} // namespace misign::domain
