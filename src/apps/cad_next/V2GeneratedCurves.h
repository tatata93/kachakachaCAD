#pragma once
#include "kachakacha/geometry/CurveSegment.h"
#include <vector>

// Only for newly generated wires. Existing segment UUIDs/references are untouched.
std::vector<kachakacha::v2::geometry::CurveSegment> V2CompactGeneratedCurves(
    const std::vector<kachakacha::v2::geometry::CurveSegment>& curves);

// Regroup new boundary edges without changing geometry. Lines, corners and
// ambiguous continuations remain separate; a smooth curved run is one wire.
std::vector<std::vector<kachakacha::v2::geometry::CurveSegment>> V2GeneratedBoundaryRuns(
    const std::vector<std::vector<kachakacha::v2::geometry::CurveSegment>>& edges,
    double modelTolerance);
bool V2IsSmoothCurvedWire(const std::vector<kachakacha::v2::geometry::CurveSegment>& curves);
