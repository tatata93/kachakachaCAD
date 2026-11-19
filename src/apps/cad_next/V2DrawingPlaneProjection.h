#pragma once
#include "kachakacha/geometry/CurveProjection.h"
#include "kachakacha/modeling/WorkPlane.h"

struct V2DrawingProjectionStats {
    double maximumErrorMm=0,rmsErrorMm=0;
    std::size_t samples=0;
};
// Reuse the native projection boundary for circles that become ellipses.
// The document and original curves are never changed by this adapter.
kachakacha::v2::base::Result<std::vector<kachakacha::v2::geometry::CurveSegment>> V2ProjectDrawingCurves(
    const std::vector<kachakacha::v2::geometry::CurveSegment>&,
    const kachakacha::v2::modeling::WorkPlaneFrame&,
    const kachakacha::v2::geometry::GeometryTolerance&,V2DrawingProjectionStats* stats=nullptr);
