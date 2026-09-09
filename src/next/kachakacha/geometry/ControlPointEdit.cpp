#include "kachakacha/geometry/ControlPointEdit.h"

#include <cmath>

namespace kachakacha::v2::geometry {
namespace {

using base::MakeError;
using base::Result;

//! 制御点の名前。1始まりで数える。画面に出る言い方に合わせる。
constexpr std::string_view kControlNames[] = {
    "制御点1", "制御点2", "制御点3", "制御点4", "制御点5", "制御点6",
    "制御点7", "制御点8", "制御点9", "制御点10",
};

[[nodiscard]] std::string_view ControlName(std::size_t index)
{
    return index < std::size(kControlNames) ? kControlNames[index]
                                            : std::string_view("制御点");
}

[[nodiscard]] Result<CurveSegment> Refuse(const char* code, const char* summaryJa,
    const char* detailJa)
{
    return Result<CurveSegment>::Failure({MakeError(code, summaryJa, detailJa)});
}

//! 円弧を、始点・終点・中心の3つで作り直す。
//! 角度は中心から見た向きで決める。掃引の向きは元の符号を引き継ぐ。
[[nodiscard]] Result<CurveSegment> RebuildArc(const CurveSegment& arc, Vector3 start,
    Vector3 end, Vector3 center)
{
    const Vector3 toStart = start - center;
    const Vector3 toEnd = end - center;
    const double radius = toStart.Length();
    if (!(radius > 0.0) || !(toEnd.Length() > 0.0)) {
        return Refuse("GEO-D002", "つぶれた形になるので、そこへは動かせません。",
            "中心と端点が重なっています。");
    }
    const Vector3 normal = arc.Normal();
    const Vector3 reference = Normalized(toStart, 1.0e-12);
    if (reference.Length() < 0.5) {
        return Refuse("GEO-D002", "つぶれた形になるので、そこへは動かせません。",
            "向きが決まりません。");
    }
    // 終点の向きを、基準方向から測る。半径は始点で決める。
    const Vector3 unitEnd = Normalized(toEnd, 1.0e-12);
    double sweep = std::atan2(Dot(Cross(reference, unitEnd), normal),
        Dot(reference, unitEnd));
    // 元が長い側の円弧だったなら、長い側を保つ。近い方へ勝手に切り替えない。
    if (arc.SweepAngleRad() < 0.0 && sweep > 0.0) {
        sweep -= 2.0 * 3.14159265358979323846;
    } else if (arc.SweepAngleRad() > 0.0 && sweep < 0.0) {
        sweep += 2.0 * 3.14159265358979323846;
    }
    if (std::abs(sweep) < 1.0e-12) {
        return Refuse("GEO-D002", "つぶれた形になるので、そこへは動かせません。",
            "始点と終点の向きが同じです。");
    }
    return CurveSegment::MakeCircularArc(center, normal, reference, radius, 0.0, sweep);
}

} // namespace

std::vector<EditableControlPoint> EditableControlPointsOf(const CurveSegment& curve)
{
    std::vector<EditableControlPoint> points;
    switch (curve.Kind()) {
    case CurveKind::Line:
        points.push_back({curve.StartPoint(), "始点"});
        points.push_back({curve.EndPoint(), "終点"});
        break;
    case CurveKind::CircularArc:
        points.push_back({curve.StartPoint(), "始点"});
        points.push_back({curve.EndPoint(), "終点"});
        points.push_back({curve.Center(), "中心"});
        break;
    case CurveKind::Circle:
        points.push_back({curve.Center(), "中心"});
        points.push_back({curve.StartPoint(), "半径"});
        break;
    case CurveKind::CubicBezier:
    case CurveKind::CubicBSpline: {
        std::size_t index = 0;
        for (const Vector3& point : curve.ControlPoints()) {
            points.push_back({point, ControlName(index)});
            ++index;
        }
        break;
    }
    }
    return points;
}

Result<CurveSegment> WithControlPointMoved(const CurveSegment& curve, std::size_t index,
    const Vector3& to)
{
    if (!to.IsFinite()) {
        return Refuse("GEO-D001", "その点は動かせません。",
            "行き先に数値でない値が入っています。");
    }
    const auto points = EditableControlPointsOf(curve);
    if (index >= points.size()) {
        return Refuse("GEO-D001", "その点は動かせません。",
            "その番号の制御点がありません。");
    }
    switch (curve.Kind()) {
    case CurveKind::Line:
        return index == 0 ? CurveSegment::MakeLine(to, curve.EndPoint())
                          : CurveSegment::MakeLine(curve.StartPoint(), to);
    case CurveKind::CircularArc: {
        const Vector3 start = index == 0 ? to : curve.StartPoint();
        const Vector3 end = index == 1 ? to : curve.EndPoint();
        if (index == 2) {
            // 中心を動かすときは、円弧ごと平行移動する。
            // 中心だけ動かすと、半径と角が同時に変わって形が読めなくなる。
            const Vector3 delta = to - curve.Center();
            return RebuildArc(curve, start + delta, end + delta, to);
        }
        return RebuildArc(curve, start, end, curve.Center());
    }
    case CurveKind::Circle: {
        if (index == 0) {
            return CurveSegment::MakeCircle(to, curve.Normal(),
                curve.ReferenceDirection(), curve.Radius());
        }
        const double radius = (to - curve.Center()).Length();
        if (!(radius > 0.0)) {
            return Refuse("GEO-D002", "つぶれた形になるので、そこへは動かせません。",
                "半径が 0 になります。");
        }
        return CurveSegment::MakeCircle(curve.Center(), curve.Normal(),
            curve.ReferenceDirection(), radius);
    }
    case CurveKind::CubicBezier:
    case CurveKind::CubicBSpline: {
        std::vector<Vector3> moved = curve.ControlPoints();
        moved[index] = to;
        return curve.Kind() == CurveKind::CubicBezier
            ? CurveSegment::MakeCubicBezier(std::move(moved))
            : CurveSegment::MakeCubicBSpline(std::move(moved));
    }
    }
    return Refuse("GEO-D001", "その点は動かせません。", "この種類の線は扱えません。");
}

} // namespace kachakacha::v2::geometry
