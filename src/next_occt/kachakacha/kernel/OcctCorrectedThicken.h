#pragma once
#include "kachakacha/kernel/OcctThicken.h"

namespace kachakacha::v2::kernel::detail {
// Explicitly selected correction, never a silent fallback from strict mode.
base::Result<ThickenedSolid> CorrectedThicken(modeling::KernelShapeHandle source,
    double thickness, fabrication::ThicknessPlacement placement,
    const geometry::GeometryTolerance& tolerance);
}
