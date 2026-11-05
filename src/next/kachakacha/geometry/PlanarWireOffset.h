#pragma once
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include <vector>

namespace kachakacha::v2::geometry {
//! Closed line/arc contours retain connected corners and curve kinds.
//! Positive distance is to the left of the ordered traversal, about planeNormal.
//! Single curves and open/disconnected inputs keep OffsetCurveInPlane semantics.
[[nodiscard]] base::Result<std::vector<CurveSegment>> OffsetCurvesInPlane(
    const std::vector<CurveSegment>& curves,Vector3 planeNormal,double distanceMm,
    const GeometryTolerance& tolerance = {});
}
