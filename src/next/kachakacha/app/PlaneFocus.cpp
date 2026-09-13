#include "kachakacha/app/PlaneFocus.h"

#include "kachakacha/geometry/CurveIntersection.h"

namespace kachakacha::v2::app {

bool CurveLiesOnPlane(const geometry::CurveSegment& segment,
    const modeling::WorkPlaneFrame& plane, double toleranceMm)
{
    // 判定そのものは geometry に1つだけ置き、スナップと同じ答えを出す。
    return geometry::CurveLiesInPlane(segment, plane.origin, plane.normal, toleranceMm);
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
