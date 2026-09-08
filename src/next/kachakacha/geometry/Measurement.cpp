#include "kachakacha/geometry/Measurement.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kachakacha::v2::geometry {

using base::MakeError;
using base::Result;

namespace {

constexpr const char* kNonFinite = "MEA-M001";
constexpr const char* kZeroDirection = "MEA-M002";
constexpr const char* kCoincident = "MEA-M003";

[[nodiscard]] Radians AcuteAngleTo(const Vector3& direction, const Vector3& axis)
{
    const double cosine = std::clamp(std::abs(Dot(Normalized(direction), axis)), 0.0, 1.0);
    return Radians(std::acos(cosine));
}

} // namespace

Result<DistanceMeasurement> MeasureTwoPoints(Vector3 first, Vector3 second)
{
    if (!first.IsFinite() || !second.IsFinite()) {
        return Result<DistanceMeasurement>::Failure(MakeError(kNonFinite,
            "測る点の座標に数値でない値が入っています。", {}));
    }
    const Vector3 delta = second - first;
    DistanceMeasurement measurement;
    measurement.firstPoint = first;
    measurement.secondPoint = second;
    measurement.distanceMm = delta.Length();
    measurement.deltaXMm = delta.x;
    measurement.deltaYMm = delta.y;
    measurement.deltaZMm = delta.z;
    // 座標面へ落とした長さ。XY面ならZ成分を捨てる。
    measurement.projectedOnXYMm = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    measurement.projectedOnYZMm = std::sqrt(delta.y * delta.y + delta.z * delta.z);
    measurement.projectedOnZXMm = std::sqrt(delta.z * delta.z + delta.x * delta.x);
    if (measurement.distanceMm > 0.0) {
        measurement.angleToXAxis = AcuteAngleTo(delta, {1.0, 0.0, 0.0});
        measurement.angleToYAxis = AcuteAngleTo(delta, {0.0, 1.0, 0.0});
        measurement.angleToZAxis = AcuteAngleTo(delta, {0.0, 0.0, 1.0});
    }
    return Result<DistanceMeasurement>::Success(measurement);
}

Result<AngleMeasurement> MeasureDirections(Vector3 first, Vector3 second)
{
    const Vector3 a = Normalized(first);
    const Vector3 b = Normalized(second);
    if (a == Vector3{} || b == Vector3{}) {
        return Result<AngleMeasurement>::Failure(MakeError(kZeroDirection,
            "長さ0の向きは測れません。", "2つの向きに長さを持たせてください。"));
    }
    const double cosine = std::clamp(Dot(a, b), -1.0, 1.0);
    AngleMeasurement measurement;
    measurement.directed = Radians(std::acos(cosine));
    // 鋭角側。180度を超えた分は折り返す。
    measurement.acute = Radians(std::acos(std::clamp(std::abs(cosine), 0.0, 1.0)));
    return Result<AngleMeasurement>::Success(measurement);
}

Result<AngleMeasurement> MeasureThreePointAngle(Vector3 vertex, Vector3 first,
    Vector3 second)
{
    if (!vertex.IsFinite() || !first.IsFinite() || !second.IsFinite()) {
        return Result<AngleMeasurement>::Failure(MakeError(kNonFinite,
            "測る点の座標に数値でない値が入っています。", {}));
    }
    if (Distance(vertex, first) == 0.0 || Distance(vertex, second) == 0.0) {
        return Result<AngleMeasurement>::Failure(MakeError(kCoincident,
            "頂点と同じ位置の点があるため、角度が決まりません。", {}));
    }
    return MeasureDirections(first - vertex, second - vertex);
}

Result<AngleMeasurement> MeasureTangentAngle(const CurveSegment& first,
    double firstParameter, const CurveSegment& second, double secondParameter)
{
    return MeasureDirections(first.FirstDerivative(firstParameter),
        second.FirstDerivative(secondParameter));
}

Result<AngleMeasurement> MeasureNormalAngle(const CurveSegment& first,
    double firstParameter, const CurveSegment& second, double secondParameter)
{
    const std::optional<Vector3> a = MeasureCurveCurvatureNormal(first, firstParameter);
    const std::optional<Vector3> b = MeasureCurveCurvatureNormal(second, secondParameter);
    if (!a.has_value() || !b.has_value()) {
        return Result<AngleMeasurement>::Failure(MakeError(kZeroDirection,
            "まっすぐな所では法線の向きが決まりません。",
            "曲がっている位置を選んでください。"));
    }
    return MeasureDirections(*a, *b);
}

