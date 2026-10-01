#pragma once
#include "kachakacha/kernel/OcctExtrude.h"

namespace kachakacha::v2::kernel {
// Translate the actual BRep faces, retaining curved start/end caps. Meshes are never inputs.
[[nodiscard]] base::Result<ExtrudeBuildResult> BuildSurfaceExtrude(
    modeling::KernelShapeHandle source, const geometry::Vector3& direction,
    double startMm, double endMm, const geometry::GeometryTolerance& tolerance);
[[nodiscard]] base::Result<ExtrudeBuildResult> BuildExtrudeToFace(
    const modeling::ExtrudeRequest& request, modeling::KernelShapeHandle source,
    modeling::KernelShapeHandle target, const geometry::GeometryTolerance& tolerance);
} // namespace kachakacha::v2::kernel
