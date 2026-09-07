#include "kachakacha/geometry/CurveSegment.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kachakacha::v2::geometry {

using base::Diagnostic;
using base::MakeError;
using base::Result;

namespace {

//! 曲線の診断コード。文字列一致ではなくコードで扱う。
constexpr const char* kNonFinite = "GEO-C010";
constexpr const char* kDegenerate = "GEO-C011";
constexpr const char* kParameterOutOfRange = "GEO-C012";
constexpr const char* kControlPointCount = "GEO-C013";

[[nodiscard]] bool AllFinite(const std::vector<Vector3>& points)
{
    for (const Vector3& point : points) {
        if (!point.IsFinite()) {
            return false;
        }
    }
    return true;
}

//! 弧長と外接箱に使う標本数。許容差が細かいほど増やす。
[[nodiscard]] int SampleCount(double tolerance)
{
    if (!(tolerance > 0.0)) {
        return 256;
    }
    const double wanted = 1.0 / std::sqrt(tolerance);
    return std::clamp(static_cast<int>(wanted), 32, 4096);
}

[[nodiscard]] double BinomialBezier(int index, double t)
{
    const double u = 1.0 - t;
    switch (index) {
    case 0: return u * u * u;
    case 1: return 3.0 * u * u * t;
    case 2: return 3.0 * u * t * t;
    case 3: return t * t * t;
    default: return 0.0;
    }
}

} // namespace

// ---------------- 作り方 ----------------

Result<CurveSegment> CurveSegment::MakeLine(Vector3 start, Vector3 end)
{
    if (!start.IsFinite() || !end.IsFinite()) {
        return Result<CurveSegment>::Failure(MakeError(kNonFinite,
            "線の端点に数値でない値が入っています。",
            "座標に NaN や無限大が含まれていないか確かめてください。"));
    }
    if ((end - start).LengthSquared() == 0.0) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "始点と終点が同じ位置です。",
            "長さのある線を引いてください。"));
    }
    CurveSegment segment;
    segment.kind_ = CurveKind::Line;
    segment.controlPoints_ = {start, end};
    return Result<CurveSegment>::Success(std::move(segment));
}

Result<CurveSegment> CurveSegment::MakeCircularArc(Vector3 center, Vector3 normal,
    Vector3 referenceDirection, double radius, double startAngleRad, double sweepAngleRad)
{
    if (!center.IsFinite() || !normal.IsFinite() || !referenceDirection.IsFinite()
        || !IsFinite(radius) || !IsFinite(startAngleRad) || !IsFinite(sweepAngleRad)) {
        return Result<CurveSegment>::Failure(MakeError(kNonFinite,
            "円弧の指定に数値でない値が入っています。",
            "中心・法線・基準方向・半径・角度を確かめてください。"));
    }
    if (!(radius > 0.0)) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "円弧の半径が0以下です。", "正の半径を指定してください。"));
    }
    const Vector3 unitNormal = Normalized(normal);
    if (unitNormal == Vector3{}) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "円弧の法線が定まりません。", "長さのある法線ベクトルを指定してください。"));
    }
    // 基準方向から法線成分を抜いて、面内の向きにする。
    Vector3 planar = referenceDirection - unitNormal * Dot(referenceDirection, unitNormal);
    planar = Normalized(planar);
    if (planar == Vector3{}) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "基準方向が法線と平行です。",
            "法線と平行でない基準方向を指定してください。"));
    }
    if (sweepAngleRad == 0.0) {
        return Result<CurveSegment>::Failure(MakeError(kDegenerate,
            "円弧の掃引角が0です。", "0でない角度を指定してください。"));
    }
    CurveSegment segment;
    segment.kind_ = CurveKind::CircularArc;
    segment.center_ = center;
    segment.normal_ = unitNormal;
    segment.reference_ = planar;
    segment.radius_ = radius;
    segment.startAngle_ = startAngleRad;
    segment.sweepAngle_ = sweepAngleRad;
    return Result<CurveSegment>::Success(std::move(segment));
}

Result<CurveSegment> CurveSegment::MakeCircle(Vector3 center, Vector3 normal,
    Vector3 referenceDirection, double radius)
{
    Result<CurveSegment> arc = MakeCircularArc(center, normal, referenceDirection, radius,
        0.0, 2.0 * kPi);
    if (!arc.HasValue()) {
        return arc;
    }
    CurveSegment segment = arc.Value();
    segment.kind_ = CurveKind::Circle;
    return Result<CurveSegment>::Success(std::move(segment));
}

