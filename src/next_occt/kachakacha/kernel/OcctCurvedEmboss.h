#pragma once
#include "kachakacha/kernel/OcctThicken.h"
#include "kachakacha/geometry/CurveSegment.h"
namespace kachakacha::v2::kernel {
struct CurvedEmbossOptions {
    double heightMm = 1.0;
    double draftRad = 0.0;
    double tiltRad = 0.0;
    geometry::Vector3 tiltReference{1, 0, 0};
    double toleranceMm = 0.01;
};
struct CurvedEmbossResult {
    modeling::KernelShapeHandle handle;
    double volumeMm3 = 0.0;
    double sampledBoundaryErrorMm = 0.0;
};
// A single closed contour on a single face. Angled boundary curves are approximated.
// No tessellation is used to create the solid. Invalid or unsupported inputs fail atomically.
[[nodiscard]] base::Result<CurvedEmbossResult> BuildCurvedEmboss(
    modeling::KernelShapeHandle face, const std::vector<geometry::CurveSegment>& boundary,
    const CurvedEmbossOptions& options);
}
