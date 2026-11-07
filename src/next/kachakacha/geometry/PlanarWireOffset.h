#pragma once
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include <vector>

namespace kachakacha::v2::geometry {
//! Strictly connected open/closed line/arc contours retain corners and curve kinds.
//! Positive distance is to the left of the ordered traversal, about planeNormal.
//! Open contours keep the shifted free ends; single/disconnected inputs retain
//! OffsetCurveInPlane semantics without silently joining pre-existing gaps.
[[nodiscard]] base::Result<std::vector<CurveSegment>> OffsetCurvesInPlane(
    const std::vector<CurveSegment>& curves,Vector3 planeNormal,double distanceMm,
    const GeometryTolerance& tolerance = {});
}
