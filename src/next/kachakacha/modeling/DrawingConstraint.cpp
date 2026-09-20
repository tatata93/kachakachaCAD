#include "kachakacha/modeling/DrawingConstraint.h"

#include <cmath>
#include <optional>

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

bool ToolUsesRightAngleSnap(DrawingTool tool) noexcept
{
    switch (tool) {
    case DrawingTool::Line:
    case DrawingTool::Polyline:
    case DrawingTool::Spline:
    case DrawingTool::Bezier:
    case DrawingTool::Move:
    case DrawingTool::Copy:
        return true;
    default:
        // 矩形は「正方形へ」の拘束が別にある。円・円弧は向きで決まらない。
        return false;
    }
}

std::optional<geometry::Vector3> SnapDirectionToRightAngle(DrawingTool tool,
    const WorkPlaneFrame& plane, const geometry::Vector3& anchor,
    const geometry::Vector3& point, double toleranceDeg)
{
    if (!ToolUsesRightAngleSnap(tool) || toleranceDeg <= 0.0) {
        return std::nullopt;
    }
    const double du = plane.CoordinateU(point) - plane.CoordinateU(anchor);
    const double dv = plane.CoordinateV(point) - plane.CoordinateV(anchor);
    const double length = std::hypot(du, dv);
    if (length <= 1.0e-9) {
        return std::nullopt;   // まだ動いていない。向きが無い。
    }
    constexpr double kPi = 3.14159265358979323846;
    const double degrees = std::atan2(dv, du) * 180.0 / kPi;
    const double nearest = std::round(degrees / 90.0) * 90.0;
    if (std::abs(degrees - nearest) > toleranceDeg) {
        return std::nullopt;
    }
    // 長さは変えない。向きだけを直角へ置く。丸めの残りが出ないよう、0/±1 で作る。
    const double radians = nearest * kPi / 180.0;
    const double unitU = std::abs(std::cos(radians)) < 0.5 ? 0.0
                                                           : (std::cos(radians) > 0.0 ? 1.0 : -1.0);
    const double unitV = std::abs(std::sin(radians)) < 0.5 ? 0.0
                                                           : (std::sin(radians) > 0.0 ? 1.0 : -1.0);
    return plane.PointAt(plane.CoordinateU(anchor) + unitU * length,
        plane.CoordinateV(anchor) + unitV * length);
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
