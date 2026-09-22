#include "kachakacha/geometry/CornerCurves.h"

#include "kachakacha/geometry/CurveIntersection.h"
#include "kachakacha/geometry/GeometryTolerance.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace kachakacha::v2::geometry {

using base::MakeError;
using base::Result;

namespace {

constexpr const char* kNoIntersection = "GEO-E001";
constexpr const char* kNotSupported = "GEO-E003";
constexpr const char* kDegenerate = "GEO-E004";
constexpr double kTwoPi = 6.283185307179586;
constexpr double kEps = 1.0e-9;

//! 角を探すために伸ばした線と、元の線がその中でどこにあるか。
struct Reach {
    CurveSegment curve;      //!< 伸ばした線(伸ばせなければ元のまま)
    double originalStart = 0.0;   //!< 元の始点の t(伸ばした線の上で)
    double originalEnd = 1.0;
    bool closed = false;
};

[[nodiscard]] Result<Reach> Extended(const CurveSegment& curve, double toleranceMm)
{
    using Out = Result<Reach>;
    Reach reach{curve, 0.0, 1.0, false};
    switch (curve.Kind()) {
    case CurveKind::Line: {
        const Vector3 start = curve.StartPoint();
        const Vector3 end = curve.EndPoint();
        const double length = Distance(start, end);
        const double extra = std::max(length * 20.0, 1000.0);
        const Vector3 direction = Normalized(end - start);
        const auto grown = CurveSegment::MakeLine(start - direction * extra, end + direction * extra);
        if (!grown.HasValue()) {
            return Out::Failure(grown.Diagnostics());
        }
        reach.curve = grown.Value();
        reach.originalStart = extra / (length + 2.0 * extra);
        reach.originalEnd = (extra + length) / (length + 2.0 * extra);
        return Out::Success(reach);
    }
    case CurveKind::CircularArc: {
        // 一周まで伸ばす(両側へ同じだけ)。
        const double sweep = curve.SweepAngleRad();
        const double sign = sweep >= 0.0 ? 1.0 : -1.0;
        const double rest = kTwoPi - std::abs(sweep);
        if (rest <= kEps) {
            reach.closed = true;
            return Out::Success(reach);
        }
        const double extra = rest * 0.5;
        const auto grown = CurveSegment::MakeCircularArc(curve.Center(), curve.Normal(),
            curve.ReferenceDirection(), curve.Radius(), curve.StartAngleRad() - sign * extra,
            sign * kTwoPi);
        if (!grown.HasValue()) {
            return Out::Failure(grown.Diagnostics());
        }
        reach.curve = grown.Value();
        reach.originalStart = extra / kTwoPi;
        reach.originalEnd = (extra + std::abs(sweep)) / kTwoPi;
        return Out::Success(reach);
    }
    case CurveKind::Circle:
        reach.closed = true;
        return Out::Success(reach);
    case CurveKind::CubicBezier:
        return Out::Success(reach);   // 延ばさない(実交点だけ)
    case CurveKind::CubicBSpline:
        return Out::Failure(MakeError(kNotSupported, "スプラインの角の加工はまだ入っていません。",
            "スプラインは途中で切れません(GEO-C014)。"));
    }
    (void)toleranceMm;
    return Out::Success(reach);
}

[[nodiscard]] double SubLength(const CurveSegment& curve, double a, double b)
{
    return curve.ArcLength(std::min(a, b), std::max(a, b), 1.0e-6);
}

//! 伸ばした線の [a, b] だけを取り出す(種類を保つ)。
[[nodiscard]] Result<CurveSegment> SubCurve(const CurveSegment& curve, double a, double b)
{
    using Out = Result<CurveSegment>;
    if (!(b > a + kEps)) {
        return Out::Failure(MakeError(kDegenerate, "残る線の長さが 0 になります。", {}));
    }
    CurveSegment piece = curve;
    if (a > kEps) {
        const auto split = curve.Split(std::min(a, 1.0 - kEps));
        if (!split.HasValue()) {
            return Out::Failure(split.Diagnostics());
        }
        piece = *split.Value().second;
        b = (b - a) / (1.0 - a);
    }
    if (b < 1.0 - kEps) {
        const auto split = piece.Split(std::max(b, kEps));
        if (!split.HasValue()) {
            return Out::Failure(split.Diagnostics());
        }
        piece = *split.Value().first;
    }
    return Out::Success(piece);
}

//! 1 本ぶんの角の読み方: 角の t、残す向き(+1 = t が増える側)、残す端の t。
struct Side {
    double corner = 0.0;
    double sign = 1.0;
    double keptEnd = 1.0;
    Vector3 direction{};   //!< 角から残す側へ向く接線
};

[[nodiscard]] Vector3 TangentAt(const CurveSegment& curve, double t, double sign)
{
    const Vector3 derivative = curve.FirstDerivative(std::clamp(t, 0.0, 1.0));
    return Normalized(derivative * sign);
}

//! 残す向きを決める。欄で明示した側 > 押した点のある側 > 元の遠い端の側。
[[nodiscard]] Side ReadSide(const Reach& reach, double corner, const std::optional<Vector3>& hint,
    int keepSide)
{
    Side side;
    side.corner = corner;
    double sign = 0.0;
    if (keepSide == 1) {
        sign = -1.0;
    } else if (keepSide == 2) {
        sign = 1.0;
    }
    if (sign == 0.0 && hint.has_value()) {
        double at = reach.curve.ClosestPoint(*hint).parameter;
        if (reach.closed) {
            // 円は近い方の回り方。
            double delta = at - corner;
            delta -= std::floor(delta + 0.5);
            sign = delta >= 0.0 ? 1.0 : -1.0;
        } else if (std::abs(at - corner) > kEps) {
            sign = at > corner ? 1.0 : -1.0;
        }
    }
    if (sign == 0.0) {
        const Vector3 cornerPoint = reach.curve.Evaluate(std::clamp(corner, 0.0, 1.0));
        const double toStart = Distance(reach.curve.Evaluate(reach.originalStart), cornerPoint);
        const double toEnd = Distance(reach.curve.Evaluate(reach.originalEnd), cornerPoint);
        sign = toEnd >= toStart ? 1.0 : -1.0;
    }
    side.sign = sign;
    side.keptEnd = reach.closed ? corner + sign : (sign > 0.0 ? reach.originalEnd : reach.originalStart);
    side.direction = TangentAt(reach.curve, corner, sign);
    return side;
}

//! 円は、角を継ぎ目にした一周の円弧(残す向きに進む)に読み替える。角は t = 0、残す端は
//! 相手との次の交点(無ければ一周して角へ戻る)。
[[nodiscard]] Result<Reach> UnrollCircle(const Reach& reach, Side& side, const CurveSegment& other,
    const GeometryTolerance& tolerance)
{
    using Out = Result<Reach>;
    double start = side.corner - std::floor(side.corner);
    const double sweepSign = reach.curve.SweepAngleRad() >= 0.0 ? 1.0 : -1.0;
    const auto arc = CurveSegment::MakeCircularArc(reach.curve.Center(), reach.curve.Normal(),
        reach.curve.ReferenceDirection(), reach.curve.Radius(),
        reach.curve.StartAngleRad() + reach.curve.SweepAngleRad() * start, sweepSign * side.sign * kTwoPi);
    if (!arc.HasValue()) {
        return Out::Failure(arc.Diagnostics());
    }
    Reach unrolled{arc.Value(), 0.0, 1.0, false};
    double next = 1.0;
    for (const CurveIntersection& hit : IntersectCurves(arc.Value(), other, tolerance)) {
        if (hit.firstParameter > 1.0e-6 && hit.firstParameter < next - 1.0e-6) {
            next = hit.firstParameter;
        }
    }
    unrolled.originalEnd = next;
    side.corner = 0.0;
    side.sign = 1.0;
    side.keptEnd = next;
    side.direction = TangentAt(arc.Value(), 0.0, 1.0);
    return Out::Success(unrolled);
}

//! 残る線: 角から切った点 cut までを捨て、cut から残す端まで。toward = 真なら cut で終わる向き。
[[nodiscard]] Result<CurveSegment> KeptPiece(const Reach& reach, const Side& side, double cut,
    bool endAtCut)
{
    using Out = Result<CurveSegment>;
    const double a = std::min(cut, side.keptEnd);
    const double b = std::max(cut, side.keptEnd);
    const auto piece = SubCurve(reach.curve, a, b);
    if (!piece.HasValue()) {
        return piece;
    }
    // 向き: side.sign > 0 なら piece は cut → 残す端。cut で終わらせたいなら反転。
    const bool endsAtCut = side.sign < 0.0;
    if (endsAtCut == endAtCut) {
        return Out::Success(piece.Value());
    }
    return ReverseCurve(piece.Value());
}

//! 角から線に沿って距離 d の位置(残す側)。届かなければ値なし。
[[nodiscard]] std::optional<double> ParameterAtDistance(const Reach& reach, const Side& side,
    double distanceMm)
{
    double low = side.corner;
    double high = side.keptEnd;
    if (SubLength(reach.curve, low, high) < distanceMm) {
        return std::nullopt;
    }
    for (int iteration = 0; iteration < 80; ++iteration) {
        const double middle = (low + high) * 0.5;
        if (SubLength(reach.curve, side.corner, middle) < distanceMm) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return (low + high) * 0.5;
}

struct Corner {
    Reach first;
    Reach second;
    Side sideA;
    Side sideB;
    Vector3 point{};
    Vector3 normal{};
};

//! 角を決める。実交点(無ければ伸ばして)のうち押した点に近いもの。
[[nodiscard]] Result<Corner> FindCorner(const CurveSegment& first, const CurveSegment& second,
    const CornerOptions& options, const GeometryTolerance& tolerance)
{
    using Out = Result<Corner>;
    const auto reachA = Extended(first, tolerance.modelLinearMm);
    const auto reachB = Extended(second, tolerance.modelLinearMm);
    if (!reachA.HasValue()) {
        return Out::Failure(reachA.Diagnostics());
    }
    if (!reachB.HasValue()) {
        return Out::Failure(reachB.Diagnostics());
    }
    auto hits = IntersectCurves(first, second, tolerance);
    Reach a = reachA.Value();
    Reach b = reachB.Value();
    bool onExtended = false;
    if (hits.empty()) {
        hits = IntersectCurves(a.curve, b.curve, tolerance);
        onExtended = true;
    }
    if (hits.empty()) {
        return Out::Failure(MakeError(kNoIntersection, "2 本が交わりません(伸ばしても届きません)。",
            "交わる 2 本、または端で触れている 2 本を選んでください。"));
    }
    // 押した点に近い交点。無ければ、両方の線の端に近い交点。
    const CurveIntersection* chosen = nullptr;
    double bestScore = std::numeric_limits<double>::infinity();
    for (const CurveIntersection& hit : hits) {
        double score = 0.0;
        if (options.firstHint.has_value() || options.secondHint.has_value()) {
            if (options.firstHint.has_value()) {
                score += Distance(*options.firstHint, hit.position);
            }
            if (options.secondHint.has_value()) {
                score += Distance(*options.secondHint, hit.position);
            }
        } else {
            score = std::min(Distance(first.StartPoint(), hit.position), Distance(first.EndPoint(), hit.position))
                + std::min(Distance(second.StartPoint(), hit.position), Distance(second.EndPoint(), hit.position));
        }
        if (score < bestScore) {
            bestScore = score;
            chosen = &hit;
        }
    }
    const Vector3 point = chosen->position;
    // 交点の t を、伸ばした線の上の t に直す。
    const double ta = onExtended ? chosen->firstParameter : a.curve.ClosestPoint(point).parameter;
    const double tb = onExtended ? chosen->secondParameter : b.curve.ClosestPoint(point).parameter;
    Side sideA = ReadSide(a, ta, options.firstHint, options.firstKeepSide);
    Side sideB = ReadSide(b, tb, options.secondHint, options.secondKeepSide);
    if (a.closed) {
        const auto unrolled = UnrollCircle(a, sideA, second, tolerance);
        if (!unrolled.HasValue()) {
            return Out::Failure(unrolled.Diagnostics());
        }
        a = unrolled.Value();
    }
    if (b.closed) {
        const auto unrolled = UnrollCircle(b, sideB, first, tolerance);
        if (!unrolled.HasValue()) {
            return Out::Failure(unrolled.Diagnostics());
        }
        b = unrolled.Value();
    }
    const Vector3 normal = Normalized(Cross(sideA.direction, sideB.direction));
    if (normal == Vector3{}) {
        return Out::Failure(MakeError(kDegenerate, "角で 2 本が同じ向きなので、加工する面が決まりません。",
            "接している 2 本や一直線の 2 本は面取り・丸めできません。"));
    }
    return Out::Success(Corner{a, b, sideA, sideB, point, normal});
}

//! 丸め。接点 A(u) から r だけ内側へ置いた中心が、B からちょうど r 離れる u を探す。
[[nodiscard]] Result<CornerResult> FilletCorner(const Corner& corner, double radiusMm,
    const GeometryTolerance& tolerance)
{
    using Out = Result<CornerResult>;
    const CurveSegment& a = corner.first.curve;
    const CurveSegment& b = corner.second.curve;
    const Side& sideA = corner.sideA;
    const Side& sideB = corner.sideB;
    // 内側の法線の向き: 角で B の残す側を向くほう。
    const Vector3 normalAtCorner = Normalized(Cross(corner.normal, sideA.direction));
    const double inward = Dot(normalAtCorner, sideB.direction) >= 0.0 ? 1.0 : -1.0;
    const auto centerAt = [&](double u) {
        const Vector3 tangent = TangentAt(a, u, sideA.sign);
        const Vector3 normal = Normalized(Cross(corner.normal, tangent)) * inward;
        return a.Evaluate(std::clamp(u, 0.0, 1.0)) + normal * radiusMm;
    };
    const auto gap = [&](double u) { return b.ClosestPoint(centerAt(u)).distance - radiusMm; };
    const double from = sideA.corner;
    const double to = sideA.keptEnd;
    constexpr int kSteps = 256;
    double previousU = from;
    double previousGap = gap(from);
    std::optional<double> root;
    for (int step = 1; step <= kSteps && !root.has_value(); ++step) {
        const double u = from + (to - from) * step / kSteps;
        const double value = gap(u);
        if ((previousGap < 0.0) != (value < 0.0)) {
            double low = previousU;
            double high = u;
            double lowGap = previousGap;
            for (int iteration = 0; iteration < 80; ++iteration) {
                const double middle = (low + high) * 0.5;
                const double middleGap = gap(middle);
                if ((lowGap < 0.0) == (middleGap < 0.0)) {
                    low = middle;
                    lowGap = middleGap;
                } else {
                    high = middle;
                }
            }
            root = (low + high) * 0.5;
        }
        previousU = u;
        previousGap = value;
    }
    if (!root.has_value()) {
        return Out::Failure(MakeError(kDegenerate, "半径が大きすぎて 2 本に収まりません。",
            "半径を小さくするか、線を長くしてください。"));
    }
    const Vector3 center = centerAt(*root);
    const auto onB = b.ClosestPoint(center);
    // 接点が B の残す側に無ければ、接する円弧にならない。
    const double vb = onB.parameter;
    const bool onKept = sideB.sign > 0.0 ? (vb >= sideB.corner - 1.0e-6 && vb <= sideB.keptEnd + 1.0e-6)
                                         : (vb <= sideB.corner + 1.0e-6 && vb >= sideB.keptEnd - 1.0e-6);
    if (!onKept || std::abs(onB.distance - radiusMm) > std::max(radiusMm * 1.0e-6, tolerance.modelLinearMm * 10.0)) {
        return Out::Failure(MakeError(kDegenerate, "半径が大きすぎて 2 本に収まりません。",
            "接点が残す側の線の外へ出ます。半径を小さくしてください。"));
    }
    const Vector3 pointA = a.Evaluate(*root);
    const Vector3 pointB = onB.point;
    const Vector3 reference = Normalized(pointA - center);
    const Vector3 binormal = Cross(corner.normal, reference);
    const Vector3 toB = pointB - center;
    const double sweep = std::atan2(Dot(toB, binormal), Dot(toB, reference));
    const auto arc = CurveSegment::MakeCircularArc(center, corner.normal, reference, radiusMm, 0.0, sweep);
    const auto firstPiece = KeptPiece(corner.first, sideA, *root, true);
    const auto secondPiece = KeptPiece(corner.second, sideB, vb, false);
    if (!arc.HasValue() || !firstPiece.HasValue() || !secondPiece.HasValue()) {
        return Out::Failure(MakeError(kDegenerate, "丸めの結果が作れません。",
            "接点が線の端と重なっています。半径を変えてください。"));
    }
    return Out::Success(CornerResult{firstPiece.Value(), arc.Value(), secondPiece.Value()});
}

//! 面取り。角から線に沿って切戻し、2 点を直線で結ぶ。
[[nodiscard]] Result<CornerResult> ChamferCorner(const Corner& corner, double firstSetbackMm,
    double secondSetbackMm)
{
    using Out = Result<CornerResult>;
    const auto ua = ParameterAtDistance(corner.first, corner.sideA, firstSetbackMm);
    const auto vb = ParameterAtDistance(corner.second, corner.sideB, secondSetbackMm);
    if (!ua.has_value() || !vb.has_value()) {
        return Out::Failure(MakeError(kDegenerate, "切戻し量が線の長さより大きいです。",
            "もっと小さい値にするか、線を長くしてください。"));
    }
    const Vector3 pointA = corner.first.curve.Evaluate(*ua);
    const Vector3 pointB = corner.second.curve.Evaluate(*vb);
    const auto chamfer = CurveSegment::MakeLine(pointA, pointB);
    const auto firstPiece = KeptPiece(corner.first, corner.sideA, *ua, true);
    const auto secondPiece = KeptPiece(corner.second, corner.sideB, *vb, false);
    if (!chamfer.HasValue() || !firstPiece.HasValue() || !secondPiece.HasValue()) {
        return Out::Failure(MakeError(kDegenerate, "面取りの結果が長さ 0 になります。", {}));
    }
    return Out::Success(CornerResult{firstPiece.Value(), chamfer.Value(), secondPiece.Value()});
}

} // namespace

Result<CornerResult> CornerBetweenCurves(const CurveSegment& first, const CurveSegment& second,
    CornerKind kind, double sizeMm, const CornerOptions& options, double toleranceMm)
{
    using Out = Result<CornerResult>;
    if (!(sizeMm > 0.0) || !IsFinite(sizeMm)) {
        return Out::Failure(MakeError(kDegenerate,
            kind == CornerKind::Fillet ? "半径は正の値にしてください。" : "切戻し量は正の値にしてください。", {}));
    }
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = std::max(toleranceMm, 1.0e-6);
    const auto corner = FindCorner(first, second, options, tolerance);
    if (!corner.HasValue()) {
        return Out::Failure(corner.Diagnostics());
    }
    if (kind == CornerKind::Fillet) {
        return FilletCorner(corner.Value(), sizeMm, tolerance);
    }
    const double secondSetback = options.secondSetbackMm > 0.0 ? options.secondSetbackMm : sizeMm;
    if (!IsFinite(secondSetback)) {
        return Out::Failure(MakeError(kDegenerate, "切戻し量は正の値にしてください。", {}));
    }
    return ChamferCorner(corner.Value(), sizeMm, secondSetback);
}

} // namespace kachakacha::v2::geometry
