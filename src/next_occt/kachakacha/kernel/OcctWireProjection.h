#pragma once
#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
#include <vector>
namespace kachakacha::v2::kernel {
enum class WireProjectionMode { Direction, Wrap, Closest };
enum class WireProjectionHits { NearestPositive, NearestEither, All };
inline constexpr const char* kWireProjectionFailed = "KER-PJ001";
inline constexpr const char* kWrapNotDevelopable = "KER-PJ002";
struct WireProjectionInput {
    std::vector<geometry::CurveSegment> curves;
    geometry::Vector3 direction{0,0,-1};
};
struct WireProjectionOptions {
    WireProjectionMode mode = WireProjectionMode::Direction;
    WireProjectionHits hits = WireProjectionHits::NearestPositive;
    double toleranceMm = 0.001;
    double joinToleranceMm = 0.01;
    double maximumDistanceMm = 10000;
    geometry::Vector3 sourceOrigin{}, sourceU{1,0,0}, sourceV{0,1,0};
    geometry::Vector3 targetPoint{};
    double wrapRotationRad = 0;
};
struct WireProjectionResult {
    std::vector<std::vector<geometry::CurveSegment>> wires;
    double maxCurveDeviationMm = 0;
    double maxSampledSurfaceDeviationMm = 0;
    int openEnds = 0;
};
// Only explicitly selected native faces participate. No sampled-surface fallback.
base::Result<WireProjectionResult> ProjectNativeWires(
    const std::vector<modeling::KernelShapeHandle>& faces,
    const std::vector<WireProjectionInput>& inputs, const WireProjectionOptions& options);
}
