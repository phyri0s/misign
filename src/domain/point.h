#pragma once

#include <chrono>

namespace misign::domain {

// A pen sample on the drawing surface: logical pixels, origin at the top left,
// y down, as the drawing pad receives it.
//
// The timestamp is taken on reception with a steady clock, in microseconds
// since an arbitrary origin: only differences within a session are meaningful.
// samples arrive about every 5 ms, and the event timestamps Qt gets from
// Windows move in 15 ms steps (PHY-97), both too coarse for a speed.
class Point {
public:
    // Pressure is clamped to [0, 1]; constant for the mouse and the touchpad.
    Point(double x, double y, double pressure, std::chrono::microseconds timestamp) noexcept;

    [[nodiscard]] double x() const noexcept { return m_x; }
    [[nodiscard]] double y() const noexcept { return m_y; }
    [[nodiscard]] double pressure() const noexcept { return m_pressure; }
    [[nodiscard]] std::chrono::microseconds timestamp() const noexcept { return m_timestamp; }

    friend bool operator==(const Point &, const Point &) = default;

private:
    double m_x;
    double m_y;
    double m_pressure;
    std::chrono::microseconds m_timestamp;
};

} // namespace misign::domain
