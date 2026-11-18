#pragma once
#include "kachakacha/geometry/CurveSegment.h"
#include <vector>
class V2SurfaceDock;
class QVBoxLayout;

namespace v2ui {
void InstallRevolveControls(V2SurfaceDock&, QVBoxLayout&);
void RefreshRevolveControls(V2SurfaceDock&, bool visible);
// A circle/arc supplies a center and normal, not another revolved profile.
// Adapt only the private UI input; the core's straight-axis contract is unchanged.
inline std::vector<kachakacha::v2::geometry::CurveSegment> RevolveAxisCurves(
    const std::vector<kachakacha::v2::geometry::CurveSegment>& selected)
{
    using namespace kachakacha::v2::geometry;
    if (selected.size() != 1 || (selected.front().Kind() != CurveKind::Circle
            && selected.front().Kind() != CurveKind::CircularArc)) return selected;
    const auto& circle = selected.front();
    const auto line = CurveSegment::MakeLine(circle.Center(), circle.Center() + Normalized(circle.Normal()));
    return line.HasValue() ? std::vector<CurveSegment>{line.Value()} : selected;
}
}
