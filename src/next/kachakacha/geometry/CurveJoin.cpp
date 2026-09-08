#include "kachakacha/geometry/CurveJoin.h"

#include "kachakacha/geometry/WireEdit.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::geometry {

using base::MakeError;
using base::Result;

namespace {

constexpr const char* kNoSolution = "GEO-J001";
constexpr const char* kUnsupportedKind = "GEO-J002";
constexpr const char* kBadInput = "GEO-J003";

[[nodiscard]] double ParameterAt(CurveEnd end)
{
    return end == CurveEnd::Start ? 0.0 : 1.0;
}

[[nodiscard]] Vector3 PointAt(const CurveSegment& curve, CurveEnd end)
{
    return curve.Evaluate(ParameterAt(end));
}

//! その端から外向きの接線。端点どうしを突き合わせるので、向きを揃える。
[[nodiscard]] Vector3 OutwardTangent(const CurveSegment& curve, CurveEnd end)
{
    const Vector3 derivative = curve.FirstDerivative(ParameterAt(end));
    return end == CurveEnd::Start ? Normalized(-derivative) : Normalized(derivative);
}

[[nodiscard]] Vector3 CurvatureVector(const CurveSegment& curve, CurveEnd end)
{
    const double t = ParameterAt(end);
    const Vector3 first = curve.FirstDerivative(t);
    const Vector3 second = curve.SecondDerivative(t);
    const double speed = first.Length();
    if (!(speed > 0.0)) {
        return {};
    }
    // κ * n = (v × a × v) / |v|^4
    const Vector3 numerator = Cross(Cross(first, second), first);
    return numerator * (1.0 / std::pow(speed, 4.0));
}

//! 曲線の端点を動かす。動かせない種類は断る。
[[nodiscard]] Result<CurveSegment> MoveEndTo(const CurveSegment& curve, CurveEnd end,
    const Vector3& target)
{
    switch (curve.Kind()) {
    case CurveKind::Line: {
        const Vector3 start = end == CurveEnd::Start ? target : curve.StartPoint();
        const Vector3 finish = end == CurveEnd::Start ? curve.EndPoint() : target;
        return CurveSegment::MakeLine(start, finish);
    }
    case CurveKind::CubicBezier: {
        std::vector<Vector3> control = curve.ControlPoints();
        if (control.size() != 4) {
            return Result<CurveSegment>::Failure(MakeError(kBadInput,
                "制御点の数が正しくありません。", {}));
        }
        // 端の制御点を動かし、隣の制御点も同じだけずらして形を保つ。
        const std::size_t moved = end == CurveEnd::Start ? 0 : 3;
        const std::size_t neighbour = end == CurveEnd::Start ? 1 : 2;
        const Vector3 delta = target - control[moved];
        control[moved] = target;
        control[neighbour] = control[neighbour] + delta * 0.5;
        return CurveSegment::MakeCubicBezier(std::move(control));
    }
    default:
        return Result<CurveSegment>::Failure(MakeError(kUnsupportedKind,
            "この種類の曲線は、形を保ったまま端点を動かせません。",
            std::string(CurveKindName(curve.Kind()))
                + " は端点を動かすと形が変わります。分割してからつないでください。"));
    }
}

//! Bezier の端の接線方向を、指定した向きに合わせる。長さは保つ。
[[nodiscard]] Result<CurveSegment> AlignBezierTangent(const CurveSegment& curve,
    CurveEnd end, const Vector3& outward)
{
    if (curve.Kind() != CurveKind::CubicBezier) {
        return Result<CurveSegment>::Failure(MakeError(kUnsupportedKind,
            "接線を合わせられるのはベジェだけです。",
            std::string(CurveKindName(curve.Kind()))
                + " は形を変えずに接線を変えられません。"));
    }
    std::vector<Vector3> control = curve.ControlPoints();
    if (control.size() != 4) {
        return Result<CurveSegment>::Failure(MakeError(kBadInput,
            "制御点の数が正しくありません。", {}));
    }
    const std::size_t anchor = end == CurveEnd::Start ? 0 : 3;
    const std::size_t handle = end == CurveEnd::Start ? 1 : 2;
    const double length = (control[handle] - control[anchor]).Length();
    if (!(length > 0.0)) {
        return Result<CurveSegment>::Failure(MakeError(kNoSolution,
            "制御点が重なっていて、接線を決められません。", {}));
    }
    // outward は端から外へ向かう向き。制御点は内側へ置くので反転する。
    control[handle] = control[anchor] - outward * length;
    return CurveSegment::MakeCubicBezier(std::move(control));
}

} // namespace

std::string_view JoinContinuityNameJa(JoinContinuity value) noexcept
{
    switch (value) {
    case JoinContinuity::Position:  return "端点一致";
    case JoinContinuity::Tangent:   return "接線接続";
    case JoinContinuity::Curvature: return "曲率接続";
    }
    return "";
}

