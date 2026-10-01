#pragma once
#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
#include <vector>

namespace kachakacha::v2::kernel {
struct ContactPiece {
    modeling::KernelShapeHandle handle;
    double volumeMm3 = 0.0;
    bool inside = false;
    int fragmentIndex = 0;
    int sourceSide = 0;
    geometry::Vector3 center;
};
struct ContactResult {
    std::vector<std::vector<geometry::CurveSegment>> wires;
    std::vector<ContactPiece> pieces;
};
// Uses BRep boundaries, never tessellation edges. Unsupported curves are refused.
[[nodiscard]] base::Result<ContactResult> BuildContact(modeling::KernelShapeHandle target,
    modeling::KernelShapeHandle other, bool wires, bool outside, bool inside, double toleranceMm);
[[nodiscard]] base::Result<ContactResult> BuildLocalTrim(modeling::KernelShapeHandle first,
    modeling::KernelShapeHandle second, const std::vector<int>& removals, double toleranceMm);
[[nodiscard]] base::Result<std::vector<ContactPiece>> ContactSolidPieces(
    modeling::KernelShapeHandle shape, double toleranceMm);
}
