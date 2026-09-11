#include "kachakacha/app/PlaneFocus.h"

#include <cmath>

namespace kachakacha::v2::app {

using geometry::Dot;

bool CurveLiesOnPlane(const geometry::CurveSegment& segment,
    const modeling::WorkPlaneFrame& plane, double toleranceMm)
{
    for (const double at : {0.0, 0.5, 1.0}) {
        const geometry::Vector3 point = segment.Evaluate(at);
        if (std::abs(Dot(point - plane.origin, plane.normal)) > toleranceMm) {
            return false;
        }
    }
    return true;
}

bool DimsOffPlaneCurve(bool drawing, bool enabled, bool selected, bool onPlane) noexcept
{
    return drawing && enabled && !selected && !onPlane;
}

bool PickableOffPlaneCurve(bool drawing, bool enabled, bool onPlane) noexcept
{
    // 薄くしているものは掴まない。それ以外は全部掴める。
    return !DimsOffPlaneCurve(drawing, enabled, false, onPlane);
}

} // namespace kachakacha::v2::app