Result<CurveSegment> CurveSegment::MakeCubicBezier(std::vector<Vector3> controlPoints)
{
    if (controlPoints.size() != 4) {
        return Result<CurveSegment>::Failure(MakeError(kControlPointCount,
            "3次ベジェには制御点が4つ必要です。",
            "いまの指定は " + std::to_string(controlPoints.size()) + " 点です。"));
    }
    if (!AllFinite(controlPoints)) {
        return Result<CurveSegment>::Failure(MakeError(kNonFinite,
            "ベジェの制御点に数値でない値が入っています。", {}));
    }
    CurveSegment segment;
    segment.kind_ = CurveKind::CubicBezier;
    segment.controlPoints_ = std::move(controlPoints);
    return Result<CurveSegment>::Success(std::move(segment));
}

Result<CurveSegment> CurveSegment::MakeCubicBSpline(std::vector<Vector3> controlPoints)
{
    if (controlPoints.size() < 4) {
        return Result<CurveSegment>::Failure(MakeError(kControlPointCount,
            "3次B-splineには制御点が4つ以上必要です。",
            "いまの指定は " + std::to_string(controlPoints.size()) + " 点です。"));
    }
    if (!AllFinite(controlPoints)) {
        return Result<CurveSegment>::Failure(MakeError(kNonFinite,
            "B-splineの制御点に数値でない値が入っています。", {}));
    }
    CurveSegment segment;
    segment.kind_ = CurveKind::CubicBSpline;
    segment.controlPoints_ = std::move(controlPoints);
    return Result<CurveSegment>::Success(std::move(segment));
}

// ---------------- 評価 ----------------

Vector3 CurveSegment::EvaluateArc(double angleRad) const
{
    const Vector3 binormal = Cross(normal_, reference_);
    return center_ + reference_ * (radius_ * std::cos(angleRad))
        + binormal * (radius_ * std::sin(angleRad));
}

Vector3 CurveSegment::Evaluate(double t) const
{
    switch (kind_) {
    case CurveKind::Line:
        return controlPoints_[0] + (controlPoints_[1] - controlPoints_[0]) * t;
    case CurveKind::CircularArc:
    case CurveKind::Circle:
        return EvaluateArc(startAngle_ + sweepAngle_ * t);
    case CurveKind::CubicBezier: {
        Vector3 point{};
        for (int index = 0; index < 4; ++index) {
            point = point + controlPoints_[static_cast<std::size_t>(index)]
                * BinomialBezier(index, t);
        }
        return point;
    }
    case CurveKind::CubicBSpline: {
        // 一様3次B-splineを、制御点を4つずつ使う区分ベジェとして評価する。
        const std::size_t spans = controlPoints_.size() - 3;
        const double scaled = t * static_cast<double>(spans);
        std::size_t span = static_cast<std::size_t>(std::floor(scaled));
        if (span >= spans) {
            span = spans - 1;
        }
        const double local = scaled - static_cast<double>(span);
        const Vector3& p0 = controlPoints_[span];
        const Vector3& p1 = controlPoints_[span + 1];
        const Vector3& p2 = controlPoints_[span + 2];
        const Vector3& p3 = controlPoints_[span + 3];
        const double u = local;
        const double u2 = u * u;
        const double u3 = u2 * u;
        const double b0 = (1.0 - 3.0 * u + 3.0 * u2 - u3) / 6.0;
        const double b1 = (4.0 - 6.0 * u2 + 3.0 * u3) / 6.0;
        const double b2 = (1.0 + 3.0 * u + 3.0 * u2 - 3.0 * u3) / 6.0;
        const double b3 = u3 / 6.0;
        return p0 * b0 + p1 * b1 + p2 * b2 + p3 * b3;
    }
    }
    return {};
}

base::Result<Vector3> CurveSegment::EvaluateChecked(double t) const
{
    if (!IsFinite(t) || t < 0.0 || t > 1.0) {
        return Result<Vector3>::Failure(MakeError(kParameterOutOfRange,
            "曲線の位置指定が 0〜1 の外です。",
            "指定された値: " + std::to_string(t)));
    }
    return Result<Vector3>::Success(Evaluate(t));
}

