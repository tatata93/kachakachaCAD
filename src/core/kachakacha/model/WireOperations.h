#pragma once

#include "kachakacha/model/Wire.h"

#include <vector>

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

struct LineFilletResult {
    Wire trimmedFirst;
    Wire fillet;
    Wire trimmedSecond;
    geometry::Vector3 intersection;
    geometry::Vector3 firstTangentPoint;
    geometry::Vector3 secondTangentPoint;
    geometry::Vector3 center;
    double radius = 0.0;
};

struct LineIntersectionEditResult {
    Wire first;
    Wire second;
    geometry::Vector3 intersection;
};

[[nodiscard]] LineIntersectionEditResult MeetLinesAtIntersection(
    const Wire& first,
    RetainedLineEnd retainedFirst,
    const Wire& second,
    RetainedLineEnd retainedSecond,
    double tolerance = 1.0e-8);

[[nodiscard]] Wire JoinLineChain(
    const std::vector<Wire>& wires,
    double tolerance = 1.0e-8);

[[nodiscard]] LineChamferResult ChamferIntersectingLines(
    const Wire& first,
    RetainedLineEnd retainedFirst,
    double firstSetback,
    const Wire& second,
    RetainedLineEnd retainedSecond,
    double secondSetback,
    double tolerance = 1.0e-8);

[[nodiscard]] LineFilletResult FilletIntersectingLines(
    const Wire& first,
    RetainedLineEnd retainedFirst,
    const Wire& second,
    RetainedLineEnd retainedSecond,
    double radius,
    double tolerance = 1.0e-8);

} // namespace kachakacha::model
