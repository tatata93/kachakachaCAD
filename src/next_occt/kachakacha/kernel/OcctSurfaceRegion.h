#pragma once
#include "kachakacha/kernel/OcctSurfaceExtrude.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
namespace kachakacha::v2::kernel {
// Keep the native supporting faces inside a closed spatial boundary. Further loops are holes.
// Reject folds, detached boundaries and ambiguous opposite surfaces rather than flattening them.
[[nodiscard]] base::Result<modeling::KernelShapeHandle> TrimSurfaceRegion(
    const std::vector<modeling::KernelShapeHandle>& supports,
    const std::vector<std::vector<geometry::CurveSegment>>& loops,
    const geometry::GeometryTolerance& tolerance, double boundaryApproximationMm = 0,
    double* maximumDeviationMm = nullptr, double* rmsDeviationMm = nullptr);
[[nodiscard]] base::Result<modeling::GuideSurfaceResult> BuildSurfaceWithHoles(
    const modeling::GuideSurfaceRequest& request,const geometry::GeometryTolerance& tolerance);
}