ClosestApproach MeasurePointToCurve(Vector3 point, const CurveSegment& curve)
{
    const auto closest = curve.ClosestPoint(point);
    ClosestApproach approach;
    approach.firstPoint = point;
    approach.secondPoint = closest.point;
    approach.distanceMm = closest.distance;
    approach.firstParameter = 0.0;
    approach.secondParameter = closest.parameter;
    return approach;
}

ClosestApproach MeasureCurveToCurve(const CurveSegment& first, const CurveSegment& second)
{
    // 粗い標本で当たりを付けて、交互に詰める。
    constexpr int kSamples = 128;
    double bestDistance = std::numeric_limits<double>::max();
    double bestFirst = 0.0;
    for (int index = 0; index <= kSamples; ++index) {
        const double t = static_cast<double>(index) / kSamples;
        const double distance = second.ClosestPoint(first.Evaluate(t)).distance;
        if (distance < bestDistance) {
            bestDistance = distance;
            bestFirst = t;
        }
    }
    double low = std::max(0.0, bestFirst - 1.0 / kSamples);
    double high = std::min(1.0, bestFirst + 1.0 / kSamples);
    for (int iteration = 0; iteration < 80; ++iteration) {
        const double third = (high - low) / 3.0;
        const double a = low + third;
        const double b = high - third;
        if (second.ClosestPoint(first.Evaluate(a)).distance
            < second.ClosestPoint(first.Evaluate(b)).distance) {
            high = b;
        } else {
            low = a;
        }
    }
    const double firstParameter = (low + high) * 0.5;
    const Vector3 pointOnFirst = first.Evaluate(firstParameter);
    const auto onSecond = second.ClosestPoint(pointOnFirst);
    ClosestApproach approach;
    approach.firstPoint = pointOnFirst;
    approach.secondPoint = onSecond.point;
    approach.distanceMm = onSecond.distance;
    approach.firstParameter = firstParameter;
    approach.secondParameter = onSecond.parameter;
    return approach;
}

double MeasureCurveLength(const CurveSegment& curve, double tolerance)
{
    return curve.TotalLength(tolerance);
}

std::optional<double> MeasureCurveRadius(const CurveSegment& curve)
{
    if (curve.Kind() == CurveKind::Circle || curve.Kind() == CurveKind::CircularArc) {
        return curve.Radius();
    }
    return std::nullopt;
}

Vector3 MeasureCurveTangent(const CurveSegment& curve, double parameter)
{
    return Normalized(curve.FirstDerivative(parameter));
}

std::optional<Vector3> MeasureCurveCurvatureNormal(const CurveSegment& curve,
    double parameter)
{
    const Vector3 first = curve.FirstDerivative(parameter);
    const Vector3 second = curve.SecondDerivative(parameter);
    const double speedSquared = first.LengthSquared();
    if (!(speedSquared > 0.0)) {
        return std::nullopt;
    }
    // 接線方向の成分を抜いた残りが、曲がっていく向き。
    const Vector3 normal = second - first * (Dot(second, first) / speedSquared);
    if (normal.Length() <= 1.0e-12) {
        return std::nullopt; // まっすぐな所
    }
    return Normalized(normal);
}

Result<Radians> MeasureDirectionToPlaneAngle(Vector3 direction, const Plane& plane)
{
    const Vector3 unitDirection = Normalized(direction);
    const Vector3 unitNormal = Normalized(plane.normal);
    if (unitDirection == Vector3{} || unitNormal == Vector3{}) {
        return Result<Radians>::Failure(MakeError(kZeroDirection,
            "向きか平面の法線の長さが0です。", {}));
    }
    // 面となす角は、法線となす角の余角。
    const double cosine = std::clamp(std::abs(Dot(unitDirection, unitNormal)), 0.0, 1.0);
    return Result<Radians>::Success(Radians(kPi / 2.0 - std::acos(cosine)));
}

Result<Radians> MeasurePlaneToPlaneAngle(const Plane& first, const Plane& second)
{
    const auto angle = MeasureDirections(first.normal, second.normal);
    if (!angle.HasValue()) {
        return Result<Radians>::Failure(angle.Diagnostics());
    }
    return Result<Radians>::Success(angle.Value().acute);
}

Result<double> MeasureSignedPointToPlaneDistance(Vector3 point, const Plane& plane)
{
    const Vector3 unitNormal = Normalized(plane.normal);
    if (unitNormal == Vector3{}) {
        return Result<double>::Failure(MakeError(kZeroDirection,
            "平面の法線の長さが0です。", {}));
    }
    if (!point.IsFinite()) {
        return Result<double>::Failure(MakeError(kNonFinite,
            "点の座標に数値でない値が入っています。", {}));
    }
    return Result<double>::Success(Dot(point - plane.origin, unitNormal));
}

} // namespace kachakacha::v2::geometry