Result<JoinResult> JoinCurves(const CurveSegment& first, CurveEnd firstEnd,
    const CurveSegment& second, CurveEnd secondEnd, JoinContinuity continuity,
    JoinAnchor anchor, const GeometryTolerance& tolerance)
{
    const Vector3 firstPoint = PointAt(first, firstEnd);
    const Vector3 secondPoint = PointAt(second, secondEnd);
    Vector3 target;
    switch (anchor) {
    case JoinAnchor::KeepFirst:
        target = firstPoint;
        break;
    case JoinAnchor::KeepSecond:
        target = secondPoint;
        break;
    case JoinAnchor::Midpoint:
        target = (firstPoint + secondPoint) * 0.5;
        break;
    }

    CurveSegment movedFirst = first;
    CurveSegment movedSecond = second;
    if ((firstPoint - target).Length() > tolerance.modelLinearMm) {
        auto made = MoveEndTo(first, firstEnd, target);
        if (!made.HasValue()) {
            return Result<JoinResult>::Failure(made.Diagnostics());
        }
        movedFirst = made.Value();
    }
    if ((secondPoint - target).Length() > tolerance.modelLinearMm) {
        auto made = MoveEndTo(second, secondEnd, target);
        if (!made.HasValue()) {
            return Result<JoinResult>::Failure(made.Diagnostics());
        }
        movedSecond = made.Value();
    }

    if (continuity != JoinContinuity::Position) {
        // 接線を合わせる。1本目の端の接線に、2本目を合わせる。
        // 端点でつなぐので、2本目の外向き接線は1本目の逆向きになる。
        const Vector3 firstTangent = OutwardTangent(movedFirst, firstEnd);
        if (!(firstTangent.Length() > 0.0)) {
            return Result<JoinResult>::Failure(MakeError(kNoSolution,
                "1本目の接線が決まりません。", "端で速度が0になっています。"));
        }
        auto aligned = AlignBezierTangent(movedSecond, secondEnd, -firstTangent);
        if (!aligned.HasValue()) {
            return Result<JoinResult>::Failure(aligned.Diagnostics());
        }
        movedSecond = aligned.Value();
    }

    JoinResult result{movedFirst, movedSecond, 0.0, 0.0, 0.0};
    result.positionGapMm =
        (PointAt(movedFirst, firstEnd) - PointAt(movedSecond, secondEnd)).Length();
    const Vector3 firstTangent = OutwardTangent(movedFirst, firstEnd);
    const Vector3 secondTangent = OutwardTangent(movedSecond, secondEnd);
    if (firstTangent.Length() > 0.0 && secondTangent.Length() > 0.0) {
        // つながっているとき、2本の外向き接線は正反対を向く。
        const double cosine = std::clamp(Dot(firstTangent, -secondTangent), -1.0, 1.0);
        result.tangentAngleRad = std::acos(cosine);
    }
    result.curvatureDifference =
        (CurvatureVector(movedFirst, firstEnd) - CurvatureVector(movedSecond, secondEnd))
            .Length();

    // 求められた条件を本当に満たしたか、数字で確かめてから返す。
    if (result.positionGapMm > tolerance.modelLinearMm * 10.0) {
        return Result<JoinResult>::Failure(MakeError(kNoSolution,
            "端点を合わせられませんでした。",
            "残った隙間 " + std::to_string(result.positionGapMm) + " mm。"));
    }
    if (continuity != JoinContinuity::Position
        && result.tangentAngleRad > tolerance.modelAngularRad * 1000.0) {
        return Result<JoinResult>::Failure(MakeError(kNoSolution,
            "接線を合わせられませんでした。",
            "残った角度 " + std::to_string(result.tangentAngleRad) + " rad。"));
    }
    if (continuity == JoinContinuity::Curvature) {
        // G2 は、形を保ったままでは満たせないことが多い。満たせないなら言う。
        const double limit = 1.0e-6;
        if (result.curvatureDifference > limit) {
            return Result<JoinResult>::Failure(MakeError(kNoSolution,
                "曲率を合わせられませんでした。",
                "残った差 " + std::to_string(result.curvatureDifference)
                    + "。曲率まで合わせるには、どちらかの形を変える必要があります。"
                      "制御点を増やすか、接線接続で止めてください。"));
        }
    }
    return Result<JoinResult>::Success(std::move(result));
}

Result<std::vector<CurveSegment>> ProcessPolylineCorners(
    const std::vector<CurveSegment>& polyline, double radiusOrDistanceMm, bool round,
    const GeometryTolerance& tolerance)
{
    if (polyline.size() < 2) {
        return Result<std::vector<CurveSegment>>::Failure(MakeError(kBadInput,
            "角が1つもありません。", "線が2本以上必要です。"));
    }
    if (!(radiusOrDistanceMm > 0.0) || !IsFinite(radiusOrDistanceMm)) {
        return Result<std::vector<CurveSegment>>::Failure(MakeError(kBadInput,
            round ? "丸めの半径は正の値です。" : "面取りの距離は正の値です。", {}));
    }
    for (const CurveSegment& segment : polyline) {
        if (segment.Kind() != CurveKind::Line) {
            return Result<std::vector<CurveSegment>>::Failure(MakeError(kUnsupportedKind,
                "角の加工は直線の折れ線にだけ使えます。",
                std::string(CurveKindName(segment.Kind())) + " が混ざっています。"));
        }
    }

    std::vector<CurveSegment> output;
    CurveSegment current = polyline.front();
    for (std::size_t index = 1; index < polyline.size(); ++index) {
        const CurveSegment& next = polyline[index];
        // 角でつながっていること。
        if ((current.EndPoint() - next.StartPoint()).Length() > tolerance.interactiveJoinMm) {
            return Result<std::vector<CurveSegment>>::Failure(MakeError(kBadInput,
                "折れ線がつながっていません。",
                std::to_string(index) + " 本目の始点が、前の線の終点と離れています。"));
        }
        auto processed = round
            ? FilletLines(current, next, radiusOrDistanceMm, tolerance.modelLinearMm)
            : ChamferLines(current, next, radiusOrDistanceMm, tolerance.modelLinearMm);
        if (!processed.HasValue()) {
            return Result<std::vector<CurveSegment>>::Failure(processed.Diagnostics());
        }
        output.push_back(processed.Value().first);
        output.push_back(processed.Value().corner);
        current = processed.Value().second;
    }
    output.push_back(current);
    return Result<std::vector<CurveSegment>>::Success(std::move(output));
}

} // namespace kachakacha::v2::geometry