Vector3 CurveSegment::FirstDerivative(double t) const
{
    switch (kind_) {
    case CurveKind::Line:
        return controlPoints_[1] - controlPoints_[0];
    case CurveKind::CircularArc:
    case CurveKind::Circle: {
        const double angle = startAngle_ + sweepAngle_ * t;
        const Vector3 binormal = Cross(normal_, reference_);
        return (reference_ * (-radius_ * std::sin(angle))
            + binormal * (radius_ * std::cos(angle))) * sweepAngle_;
    }
    case CurveKind::CubicBezier: {
        const Vector3& p0 = controlPoints_[0];
        const Vector3& p1 = controlPoints_[1];
        const Vector3& p2 = controlPoints_[2];
        const Vector3& p3 = controlPoints_[3];
        const double u = 1.0 - t;
        return ((p1 - p0) * (3.0 * u * u) + (p2 - p1) * (6.0 * u * t)
            + (p3 - p2) * (3.0 * t * t));
    }
    case CurveKind::CubicBSpline: {
        // 中心差分。標本幅は曲線全体に対して十分小さく取る。
        const double step = 1.0e-6;
        const double lower = std::max(0.0, t - step);
        const double upper = std::min(1.0, t + step);
        const double span = upper - lower;
        if (!(span > 0.0)) {
            return {};
        }
        return (Evaluate(upper) - Evaluate(lower)) * (1.0 / span);
    }
    }
    return {};
}

Vector3 CurveSegment::SecondDerivative(double t) const
{
    switch (kind_) {
    case CurveKind::Line:
        return {};
    case CurveKind::CircularArc:
    case CurveKind::Circle: {
        const double angle = startAngle_ + sweepAngle_ * t;
        const Vector3 binormal = Cross(normal_, reference_);
        return (reference_ * (-radius_ * std::cos(angle))
            + binormal * (-radius_ * std::sin(angle))) * (sweepAngle_ * sweepAngle_);
    }
    case CurveKind::CubicBezier: {
        const Vector3& p0 = controlPoints_[0];
        const Vector3& p1 = controlPoints_[1];
        const Vector3& p2 = controlPoints_[2];
        const Vector3& p3 = controlPoints_[3];
        return ((p2 - p1 * 2.0 + p0) * (6.0 * (1.0 - t))
            + (p3 - p2 * 2.0 + p1) * (6.0 * t));
    }
    case CurveKind::CubicBSpline: {
        const double step = 1.0e-4;
        const double lower = std::max(0.0, t - step);
        const double upper = std::min(1.0, t + step);
        const double middle = (lower + upper) * 0.5;
        const double half = (upper - lower) * 0.5;
        if (!(half > 0.0)) {
            return {};
        }
        return (Evaluate(upper) - Evaluate(middle) * 2.0 + Evaluate(lower))
            * (1.0 / (half * half));
    }
    }
    return {};
}

double CurveSegment::ArcLength(double t0, double t1, double tolerance) const
{
    if (!IsFinite(t0) || !IsFinite(t1)) {
        return 0.0;
    }
    const double low = std::clamp(std::min(t0, t1), 0.0, 1.0);
    const double high = std::clamp(std::max(t0, t1), 0.0, 1.0);
    if (!(high > low)) {
        return 0.0;
    }
    // 直線と円弧は解析解を持つ。標本で近似しない。
    if (kind_ == CurveKind::Line) {
        return (controlPoints_[1] - controlPoints_[0]).Length() * (high - low);
    }
    if (kind_ == CurveKind::CircularArc || kind_ == CurveKind::Circle) {
        return radius_ * std::abs(sweepAngle_) * (high - low);
    }
    const int samples = SampleCount(tolerance);
    double length = 0.0;
    Vector3 previous = Evaluate(low);
    for (int index = 1; index <= samples; ++index) {
        const double t = low + (high - low) * index / samples;
        const Vector3 current = Evaluate(t);
        length += (current - previous).Length();
        previous = current;
    }
    return length;
}

Bounds3 CurveSegment::Bounds(double tolerance) const
{
    Bounds3 bounds;
    if (kind_ == CurveKind::Line) {
        bounds.Add(controlPoints_[0]);
        bounds.Add(controlPoints_[1]);
        return bounds;
    }
    const int samples = SampleCount(tolerance);
    for (int index = 0; index <= samples; ++index) {
        bounds.Add(Evaluate(static_cast<double>(index) / samples));
    }
    return bounds;
}

