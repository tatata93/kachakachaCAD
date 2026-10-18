#pragma once
#include "kachakacha/geometry/CurveSegment.h"
#include <vector>

// Only for newly generated wires. Existing segment UUIDs/references are untouched.
std::vector<kachakacha::v2::geometry::CurveSegment> V2CompactGeneratedCurves(
    const std::vector<kachakacha::v2::geometry::CurveSegment>& curves);
