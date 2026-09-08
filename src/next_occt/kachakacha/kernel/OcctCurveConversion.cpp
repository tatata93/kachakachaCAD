#include "kachakacha/kernel/OcctCurveConversion.h"

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/geometry/Units.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Line.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <NCollection_Array1.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>

#include <algorithm>
#include <cmath>
#include <string>

namespace kachakacha::v2::kernel {

using base::MakeError;
using base::Result;
using geometry::CurveKind;
using geometry::CurveSegment;
using geometry::Vector3;
using geometry::kPi;

namespace {

//! OCCT は失敗を例外で知らせる。ここで全部受けて、外へ出さない。
template<class Function>
[[nodiscard]] auto Guarded(Function&& body, const char* what) -> decltype(body())
{
    using ResultType = decltype(body());
    try {
        return body();
    } catch (const Standard_Failure& failure) {
        return ResultType::Failure(MakeError(kKernelFailure,
            "幾何カーネルが処理できませんでした。",
            std::string(what) + ": " + std::string(failure.GetMessageString())));
    } catch (const std::exception& error) {
        return ResultType::Failure(MakeError(kKernelFailure,
            "幾何カーネルが処理できませんでした。",
            std::string(what) + ": " + error.what()));
    } catch (...) {
        return ResultType::Failure(MakeError(kKernelFailure,
            "幾何カーネルが処理できませんでした。", what));
    }
}

} // namespace

gp_Pnt ToPoint(const Vector3& value)
{
    return gp_Pnt(value.x, value.y, value.z);
}

gp_Vec ToVector(const Vector3& value)
{
    return gp_Vec(value.x, value.y, value.z);
}

Vector3 FromPoint(const gp_Pnt& value)
{
    return Vector3{value.X(), value.Y(), value.Z()};
}

Result<occ::handle<Geom_Curve>> ToGeomCurve(const CurveSegment& segment)
{
    using Out = Result<occ::handle<Geom_Curve>>;
    return Guarded([&]() -> Out {
        switch (segment.Kind()) {
        case CurveKind::Line: {
            const gp_Pnt start = ToPoint(segment.StartPoint());
            const gp_Pnt end = ToPoint(segment.EndPoint());
            const gp_Vec direction(start, end);
            const double length = direction.Magnitude();
            if (length <= Precision::Confusion()) {
                return Out::Failure(MakeError(kCurveUnsupported,
                    "長さが0の直線は渡せません。", {}));
            }
            occ::handle<Geom_Line> line = new Geom_Line(start, gp_Dir(direction));
            occ::handle<Geom_Curve> trimmed = new Geom_TrimmedCurve(line, 0.0, length);
            return Out::Success(trimmed);
        }
        case CurveKind::Circle:
        case CurveKind::CircularArc: {
            if (!(segment.Radius() > Precision::Confusion())) {
                return Out::Failure(MakeError(kCurveUnsupported,
                    "半径が0の円は渡せません。", {}));
            }
            const gp_Ax2 axis(ToPoint(segment.Center()),
                gp_Dir(ToVector(segment.Normal())),
                gp_Dir(ToVector(segment.ReferenceDirection())));
            occ::handle<Geom_Circle> circle = new Geom_Circle(axis, segment.Radius());
            if (segment.Kind() == CurveKind::Circle) {
                return Out::Success(occ::handle<Geom_Curve>(circle));
            }
            const double start = segment.StartAngleRad();
            const double sweep = segment.SweepAngleRad();
            if (std::abs(sweep) <= 1.0e-12) {
                return Out::Failure(MakeError(kCurveUnsupported,
                    "掃引角が0の円弧は渡せません。", {}));
            }
            // OCCT は増える向きの区間しか受け取らない。
            // 逆向きは区間を入れ替えて渡し、辺の向きで元の向きを表す。
            const double first = sweep >= 0.0 ? start : start + sweep;
            const double last = sweep >= 0.0 ? start + sweep : start;
            occ::handle<Geom_Curve> trimmed =
                new Geom_TrimmedCurve(circle, first, last);
            return Out::Success(trimmed);
        }
        case CurveKind::CubicBezier: {
            const auto& control = segment.ControlPoints();
            if (control.size() != 4) {
                return Out::Failure(MakeError(kCurveUnsupported,
                    "3次ベジェの制御点は4点でなければなりません。",
                    std::to_string(control.size()) + " 点でした。"));
            }
            NCollection_Array1<gp_Pnt> points(1, static_cast<int>(control.size()));
            for (std::size_t index = 0; index < control.size(); ++index) {
                points.SetValue(static_cast<int>(index + 1), ToPoint(control[index]));
            }
            occ::handle<Geom_Curve> bezier = new Geom_BezierCurve(points);
            return Out::Success(bezier);
        }
        case CurveKind::CubicBSpline: {
            // core と同じ一様3次B-spline(節点は等間隔、両端は重複度4)。
            const auto& control = segment.ControlPoints();
            if (control.size() < 4) {
                return Out::Failure(MakeError(kCurveUnsupported,
                    "B-splineの制御点が足りません。",
                    std::to_string(control.size()) + " 点でした。4点以上必要です。"));
            }
            NCollection_Array1<gp_Pnt> points(1, static_cast<int>(control.size()));
            for (std::size_t index = 0; index < control.size(); ++index) {
                points.SetValue(static_cast<int>(index + 1), ToPoint(control[index]));
            }
            const int spans = static_cast<int>(control.size()) - 3;
            NCollection_Array1<double> knots(1, spans + 1);
            NCollection_Array1<int> multiplicities(1, spans + 1);
            for (int index = 1; index <= spans + 1; ++index) {
                knots.SetValue(index, static_cast<double>(index - 1));
                multiplicities.SetValue(index, 1);
            }
            multiplicities.SetValue(1, 4);
            multiplicities.SetValue(spans + 1, 4);
            occ::handle<Geom_Curve> spline =
                new Geom_BSplineCurve(points, knots, multiplicities, 3);
            return Out::Success(spline);
        }
        }
        return Out::Failure(MakeError(kCurveUnsupported, "知らない曲線の種類です。", {}));
    }, "曲線の変換");
}

Result<TopoDS_Edge> ToEdge(const CurveSegment& segment)
{
    auto curve = ToGeomCurve(segment);
    if (!curve.HasValue()) {
        return Result<TopoDS_Edge>::Failure(curve.Diagnostics());
    }
    return Guarded([&]() -> Result<TopoDS_Edge> {
        BRepBuilderAPI_MakeEdge maker(curve.Value());
        if (!maker.IsDone()) {
            return Result<TopoDS_Edge>::Failure(MakeError(kKernelFailure,
                "辺を作れませんでした。", {}));
        }
        TopoDS_Edge edge = maker.Edge();
        // 掃引が負の円弧は、向きを反転して元の向きに合わせる。
        if (segment.Kind() == CurveKind::CircularArc && segment.SweepAngleRad() < 0.0) {
            edge.Reverse();
        }
        return Result<TopoDS_Edge>::Success(edge);
    }, "辺の作成");
}

Result<TopoDS_Wire> ToWire(const std::vector<CurveSegment>& segments, double toleranceMm)
{
    if (segments.empty()) {
        return Result<TopoDS_Wire>::Failure(MakeError(kCurveUnsupported,
            "線が1本もありません。", {}));
    }
    // 繋がっていない並びを OCCT へ渡すと、黙って隙間を埋めた「それらしい何か」が出る。
    // ここで断る。許容差の100倍までは繋がっているとみなす(UIの結合許容差に合わせる)。
    const double limit = std::max(toleranceMm, 1.0e-6) * 100.0;
    for (std::size_t index = 1; index < segments.size(); ++index) {
        const double gap =
            (segments[index].StartPoint() - segments[index - 1].EndPoint()).Length();
        if (gap > limit) {
            return Result<TopoDS_Wire>::Failure(MakeError(kNotConnected,
                "線がつながっていません。",
                std::to_string(index) + " 本目の始点が前の終点から "
                    + std::to_string(gap) + " mm 離れています。"));
        }
    }
    return Guarded([&]() -> Result<TopoDS_Wire> {
        BRepBuilderAPI_MakeWire maker;
        BRep_Builder builder;
        for (const CurveSegment& segment : segments) {
            auto edge = ToEdge(segment);
            if (!edge.HasValue()) {
                return Result<TopoDS_Wire>::Failure(edge.Diagnostics());
            }
            TopoDS_Edge shape = edge.Value();
            // 端点の許容差を、上で認めた継ぎ目の幅に合わせて申告する。
            // これをしないと OCCT は既定の 1e-7 でしか繋がず、
            // 利用者が結合許容差として認めた隙間でもワイヤーにならない。
            // 形を動かすのではなく「この点はこの幅までは同じ点である」と言うだけである。
            for (TopExp_Explorer explorer(shape, TopAbs_VERTEX); explorer.More();
                explorer.Next()) {
                builder.UpdateVertex(TopoDS::Vertex(explorer.Current()), limit);
            }
            maker.Add(shape);
        }
        if (!maker.IsDone()) {
            return Result<TopoDS_Wire>::Failure(MakeError(kKernelFailure,
                "ワイヤーを作れませんでした。", "線の並びか向きを確かめてください。"));
        }
        return Result<TopoDS_Wire>::Success(maker.Wire());
    }, "ワイヤーの作成");
}

Result<CurveSegment> FromEdge(const TopoDS_Edge& edge, double toleranceMm)
{
    (void)toleranceMm;
    return Guarded([&]() -> Result<CurveSegment> {
        double first = 0.0;
        double last = 0.0;
        occ::handle<Geom_Curve> curve = BRep_Tool::Curve(edge, first, last);
        if (curve.IsNull()) {
            return Result<CurveSegment>::Failure(MakeError(kCurveUnsupported,
                "辺に曲線がありません。", {}));
        }
        occ::handle<Geom_Curve> basis = curve;
        while (basis->IsKind(STANDARD_TYPE(Geom_TrimmedCurve))) {
            basis = occ::handle<Geom_TrimmedCurve>::DownCast(basis)->BasisCurve();
        }
        const bool reversed = edge.Orientation() == TopAbs_REVERSED;

        if (basis->IsKind(STANDARD_TYPE(Geom_Line))) {
            const Vector3 startPoint = FromPoint(curve->Value(first));
            const Vector3 endPoint = FromPoint(curve->Value(last));
            return reversed ? CurveSegment::MakeLine(endPoint, startPoint)
                            : CurveSegment::MakeLine(startPoint, endPoint);
        }
        if (basis->IsKind(STANDARD_TYPE(Geom_Circle))) {
            occ::handle<Geom_Circle> circle = occ::handle<Geom_Circle>::DownCast(basis);
            const gp_Ax2 axis = circle->Position();
            const Vector3 center = FromPoint(axis.Location());
            const Vector3 normal{axis.Direction().X(), axis.Direction().Y(),
                axis.Direction().Z()};
            const Vector3 reference{axis.XDirection().X(), axis.XDirection().Y(),
                axis.XDirection().Z()};
            const double sweep = last - first;
            if (std::abs(std::abs(sweep) - 2.0 * kPi) <= 1.0e-9) {
                return CurveSegment::MakeCircle(center, normal, reference,
                    circle->Radius());
            }
            return CurveSegment::MakeCircularArc(center, normal, reference,
                circle->Radius(), reversed ? last : first,
                reversed ? -sweep : sweep);
        }
        if (basis->IsKind(STANDARD_TYPE(Geom_BezierCurve))) {
            occ::handle<Geom_BezierCurve> bezier =
                occ::handle<Geom_BezierCurve>::DownCast(basis);
            if (bezier->Degree() != 3) {
                return Result<CurveSegment>::Failure(MakeError(kCurveUnsupported,
                    "3次でないベジェは扱えません。",
                    "次数 " + std::to_string(bezier->Degree()) + "。"));
            }
            std::vector<Vector3> control;
            control.reserve(static_cast<std::size_t>(bezier->NbPoles()));
            for (int index = 1; index <= bezier->NbPoles(); ++index) {
                control.push_back(FromPoint(bezier->Pole(index)));
            }
            if (reversed) {
                std::reverse(control.begin(), control.end());
            }
            return CurveSegment::MakeCubicBezier(std::move(control));
        }
        if (basis->IsKind(STANDARD_TYPE(Geom_BSplineCurve))) {
            occ::handle<Geom_BSplineCurve> spline =
                occ::handle<Geom_BSplineCurve>::DownCast(basis);
            if (spline->Degree() != 3) {
                return Result<CurveSegment>::Failure(MakeError(kCurveUnsupported,
                    "3次でないB-splineは、形を保ったまま戻せません。",
                    "次数 " + std::to_string(spline->Degree()) + "。"));
            }
            std::vector<Vector3> control;
            control.reserve(static_cast<std::size_t>(spline->NbPoles()));
            for (int index = 1; index <= spline->NbPoles(); ++index) {
                control.push_back(FromPoint(spline->Pole(index)));
            }
            if (control.size() < 4) {
                return Result<CurveSegment>::Failure(MakeError(kCurveUnsupported,
                    "制御点が足りないB-splineです。", {}));
            }
            if (reversed) {
                std::reverse(control.begin(), control.end());
            }
            return CurveSegment::MakeCubicBSpline(std::move(control));
        }
        // 折れ線へ落として「できた」ことにしない(設計の芯)。
        return Result<CurveSegment>::Failure(MakeError(kCurveUnsupported,
            "この種類の曲線は、形を保ったまま戻せません。",
            "折れ線へ落として返すことはしません。"));
    }, "曲線の取り出し");
}

Result<std::vector<CurveSegment>> FromWire(const TopoDS_Wire& wire, double toleranceMm)
{
    using Out = Result<std::vector<CurveSegment>>;
    return Guarded([&]() -> Out {
        std::vector<CurveSegment> segments;
        for (TopExp_Explorer explorer(wire, TopAbs_EDGE); explorer.More();
            explorer.Next()) {
            const TopoDS_Edge edge = TopoDS::Edge(explorer.Current());
            auto converted = FromEdge(edge, toleranceMm);
            if (!converted.HasValue()) {
                return Out::Failure(converted.Diagnostics());
            }
            segments.push_back(converted.Value());
        }
        if (segments.empty()) {
            return Out::Failure(MakeError(kCurveUnsupported,
                "ワイヤーに辺がありません。", {}));
        }
        return Out::Success(std::move(segments));
    }, "ワイヤーの取り出し");
}

} // namespace kachakacha::v2::kernel

#endif
