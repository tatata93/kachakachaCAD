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
//! Generated fabrication boundaries already lie on one supporting face. Build
//! its outer/hole wires directly, preserving the exact 3D curve definitions.
[[nodiscard]] base::Result<modeling::KernelShapeHandle> TrimFabricationFace(
    modeling::KernelShapeHandle support,
    const std::vector<std::vector<geometry::CurveSegment>>& loops,
    const geometry::GeometryTolerance& tolerance);
[[nodiscard]] base::Result<modeling::GuideSurfaceResult> BuildSurfaceWithHoles(
    const modeling::GuideSurfaceRequest& request,const geometry::GeometryTolerance& tolerance);
}
