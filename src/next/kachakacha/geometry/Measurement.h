#pragma once

//! 測定(PRD-070〜072、およびV1同等性の要件)。
//!
//! V1が持っていたもの: 2点距離、3点角度、方向どうしの角度、
//! 点と線・線と線の最短距離、線の長さ・半径・接線・曲率法線、
//! 方向と平面の角度、平面どうしの角度、点と平面の符号つき距離。
//!
//! V2で足すもの(PRD-070): 2点距離に dX/dY/dZ、各座標面への投影距離、各軸との角度。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/Units.h"

#include <optional>

namespace kachakacha::v2::geometry {

//! 平面。点と法線で持つ。
struct Plane {
    Vector3 origin{};
    Vector3 normal{0.0, 0.0, 1.0};
};

//! 2点間の測定。PRD-070 が要求するものを一度に返す。
struct DistanceMeasurement {
    Vector3 firstPoint{};
    Vector3 secondPoint{};
    double distanceMm = 0.0;
    double deltaXMm = 0.0;
    double deltaYMm = 0.0;
    double deltaZMm = 0.0;
    //! 各座標面へ投影したときの距離。XY面へ落とすとZ成分が消える。
    double projectedOnXYMm = 0.0;
    double projectedOnYZMm = 0.0;
    double projectedOnZXMm = 0.0;
    //! 2点を結ぶ向きと各軸のなす角(0〜pi/2 の鋭角)。
    Radians angleToXAxis{};
    Radians angleToYAxis{};
    Radians angleToZAxis{};
};

//! 角度の測定。向き付きと鋭角の両方を返す(V1と同じ)。
struct AngleMeasurement {
    Radians directed{};
    Radians acute{};
};

//! 2つの要素の最短距離と、その足の位置。
struct ClosestApproach {
    Vector3 firstPoint{};
    Vector3 secondPoint{};
    double distanceMm = 0.0;
    double firstParameter = 0.0;
    double secondParameter = 0.0;
};

// ---- 点どうし ----

[[nodiscard]] base::Result<DistanceMeasurement> MeasureTwoPoints(Vector3 first,
    Vector3 second);

// ---- 角度 ----

[[nodiscard]] base::Result<AngleMeasurement> MeasureDirections(Vector3 first,
    Vector3 second);
[[nodiscard]] base::Result<AngleMeasurement> MeasureThreePointAngle(Vector3 vertex,
    Vector3 first, Vector3 second);

//! 曲線どうしを、指定した位置での接線の角度で測る(V1のElementsモード)。
[[nodiscard]] base::Result<AngleMeasurement> MeasureTangentAngle(const CurveSegment& first,
    double firstParameter, const CurveSegment& second, double secondParameter);

//! 同じく、法線(曲率の向き)どうしの角度。
[[nodiscard]] base::Result<AngleMeasurement> MeasureNormalAngle(const CurveSegment& first,
    double firstParameter, const CurveSegment& second, double secondParameter);

// ---- 距離 ----

[[nodiscard]] ClosestApproach MeasurePointToCurve(Vector3 point,
    const CurveSegment& curve);
[[nodiscard]] ClosestApproach MeasureCurveToCurve(const CurveSegment& first,
    const CurveSegment& second);

// ---- 曲線そのもの ----

[[nodiscard]] double MeasureCurveLength(const CurveSegment& curve, double tolerance);
//! 円と円弧だけが半径を持つ。それ以外は値を返さない。
[[nodiscard]] std::optional<double> MeasureCurveRadius(const CurveSegment& curve);
[[nodiscard]] Vector3 MeasureCurveTangent(const CurveSegment& curve, double parameter);
//! 曲率の向き。直線のように曲がっていない所では値を返さない。
[[nodiscard]] std::optional<Vector3> MeasureCurveCurvatureNormal(const CurveSegment& curve,
    double parameter);

// ---- 平面 ----

[[nodiscard]] base::Result<Radians> MeasureDirectionToPlaneAngle(Vector3 direction,
    const Plane& plane);
[[nodiscard]] base::Result<Radians> MeasurePlaneToPlaneAngle(const Plane& first,
    const Plane& second);
//! 符号つき。法線側が正。
[[nodiscard]] base::Result<double> MeasureSignedPointToPlaneDistance(Vector3 point,
    const Plane& plane);

} // namespace kachakacha::v2::geometry
