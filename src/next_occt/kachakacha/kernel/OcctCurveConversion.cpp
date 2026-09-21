#include "kachakacha/kernel/OcctCurveConversion.h"

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/geometry/CurveFit.h"
#include "kachakacha/geometry/Units.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools_WireExplorer.hxx>
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
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
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
            // core の B-spline は「節点が等間隔で端を重ねない」一様 3 次(区間ごとに
            // 制御点 4 つ、端の制御点は通らない)。区間ごとの 3 次ベジェにすれば、同じ形を
            // 核の B-spline で厳密に表せる。**制御点をそのまま端を重ねた B-spline へ
            // 写すと形が変わる**(2026-09-22 まではそうしていて、core で見える線と核が
            // 使う線が食い違っていた)。
            const auto spans = geometry::UniformBSplineBezierSpans(segment.ControlPoints());
            if (spans.empty()) {
                return Out::Failure(MakeError(kCurveUnsupported,
                    "B-splineの制御点が足りません。",
                    std::to_string(segment.ControlPoints().size()) + " 点でした。4点以上必要です。"));
            }
            const int count = static_cast<int>(spans.size());
            NCollection_Array1<gp_Pnt> points(1, 3 * count + 1);
            points.SetValue(1, ToPoint(spans.front()[0]));
            for (int span = 0; span < count; ++span) {
                const auto& bezier = spans[static_cast<std::size_t>(span)];
                points.SetValue(3 * span + 2, ToPoint(bezier[1]));
                points.SetValue(3 * span + 3, ToPoint(bezier[2]));
                points.SetValue(3 * span + 4, ToPoint(bezier[3]));
            }
            NCollection_Array1<double> knots(1, count + 1);
            NCollection_Array1<int> multiplicities(1, count + 1);
            for (int index = 1; index <= count + 1; ++index) {
                knots.SetValue(index, static_cast<double>(index - 1));
                multiplicities.SetValue(index, 3);
            }
            multiplicities.SetValue(1, 4);
            multiplicities.SetValue(count + 1, 4);
            occ::handle<Geom_BSplineCurve> spline =
                new Geom_BSplineCurve(points, knots, multiplicities, 3);
            // 内側の節点は元が C2 なので、重なりを 1 まで外せる(形は変わらない)。
            for (int index = 2; index <= count; ++index) {
                (void)spline->RemoveKnot(index, 1, Precision::Confusion());
            }
            return Out::Success(occ::handle<Geom_Curve>(spline));
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

namespace {

//! 節点が等間隔で、両端の重なりが 4、内側が 1 の 3 次(core の一様 3 次と同じ形の空間)。
[[nodiscard]] bool UniformClampedCubic(const occ::handle<Geom_BSplineCurve>& spline)
{
    if (spline->Degree() != 3 || spline->IsRational() || spline->IsPeriodic()) {
        return false;
    }
    const int knots = spline->NbKnots();
    if (knots < 2 || spline->Multiplicity(1) != 4 || spline->Multiplicity(knots) != 4) {
        return false;
    }
    const double step = (spline->Knot(knots) - spline->Knot(1)) / (knots - 1);
    for (int index = 2; index < knots; ++index) {
        if (spline->Multiplicity(index) != 1
            || std::abs(spline->Knot(index) - spline->Knot(1) - step * (index - 1))
                > 1.0e-9 * std::max(1.0, std::abs(step))) {
            return false;
        }
    }
    return true;
}

//! 辺の曲線を、値に合わせて core の一様 3 次 B-spline にする(両端は通す)。
[[nodiscard]] Result<CurveSegment> FitToCore(const occ::handle<Geom_Curve>& curve, double first,
    double last, bool reversed, double toleranceMm, std::size_t preferred)
{
    const auto at = [&](double t) {
        const double u = reversed ? last - (last - first) * t : first + (last - first) * t;
        return FromPoint(curve->Value(u));
    };
    // 許容は core の長さの許容(1e-6 mm)より粗く取る。表示・展開・輪郭として十分で、
    // これより細かくすると、近似で作った面の縁(節点が不揃い)が写せなくなる。
    const auto fit = geometry::FitUniformCubicBSpline(at, std::max(toleranceMm, 1.0e-4),
        preferred, 512);
    if (!fit.HasValue()) {
        return Result<CurveSegment>::Failure(fit.Diagnostics());
    }
    return Result<CurveSegment>::Success(fit.Value().curve);
}

} // namespace

Result<CurveSegment> FromEdge(const TopoDS_Edge& edge, double toleranceMm)
{
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
            if (bezier->Degree() != 3 || bezier->IsRational()) {
                return FitToCore(curve, first, last, reversed, toleranceMm, 0);
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
            // 制御点をそのまま写すと、節点の間隔や端の重なりが違って形が変わる。
            // 値に合わせて core の一様 3 次へ写す。同じ間隔・端を重ねた 3 次
            // (ToGeomCurve が作る形)なら、制御点の数を同じにして厳密に戻る。
            occ::handle<Geom_BSplineCurve> spline =
                occ::handle<Geom_BSplineCurve>::DownCast(basis);
            const bool whole = std::abs(first - spline->FirstParameter()) <= 1.0e-12
                && std::abs(last - spline->LastParameter()) <= 1.0e-12;
            const std::size_t preferred = whole && UniformClampedCubic(spline)
                ? static_cast<std::size_t>(spline->NbPoles())
                : 0;
            return FitToCore(curve, first, last, reversed, toleranceMm, preferred);
        }
        // そのほかの曲線(楕円・オフセット曲線など)も、値に合わせて写す。
        // 許容に届かなければ断る(折れ線へ落として「できた」ことにしない)。
        return FitToCore(curve, first, last, reversed, toleranceMm, 0);
    }, "曲線の取り出し");
}

Result<std::vector<CurveSegment>> FromWire(const TopoDS_Wire& wire, double toleranceMm)
{
    using Out = Result<std::vector<CurveSegment>>;
    return Guarded([&]() -> Out {
        std::vector<CurveSegment> segments;
        // **繋がった順に** 取り出す。TopExp_Explorer は位相の並びで返すので、
        // 端どうしが繋がらない順になることがある。そのまま渡すと、
        // 戻した線を輪郭として使うときに KER-C003「線がつながっていません」で断られる。
        // 立体の面の縁を取り出して押し出す道(EX-02)で実際に起きた。
        for (BRepTools_WireExplorer explorer(wire); explorer.More(); explorer.Next()) {
            auto converted = FromEdge(explorer.Current(), toleranceMm);
            if (!converted.HasValue()) {
                return Out::Failure(converted.Diagnostics());
            }
            segments.push_back(converted.Value());
        }
        if (segments.empty()) {
            // 繋がった順に歩けない形(退化した辺など)。位相の並びで取り直す。
            // 順は保証できないが、黙って空を返すよりはよい。
            for (TopExp_Explorer explorer(wire, TopAbs_EDGE); explorer.More();
                explorer.Next()) {
                auto converted = FromEdge(TopoDS::Edge(explorer.Current()), toleranceMm);
                if (!converted.HasValue()) {
                    return Out::Failure(converted.Diagnostics());
                }
                segments.push_back(converted.Value());
            }
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
