#pragma once
#include "kachakacha/geometry/CurveSegment.h"

namespace kachakacha::v2::geometry {
// Exact restriction, with no fit or display polyline. A uniform spline needs
// at most two Bezier end pieces and one unchanged uniform interior.
[[nodiscard]] base::Result<std::vector<CurveSegment>> ExtractCurveRange(
    const CurveSegment&,double first,double last);
}
