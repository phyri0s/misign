#include "domain/stroke.h"

namespace misign::domain {

void Stroke::addPoint(const Point &point)
{
    m_points.push_back(point);
}

} // namespace misign::domain
