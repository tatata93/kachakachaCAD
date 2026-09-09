#include "kachakacha/geometry/CurveProjection.h"

#include <cmath>

namespace kachakacha::v2::geometry {
namespace {

using base::MakeError;
using Out = base::Result<CurveSegment>;

[[nodiscard]] base::Diagnostic EllipseRefusal(const char* what)
{
    // 折れ線で近似すれば通せてしまうが、それは「落とした」ことにならない。
    return MakeError("GEO-E017", "斜めの面へ落とすと楕円になります。",
        std::string(what) + "。面を線の面と平行にするか、先に線を分けてください。");
}

} // namespace

Vector3 ProjectPointOntoPlane(const Vector3& point, const ProjectionPlane& plane)
{
    const double length = plane.normal.Length();
    if (length <= 0.0) {
        return point;
    }
    const Vector3 unit = plane.normal * (1.0 / length);
    const Vector3 offset = point - plane.origin;
    return point - unit * geometry::Dot(offset, unit);
}

Out ProjectCurveOntoPlane(const CurveSegment& curve, const ProjectionPlane& plane,
    const GeometryTolerance& tolerance)
{
    const double normalLength = plane.normal.Length();
    if (normalLength <= 0.0) {
        return Out::Failure(MakeError("GEO-E018", "落とす先の面が決まっていません。",
            "法線の長さが0です。"));
    }
    switch (curve.Kind()) {
    case CurveKind::Line: {
        const Vector3 start = ProjectPointOntoPlane(curve.StartPoint(), plane);
        const Vector3 end = ProjectPointOntoPlane(curve.EndPoint(), plane);
        if ((end - start).Length() <= tolerance.modelLinearMm) {
            // 面に垂直な線は、落とすと点になる。点は線ではないので断る。
            return Out::Failure(MakeError("GEO-E019", "落とすと点になります。",
                "線が面に垂直です。"));
        }
        return CurveSegment::MakeLine(start, end);
    }
    case CurveKind::CubicBezier: {
        // 射影は一次変換なので、制御点を落とせば同じ次数の曲線になる。近似ではない。
        std::vector<Vector3> points;
        for (const Vector3& point : curve.ControlPoints()) {
            points.push_back(ProjectPointOntoPlane(point, plane));
        }
        return CurveSegment::MakeCubicBezier(std::move(points));
    }
    case CurveKind::CubicBSpline: {
        std::vector<Vector3> points;
        for (const Vector3& point : curve.ControlPoints()) {
            points.push_back(ProjectPointOntoPlane(point, plane));
        }
        return CurveSegment::MakeCubicBSpline(std::move(points));
    }
    case CurveKind::Circle:
    case CurveKind::CircularArc: {
        const Vector3 unit = plane.normal * (1.0 / normalLength);
        const double curveNormalLength = curve.Normal().Length();
        if (curveNormalLength <= 0.0) {
            return Out::Failure(EllipseRefusal("線の面が決まっていません"));
        }
        const Vector3 curveUnit = curve.Normal() * (1.0 / curveNormalLength);
        const double along = std::abs(geometry::Dot(unit, curveUnit));
        if (along < 1.0 - 1.0e-9) {
            return Out::Failure(EllipseRefusal(
                curve.Kind() == CurveKind::Circle ? "円です" : "円弧です"));
        }
        // 面が平行なら、中心を落とすだけでよい。半径も向きも変わらない。
        const Vector3 center = ProjectPointOntoPlane(curve.Center(), plane);
        if (curve.Kind() == CurveKind::Circle) {
            return CurveSegment::MakeCircle(center, curve.Normal(),
                curve.ReferenceDirection(), curve.Radius());
        }
        return CurveSegment::MakeCircularArc(center, curve.Normal(),
            curve.ReferenceDirection(), curve.Radius(), curve.StartAngleRad(),
            curve.SweepAngleRad());
    }
    }
    return Out::Failure(MakeError("GEO-E018", "落とす先の面が決まっていません。",
        "知らない線の種類です。"));
}

base::Result<std::vector<CurveSegment>> ProjectCurvesOntoPlane(
    const std::vector<CurveSegment>& curves, const ProjectionPlane& plane,
    const GeometryTolerance& tolerance)
{
    using Many = base::Result<std::vector<CurveSegment>>;
    if (curves.empty()) {
        return Many::Failure(MakeError("GEO-E020", "落とす線がありません。",
            "先に線を選んでください。"));
    }
    std::vector<CurveSegment> projected;
    projected.reserve(curves.size());
    for (const CurveSegment& curve : curves) {
        const auto one = ProjectCurveOntoPlane(curve, plane, tolerance);
        if (!one.HasValue()) {
            // 半分だけ落ちた形は返さない。どこまで落ちたのかが分からなくなる。
            return Many::Failure(one.Diagnostics());
        }
        projected.push_back(one.Value());
    }
    return Many::Success(std::move(projected));
}

} // namespace kachakacha::v2::geometry
