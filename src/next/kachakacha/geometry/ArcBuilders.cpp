#include "kachakacha/geometry/ArcBuilders.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::geometry {

using base::MakeError;
using base::Result;

namespace {

constexpr const char* kCollinear = "GEO-C020";
constexpr const char* kRadiusTooSmall = "GEO-C021";
constexpr const char* kDegenerate = "GEO-C022";

//! 中心と面内の点から、基準方向を基準にした角度を測る。
[[nodiscard]] double AngleOf(const Vector3& point, const Vector3& center,
    const Vector3& reference, const Vector3& binormal)
{
    const Vector3 radial = point - center;
    return std::atan2(Dot(radial, binormal), Dot(radial, reference));
}

//! 角度差を、向きを指定して 0〜2pi の範囲へ直す。
[[nodiscard]] double SweepBetween(double from, double to, bool positive)
{
    double sweep = to - from;
    while (sweep <= 0.0) {
        sweep += 2.0 * kPi;
    }
    while (sweep > 2.0 * kPi) {
        sweep -= 2.0 * kPi;
    }
    return positive ? sweep : sweep - 2.0 * kPi;
}

} // namespace

Result<CurveSegment> ArcThroughThreePoints(Vector3 start, Vector3 middle, Vector3 end)
{
    if (!start.IsFinite() || !middle.IsFinite() || !end.IsFinite()) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "点の座標に数値でない値が入っています。", {}));
    }
    const Vector3 firstEdge = middle - start;
    const Vector3 secondEdge = end - middle;
    const Vector3 normal = Cross(firstEdge, secondEdge);
    // 一直線に並んでいると外積が消える。円は決まらない。
    if (normal.Length() <= 1.0e-12 * std::max(1.0, firstEdge.Length() * secondEdge.Length())) {
        return Result<CurveSegment>::Failure(MakeError(kCollinear,
            "3点が一直線に並んでいるため、円弧が決まりません。",
            "真ん中の点を線から外してください。"));
    }
    const Vector3 unitNormal = Normalized(normal);

    // 外心を、2つの弦の垂直二等分線の交点として求める。
    const Vector3 firstMid = (start + middle) * 0.5;
    const Vector3 secondMid = (middle + end) * 0.5;
    const Vector3 firstPerp = Cross(unitNormal, firstEdge);
    const Vector3 secondPerp = Cross(unitNormal, secondEdge);
    // firstMid + s*firstPerp == secondMid + t*secondPerp を解く。
    const Vector3 delta = secondMid - firstMid;
    const double a = Dot(firstPerp, firstPerp);
    const double b = -Dot(firstPerp, secondPerp);
    const double c = Dot(delta, firstPerp);
    const double d = -b;
    const double e = -Dot(secondPerp, secondPerp);
    const double f = Dot(delta, secondPerp);
    const double determinant = a * e - b * d;
    if (std::abs(determinant) <= 1.0e-18) {
        return Result<CurveSegment>::Failure(MakeError(kCollinear,
            "3点から円の中心を決められません。", {}));
    }
    const double s = (c * e - b * f) / determinant;
    const Vector3 center = firstMid + firstPerp * s;
    const double radius = Distance(center, start);
    if (!(radius > 0.0) || !IsFinite(radius)) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "円弧の半径が求まりません。", {}));
    }

    const Vector3 reference = Normalized(start - center);
    const Vector3 binormal = Cross(unitNormal, reference);
    const double middleAngle = AngleOf(middle, center, reference, binormal);
    const double endAngle = AngleOf(end, center, reference, binormal);
    // 真ん中の点を通る向きを選ぶ。
    const bool positive = SweepBetween(0.0, middleAngle, true)
        < SweepBetween(0.0, endAngle, true);
    const double sweep = SweepBetween(0.0, endAngle, positive);
    return CurveSegment::MakeCircularArc(center, unitNormal, reference, radius, 0.0, sweep);
}

