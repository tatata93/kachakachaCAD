#include "kachakacha/modeling/DrawingConstraint.h"

#include <cmath>

namespace kachakacha::v2::modeling {

bool ToolUsesAxisConstraint(DrawingTool tool) noexcept
{
    switch (tool) {
    case DrawingTool::Line:
    case DrawingTool::Polyline:
    case DrawingTool::Spline:
    case DrawingTool::Rectangle:
    case DrawingTool::Bezier:
    case DrawingTool::Move:
    case DrawingTool::Copy:
        return true;
    default:
        return false;
    }
}

geometry::Vector3 ApplyAxisConstraint(DrawingTool tool, const WorkPlaneFrame& plane,
    const geometry::Vector3& anchor, const geometry::Vector3& point)
{
    if (!ToolUsesAxisConstraint(tool)) {
        return point;
    }
    const double anchorU = plane.CoordinateU(anchor);
    const double anchorV = plane.CoordinateV(anchor);
    double u = plane.CoordinateU(point);
    double v = plane.CoordinateV(point);
    const double deltaU = u - anchorU;
    const double deltaV = v - anchorV;
    if (tool == DrawingTool::Rectangle) {
        // 正方形にする。長いほうの辺に合わせる。短いほうに合わせると、
        // 大きく動かしたぶんが黙って捨てられる。
        const double side = std::max(std::abs(deltaU), std::abs(deltaV));
        u = anchorU + (deltaU < 0.0 ? -side : side);
        v = anchorV + (deltaV < 0.0 ? -side : side);
        return plane.PointAt(u, v);
    }
    // 水平か垂直か。動かした量の大きいほうに合わせる。
    // ちょうど並んだときは横を選ぶ。迷うたびに勝手が変わらないようにする。
    if (std::abs(deltaU) >= std::abs(deltaV)) {
        v = anchorV;
    } else {
        u = anchorU;
    }
    return plane.PointAt(u, v);
}

} // namespace kachakacha::v2::modeling