base::Result<SplitResult> CurveSegment::Split(double t) const
{
    if (!IsFinite(t) || !(t > 0.0) || !(t < 1.0)) {
        return Result<SplitResult>::Failure(MakeError(kParameterOutOfRange,
            "分割する位置が 0〜1 の内側にありません。",
            "端点そのものでは分割できません。"));
    }
    SplitResult result;
    switch (kind_) {
    case CurveKind::Line: {
        const Vector3 middle = Evaluate(t);
        const auto first = MakeLine(controlPoints_[0], middle);
        const auto second = MakeLine(middle, controlPoints_[1]);
        if (!first.HasValue() || !second.HasValue()) {
            return Result<SplitResult>::Failure(MakeError(kDegenerate,
                "分割の結果が長さ0になります。", {}));
        }
        result.first = std::make_shared<CurveSegment>(first.Value());
        result.second = std::make_shared<CurveSegment>(second.Value());
        return Result<SplitResult>::Success(std::move(result));
    }
    case CurveKind::CircularArc:
    case CurveKind::Circle: {
        // 円弧のまま2本に分ける。折れ線にしない。
        const auto first = MakeCircularArc(center_, normal_, reference_, radius_,
            startAngle_, sweepAngle_ * t);
        const auto second = MakeCircularArc(center_, normal_, reference_, radius_,
            startAngle_ + sweepAngle_ * t, sweepAngle_ * (1.0 - t));
        if (!first.HasValue() || !second.HasValue()) {
            return Result<SplitResult>::Failure(MakeError(kDegenerate,
                "分割の結果が角度0の円弧になります。", {}));
        }
        result.first = std::make_shared<CurveSegment>(first.Value());
        result.second = std::make_shared<CurveSegment>(second.Value());
        return Result<SplitResult>::Success(std::move(result));
    }
    case CurveKind::CubicBezier: {
        // de Casteljau。3次ベジェのまま2本になる。
        const Vector3& p0 = controlPoints_[0];
        const Vector3& p1 = controlPoints_[1];
        const Vector3& p2 = controlPoints_[2];
        const Vector3& p3 = controlPoints_[3];
        const Vector3 a = p0 + (p1 - p0) * t;
        const Vector3 b = p1 + (p2 - p1) * t;
        const Vector3 c = p2 + (p3 - p2) * t;
        const Vector3 d = a + (b - a) * t;
        const Vector3 e = b + (c - b) * t;
        const Vector3 f = d + (e - d) * t;
        const auto first = MakeCubicBezier({p0, a, d, f});
        const auto second = MakeCubicBezier({f, e, c, p3});
        if (!first.HasValue() || !second.HasValue()) {
            return Result<SplitResult>::Failure(MakeError(kDegenerate,
                "分割に失敗しました。", {}));
        }
        result.first = std::make_shared<CurveSegment>(first.Value());
        result.second = std::make_shared<CurveSegment>(second.Value());
        return Result<SplitResult>::Success(std::move(result));
    }
    case CurveKind::CubicBSpline:
        // B-splineのノット挿入は WP-04 の後段。近似で代用しない。
        return Result<SplitResult>::Failure(MakeError("GEO-C014",
            "3次B-splineの分割はまだ入っていません。",
            "近似の折れ線へ落として代用することはしません。"
            "分割したい場合は、いったんベジェで描き直してください。"));
    }
    return Result<SplitResult>::Failure(MakeError(kDegenerate, "分割できません。", {}));
}

ClosestPointResult CurveSegment::ClosestPoint(const Vector3& point) const
{
    // 粗い標本で当たりを付けて、その周りを黄金分割で詰める。
    constexpr int kCoarse = 128;
    double bestT = 0.0;
    double bestDistance = std::numeric_limits<double>::max();
    for (int index = 0; index <= kCoarse; ++index) {
        const double t = static_cast<double>(index) / kCoarse;
        const double distance = (Evaluate(t) - point).LengthSquared();
        if (distance < bestDistance) {
            bestDistance = distance;
            bestT = t;
        }
    }
    double low = std::max(0.0, bestT - 1.0 / kCoarse);
    double high = std::min(1.0, bestT + 1.0 / kCoarse);
    for (int iteration = 0; iteration < 64; ++iteration) {
        const double third = (high - low) / 3.0;
        const double a = low + third;
        const double b = high - third;
        if ((Evaluate(a) - point).LengthSquared() < (Evaluate(b) - point).LengthSquared()) {
            high = b;
        } else {
            low = a;
        }
    }
    ClosestPointResult result;
    result.parameter = (low + high) * 0.5;
    result.point = Evaluate(result.parameter);
    result.distance = (result.point - point).Length();
    return result;
}

bool CurveSegment::IsClosed(double tolerance) const
{
    if (kind_ == CurveKind::Circle) {
        return true;
    }
    return (EndPoint() - StartPoint()).Length() <= tolerance;
}

} // namespace kachakacha::v2::geometry
