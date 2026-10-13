#pragma once
#include "kachakacha/kernel/OcctWireProjection.h"
#include <functional>
namespace kachakacha::v2::kernel::detail {
using ProjectionSourceMatch = std::function<geometry::ClosestPointResult(geometry::Vector3)>;
// Resolve overlapping source intervals, retaining exact subcurves rather than sampled lines.
void SelectVisibleProjection(WireProjectionResult& result,const ProjectionSourceMatch& match,
    const WireProjectionOptions& options);
// Combine only geometrically identical lines/arcs; never fit a new curve across a corner.
void SimplifyProjectionRun(std::vector<geometry::CurveSegment>& run);
}