Result<CurveSegment> ArcFromEndpointsAndRadius(Vector3 start, Vector3 end, double radius,
    Vector3 planeNormal, bool largeArc, bool clockwise)
{
    if (!start.IsFinite() || !end.IsFinite() || !IsFinite(radius)) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "端点か半径に数値でない値が入っています。", {}));
    }
    const Vector3 chord = end - start;
    const double chordLength = chord.Length();
    if (!(chordLength > 0.0)) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "始点と終点が同じ位置です。", {}));
    }
    if (radius < chordLength * 0.5) {
        return Result<CurveSegment>::Failure(MakeError(kRadiusTooSmall,
            "半径が小さすぎて、その2点を通る円になりません。",
            "半径は端点間の距離の半分(" + std::to_string(chordLength * 0.5)
                + " mm)以上にしてください。"));
    }
    const Vector3 unitNormal = Normalized(planeNormal);
    if (unitNormal == Vector3{}) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "円弧を置く面の向きが決まりません。", {}));
    }
    const Vector3 chordDirection = Normalized(chord);
    Vector3 perpendicular = Cross(unitNormal, chordDirection);
    if (perpendicular == Vector3{}) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "弦が面の法線と平行です。", {}));
    }
    perpendicular = Normalized(perpendicular);
    const double half = chordLength * 0.5;
    const double offset = std::sqrt(std::max(0.0, radius * radius - half * half));
    const double sign = (largeArc == clockwise) ? 1.0 : -1.0;
    const Vector3 center = (start + end) * 0.5 + perpendicular * (offset * sign);

    const Vector3 reference = Normalized(start - center);
    const Vector3 binormal = Cross(unitNormal, reference);
    const double endAngle = AngleOf(end, center, reference, binormal);
    double sweep = SweepBetween(0.0, endAngle, !clockwise);
    const bool isLarge = std::abs(sweep) > kPi;
    if (isLarge != largeArc) {
        sweep = sweep > 0.0 ? sweep - 2.0 * kPi : sweep + 2.0 * kPi;
    }
    return CurveSegment::MakeCircularArc(center, unitNormal, reference, radius, 0.0, sweep);
}

Result<CurveSegment> ArcFromStartTangentRadiusSweep(Vector3 start, Vector3 tangent,
    Vector3 planeNormal, double radius, double sweepAngleRad)
{
    if (!start.IsFinite() || !tangent.IsFinite() || !IsFinite(radius)
        || !IsFinite(sweepAngleRad)) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "始点・接線・半径・角度に数値でない値が入っています。", {}));
    }
    if (!(radius > 0.0)) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "半径は正の値にしてください。", {}));
    }
    const Vector3 unitNormal = Normalized(planeNormal);
    const Vector3 unitTangent = Normalized(tangent);
    if (unitNormal == Vector3{} || unitTangent == Vector3{}) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "接線か面の向きが決まりません。", {}));
    }
    // 中心は接線と直角の方向、掃引の向き側にある。
    const Vector3 toCenter = Normalized(Cross(unitNormal, unitTangent));
    if (toCenter == Vector3{}) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "接線が面の法線と平行です。", {}));
    }
    const double sign = sweepAngleRad >= 0.0 ? 1.0 : -1.0;
    const Vector3 center = start + toCenter * (radius * sign);
    const Vector3 reference = Normalized(start - center);
    return CurveSegment::MakeCircularArc(center, unitNormal, reference, radius, 0.0,
        sweepAngleRad);
}

Result<CurveSegment> ArcFromStartTangentRadiusLength(Vector3 start, Vector3 tangent,
    Vector3 planeNormal, double radius, double arcLengthMm)
{
    if (!IsFinite(arcLengthMm) || !IsFinite(radius) || !(radius > 0.0)) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "半径か円弧長が正しくありません。", {}));
    }
    // 円弧長 = 半径 × 中心角。
    const double sweep = arcLengthMm / radius;
    return ArcFromStartTangentRadiusSweep(start, tangent, planeNormal, radius, sweep);
}

} // namespace kachakacha::v2::geometry
