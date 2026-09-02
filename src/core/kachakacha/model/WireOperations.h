#pragma once

#include "kachakacha/model/Wire.h"
#include "kachakacha/model/WireConstraints.h"
#include "kachakacha/model/WorkPlane.h"

#include <optional>
#include <vector>

namespace kachakacha::model {

inline constexpr double kWireChainConnectionTolerance = 0.02;

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

struct DirectWireTrimResult {
    Wire removed;
    std::vector<Wire> retained;
};

struct DirectWireExtendResult {
    Wire extended;
    Wire added;
    geometry::Vector3 intersection;
    RetainedLineEnd extendedEnd = RetainedLineEnd::End;
};

[[nodiscard]] DirectWireTrimResult TrimWireAtBoundaries(
    const Wire& target,
    double pickedParameter,
    const std::vector<Wire>& boundaries,
    double tolerance = 1.0e-5);

[[nodiscard]] DirectWireExtendResult ExtendWireToBoundary(
    const Wire& target,
    double pickedParameter,
    const std::vector<Wire>& boundaries,
    double tolerance = 1.0e-5);

[[nodiscard]] LineIntersectionEditResult MeetLinesAtIntersection(
    const Wire& first,
    RetainedLineEnd retainedFirst,
    const Wire& second,
    RetainedLineEnd retainedSecond,
    double tolerance = 1.0e-8);

[[nodiscard]] Wire JoinLineChain(
    const std::vector<Wire>& wires,
    double tolerance = kWireChainConnectionTolerance);

// Orders endpoint-connected wires and flattens native curves only for the
// resulting composite boundary. Source wires remain independently editable.
[[nodiscard]] Wire JoinWireChain(
    const std::vector<Wire>& wires,
    double connectionTolerance = kWireChainConnectionTolerance,
    double curveChordTolerance = 0.01);

[[nodiscard]] Wire OffsetPlanarWire(
    const Wire& wire,
    const WorkPlane& plane,
    double signedDistance,
    double tolerance = 1.0e-8);

[[nodiscard]] Wire ApplyWireLineConstraints(
    const Wire& wire,
    const std::optional<WorkPlane>& plane,
    const WireLineConstraints& constraints,
    double tolerance = 1.0e-8);

[[nodiscard]] Wire ApplyWireCurveConstraints(
    const Wire& wire,
    const WireCurveConstraints& constraints,
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

//! 2本のワイヤの3D交点(距離 tolerance 以内まで近づく点)を求める。
//! 曲線はサンプリングし、粗い候補を局所探索で詰める。同一交点は統合する。
//! 3D空間で「任意の交点」に点を作り、そこから線を引くための基盤。
[[nodiscard]] std::vector<geometry::Vector3> IntersectWires(
    const Wire& first,
    const Wire& second,
    double tolerance = 1.0e-4,
    int samplesPerCurve = 256);

struct PolylineCornerEditResult {
    Wire wire;                     //!< 角を加工した後のポリライン
    geometry::Vector3 firstPoint;  //!< 加工区間の始点(前の辺側)
    geometry::Vector3 secondPoint; //!< 加工区間の終点(次の辺側)
};

//! ポリラインの頂点(vertexIndex)を C 面取りする(両辺 setback で角を落とす)。
//! 閉じたポリラインでは始点/終点の角(vertexIndex=0)も加工できる。
[[nodiscard]] PolylineCornerEditResult CutPolylineCorner(
    const Wire& polyline,
    int vertexIndex,
    double setback);

//! ポリラインの頂点を半径 radius のR丸めへ置き換える(円弧は弦公差で分割)。
[[nodiscard]] PolylineCornerEditResult RoundPolylineCorner(
    const Wire& polyline,
    int vertexIndex,
    double radius,
    double chordToleranceMillimeters = 0.02);

} // namespace kachakacha::model
