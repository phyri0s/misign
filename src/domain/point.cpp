#include "domain/point.h"

#include <algorithm>

namespace misign::domain {

Point::Point(double x, double y, double pressure, std::chrono::microseconds timestamp) noexcept
    : m_x(x), m_y(y), m_pressure(std::clamp(pressure, 0.0, 1.0)), m_timestamp(timestamp)
{
}

} // namespace misign::domain
