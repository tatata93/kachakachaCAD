#pragma once

#include "kachakacha/model/Wire.h"

namespace kachakacha::model {

enum class RetainedLineEnd {
    Automatic,
    Start,
    End,
};

struct LineChamferResult {
    Wire trimmedFirst;
    Wire chamfer;
    Wire trimmedSecond;
    geometry::Vector3 intersection;
    geometry::Vector3 firstTrimPoint;
    geometry::Vector3 secondTrimPoint;
};

[[nodiscard]] LineChamferResult ChamferIntersectingLines(
    const Wire& first,
    RetainedLineEnd retainedFirst,
    double firstSetback,
    const Wire& second,
    RetainedLineEnd retainedSecond,
    double secondSetback,
    double tolerance = 1.0e-8);

} // namespace kachakacha::model
