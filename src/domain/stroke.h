#pragma once

#include "domain/point.h"

#include <cstdint>
#include <vector>

namespace misign::domain {

// How a stroke was drawn. The stylus draws while it touches the surface, with
// real pressure. The mouse and the touchpad draw as a toggle: a click starts
// the stroke, the pointer draws as it moves with no button held, and the next
// click ends it; their pressure is constant.
enum class InputMode : std::uint8_t {
    Stylus,
    Touchpad,
    Mouse,
};

// The points of one stroke, in the order they were drawn: between pen down and
// pen up for the stylus, between the two clicks of the toggle otherwise.
class Stroke {
public:
    explicit Stroke(InputMode inputMode) noexcept : m_inputMode(inputMode) {}

    void addPoint(const Point &point);

    [[nodiscard]] InputMode inputMode() const noexcept { return m_inputMode; }
    [[nodiscard]] const std::vector<Point> &points() const noexcept { return m_points; }
    [[nodiscard]] bool isEmpty() const noexcept { return m_points.empty(); }

    friend bool operator==(const Stroke &, const Stroke &) = default;

private:
    InputMode m_inputMode;
    std::vector<Point> m_points;
};

} // namespace misign::domain
