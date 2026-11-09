// 面の編集(kernel/OcctSurfaceEdit.h)を、実際に核で作る。
//
// 合わせる・つなぐは、作ったあとで滑らかさを測って許容の内側であること、
// 整えるは制御点が減って元の面から許容の内側であること、対称は境目の折れ目を測ること、
// U/V 線と面へ投影は、面の上の曲線(折れ線でない)が出ることを見る。
// OCCT が無い版では中身は走らない(核の無い版は作らずに断る)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/ArcBuilders.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctSurfaceEdit.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctSurfaceExtrude.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepLib.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_Curve.hxx>
#include <GeomConvert.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BSplineCurve.hxx>
#include <ShapeFix_ShapeTolerance.hxx>
#include <TopExp.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <Geom_Surface.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Dir2d.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Pnt.hxx>
#include <gp_Ax3.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include "fixtures/inactive-c0-support.inc"
#endif

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::GuideChain;
using kachakacha::v2::modeling::GuideSurfaceMethod;
using kachakacha::v2::modeling::GuideSurfaceRequest;
using kachakacha::v2::modeling::GuideSurfaceResult;
using kachakacha::v2::modeling::KernelShapeHandle;
using kachakacha::v2::modeling::SurfaceContinuity;
using kachakacha::v2::test::Require;

namespace {

[[maybe_unused]] [[nodiscard]] GeometryTolerance Tolerance()
{
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = 1.0e-6;
    tolerance.interactiveJoinMm = 0.01;
    return tolerance;
}

[[maybe_unused]] [[nodiscard]] GuideChain Path(ChainRole role, int index,
    const std::vector<Vector3>& points)
{
    GuideChain chain;
    chain.role = role;
    chain.index = index;
    for (std::size_t at = 1; at < points.size(); ++at) {
        chain.segments.push_back(CurveSegment::MakeLine(points[at - 1], points[at]).Value());
    }
    return chain;
}

[[maybe_unused]] [[nodiscard]] GuideChain Curve(ChainRole role, int index, CurveSegment segment)
{
    GuideChain chain;
    chain.role = role;
    chain.index = index;
    chain.segments.push_back(std::move(segment));
    return chain;
}

template<class T>
[[maybe_unused]] [[nodiscard]] std::string Why(const kachakacha::v2::base::Result<T>& result)
{
    if (result.HasValue() || result.Diagnostics().empty()) {
        return {};
    }
    return result.Diagnostics().front().code + " " + result.Diagnostics().front().summaryJa
        + " / " + result.Diagnostics().front().detailsJa;
}

[[maybe_unused]] [[nodiscard]] KernelShapeHandle Build(const GuideSurfaceRequest& request)
{
    const auto analysis =
        kachakacha::v2::modeling::AnalyzeGuideSurfaceRequest(request, Tolerance());
    Require(analysis.HasValue(), "入力検査が通ること");
    const auto built =
        kachakacha::v2::kernel::BuildGuideSurface(request, analysis.Value(), Tolerance());
    Require(built.HasValue(), "面が作れること: " + Why(built));
    return built.Value().handle;
}

//! 平らな長方形(z 一定)。
[[maybe_unused]] [[nodiscard]] KernelShapeHandle FlatRectangle(double x0, double x1, double y0,
    double y1, double z)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    GuideChain outer = Path(ChainRole::OuterBoundary, 1,
        {{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}, {x0, y0, z}});
    outer.closed = true;
    request.chains.push_back(outer);
    return Build(request);
}

//! 支持面の縁 y = 0 から立ち上がる面(G0 のまま作る。縁で支持面から折れている)。
[[maybe_unused]] [[nodiscard]] KernelShapeHandle RisingSurface()
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::BoundaryFill;
    request.chains.push_back(Path(ChainRole::BoundarySide, 1, {{0, 0, 0}, {40, 0, 0}}));
    request.chains.push_back(Path(ChainRole::BoundarySide, 2, {{40, 0, 0}, {40, 30, 10}}));
    request.chains.push_back(Path(ChainRole::BoundarySide, 3, {{40, 30, 10}, {0, 30, 10}}));
    request.chains.push_back(Path(ChainRole::BoundarySide, 4, {{0, 30, 10}, {0, 0, 0}}));
    return Build(request);
}

//! 制御点の多い、なめらかな面(20 点の B-spline 断面 6 本のロフト)。
[[maybe_unused]] [[nodiscard]] KernelShapeHandle ManyPoleSurface()
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::LoftSections;
    for (int s = 0; s < 6; ++s) {
        const double x = s * 10.0;
        std::vector<Vector3> control;
        for (int k = 0; k < 20; ++k) {
            const double y = k * 2.0;
            control.push_back({x, y, 4.0 * std::sin(y / 12.0) + 0.02 * x * x / 10.0});
        }
        request.chains.push_back(
            Curve(ChainRole::Section, s + 1, CurveSegment::MakeCubicBSpline(control).Value()));
    }
    return Build(request);
}

[[maybe_unused]] [[nodiscard]] int EdgeNear(const KernelShapeHandle& surface, Vector3 point)
{
    const auto edge = kachakacha::v2::kernel::NearestSurfaceEdge(surface, point);
    Require(edge.HasValue(), "縁が拾える: " + Why(edge));
    Require(edge.Value().distanceMm < 1.0e-3, "押した点の近くの縁("
            + std::to_string(edge.Value().distanceMm) + " mm)");
    return edge.Value().index;
}

} // namespace

#ifndef KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST(kernel_surface_edit_absent, カーネルが無い版では作らずに断る)
{
    const auto built = kachakacha::v2::kernel::RefitSurface(KernelShapeHandle{}, 0.1, Tolerance());
    Require(!built.HasValue(), "作れたことにしない");
}

#else

KACHA_V2_TEST(kernel_surface_edit, 合わせるは縁を支持面へG1で合わせ元の面から動く)
{
    const auto support = FlatRectangle(0, 40, -20, 0, 0);
    const auto rising = RisingSurface();
    const int risingEdge = EdgeNear(rising, {20, 0, 0});
    const int supportEdge = EdgeNear(support, {20, 0, 0});
    const auto matched = kachakacha::v2::kernel::MatchSurfaceEdge(rising, risingEdge, support,
        supportEdge, SurfaceContinuity::G1, Tolerance());
    Require(matched.HasValue(), "合わせられる: " + Why(matched));
    Require(matched.Value().continuityG1Deg >= 0.0 && matched.Value().continuityG1Deg <= 1.5,
        "折れ目を測って許容の内側(" + std::to_string(matched.Value().continuityG1Deg) + " 度)");
    Require(matched.Value().deviationMm > 0.01, "元の面から動いている(形を合わせた)");
    Require(matched.Value().surface.handle.Valid(), "新しい面として登録されている");
}

KACHA_V2_TEST(kernel_surface_edit, 端が離れた縁どうしは合わせず理由を言う)
{
    const auto support = FlatRectangle(5, 45, -20, 0, 0);
    const auto rising = RisingSurface();
    const auto matched = kachakacha::v2::kernel::MatchSurfaceEdge(rising, EdgeNear(rising, {20, 0, 0}),
        support, EdgeNear(support, {20, 0, 0}), SurfaceContinuity::G1, Tolerance());
    Require(!matched.HasValue(), "合わせない");
    Require(matched.Diagnostics().front().code == "KER-D003", "端が離れていると言う: " + Why(matched));
}

KACHA_V2_TEST(kernel_surface_edit, つなぐは2本の縁のあいだをG1で渡し張りで形が変わる)
{
    const auto low = FlatRectangle(0, 40, -20, 0, 0);
    const auto high = FlatRectangle(0, 40, 20, 40, 10);
    const int a = EdgeNear(low, {20, 0, 0});
    const int b = EdgeNear(high, {20, 20, 10});
    const auto smooth = kachakacha::v2::kernel::BridgeSurfaceEdges(low, a, SurfaceContinuity::G1,
        high, b, SurfaceContinuity::G1, 1.0, Tolerance());
    Require(smooth.HasValue(), "つなげる: " + Why(smooth));
    Require(smooth.Value().continuityG1Deg <= 1.5, "両端とも折れ目が許容の内側("
            + std::to_string(smooth.Value().continuityG1Deg) + " 度)");
    const auto taut = kachakacha::v2::kernel::BridgeSurfaceEdges(low, a, SurfaceContinuity::G1,
        high, b, SurfaceContinuity::G1, 1.8, Tolerance());
    Require(taut.HasValue(), "張りを変えても作れる: " + Why(taut));
    Require(std::abs(taut.Value().surface.areaMm2 - smooth.Value().surface.areaMm2) > 1.0e-3,
        "張りが形に効く");
    const auto straight = kachakacha::v2::kernel::BridgeSurfaceEdges(low, a, SurfaceContinuity::G0,
        high, b, SurfaceContinuity::G0, 1.0, Tolerance());
    Require(straight.HasValue(), "G0 でもつなげる: " + Why(straight));
    Require(std::abs(straight.Value().surface.areaMm2 - smooth.Value().surface.areaMm2) > 1.0e-3,
        "G0 と G1 で形が違う");
}

KACHA_V2_TEST(kernel_surface_edit, 整えるは制御点を減らし元の面から許容の内側)
{
    const auto source = ManyPoleSurface();
    const auto refit = kachakacha::v2::kernel::RefitSurface(source, 0.05, Tolerance());
    Require(refit.HasValue(), "整えられる: " + Why(refit));
    Require(refit.Value().polesAfter > 0 && refit.Value().polesAfter < refit.Value().polesBefore,
        "制御点が減る(" + std::to_string(refit.Value().polesBefore) + " → "
            + std::to_string(refit.Value().polesAfter) + ")");
    Require(refit.Value().deviationMm <= 0.05, "元の面から許容の内側("
            + std::to_string(refit.Value().deviationMm) + " mm)");
}

KACHA_V2_TEST(kernel_surface_edit, refit_preserves_a_local_ridge_between_uniform_parameter_samples)
{
    namespace kernel = kachakacha::v2::kernel;
    // A piecewise cubic ridge is concentrated in V=.501..502 and Y=9.3..9.5.
    // Both the old 30-interval fit and 16-interval checks can miss it entirely.
    NCollection_Array2<gp_Pnt> poles(1, 4, 1, 10);
    const double ys[10]{0, 3.1, 6.2, 9.3, 9.3+.2/3, 9.3+.4/3, 9.5, 13, 16.5, 20};
    for (int i = 1; i <= 4; ++i) {
        for (int j = 1; j <= 10; ++j) {
            poles(i,j) = gp_Pnt(10.*(i-1)/3, ys[j-1], j == 5 || j == 6 ? 1.3 : 0);
        }
    }
    NCollection_Array1<double> uk(1,2), vk(1,4);
    uk(1) = 0; uk(2) = 1;
    vk(1) = 0; vk(2) = .501; vk(3) = .502; vk(4) = 1;
    NCollection_Array1<int> um(1,2), vm(1,4);
    um.Init(4); vm.Init(3); vm(1) = vm(4) = 4;
    const occ::handle<Geom_BSplineSurface> surface = new Geom_BSplineSurface(poles, uk, vk, um, vm, 3, 3);
    for (int i = 1; i < 60; ++i) {
        surface->InsertUKnot(i/60., 1, 1e-12, false);
        surface->InsertVKnot(i/60., 1, 1e-12, false);
    }
    const auto face = BRepBuilderAPI_MakeFace(surface, 1e-7).Face();
    Require(BRepCheck_Analyzer(face).IsValid(), "localized ridge is a valid native surface");
    const auto source = kernel::StoreShape(face);
    const auto before = kernel::CaptureOutputShape(source);
    const auto refit = kernel::RefitSurface(source, .01, Tolerance());
    Require(refit.HasValue(), "localized ridge can be refit: " + Why(refit));
    TopoDS_Shape native;
    Require(kernel::LookupShape(refit.Value().surface.handle, native), "refit native result exists");
    const auto resultFace = TopoDS::Face(TopExp_Explorer(native, TopAbs_FACE).Current());
    Require(surface->Value(.5, .5015).Z() > .9, "source contains a genuine local ridge");
    const double peakMoved = surface->Value(.5, .5015).Distance(
        BRep_Tool::Surface(resultFace)->Value(.5, .5015));
    Require(peakMoved <= .01,
        "the ridge remains within tolerance rather than silently becoming a plane");
    Require(refit.Value().polesAfter < refit.Value().polesBefore, "refit actually reduces redundant control points");
    Require(kernel::CaptureOutputShape(source).Value() == before.Value(), "authoritative source is byte-identical after refit");
    kernel::ReleaseShape(refit.Value().surface.handle);
    kernel::ReleaseShape(source);
}

KACHA_V2_TEST(kernel_surface_edit, refit_preserves_two_trimmed_holes_and_reversed_orientation)
{
    namespace kernel = kachakacha::v2::kernel;
    const auto support = ManyPoleSurface();
    TopoDS_Shape native;
    Require(kernel::LookupShape(support, native), "curved support exists");
    const auto face = TopoDS::Face(TopExp_Explorer(native, TopAbs_FACE).Current());
    const auto surface = BRep_Tool::Surface(face);
    double u0, u1, v0, v1;
    BRepTools::UVBounds(face, u0, u1, v0, v1);
    const auto uv = [&](double u, double v) { return gp_Pnt2d(u0 + (u1-u0)*u, v0 + (v1-v0)*v); };
    const auto hole = [&](const std::vector<gp_Pnt2d>& points) {
        BRepBuilderAPI_MakeWire wire;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto& a = points[i];
            const auto& b = points[(i + 1) % points.size()];
            const occ::handle<Geom2d_Curve> line = new Geom2d_Line(a,
                gp_Dir2d(b.X()-a.X(), b.Y()-a.Y()));
            BRepBuilderAPI_MakeEdge edge(line, surface, 0, a.Distance(b));
            Require(edge.IsDone(), "a trim edge can be created on the support");
            wire.Add(edge.Edge());
        }
        Require(wire.IsDone(), "a complete trim loop can be created");
        return wire.Wire();
    };
    BRepBuilderAPI_MakeFace maker(surface, u0, u1, v0, v1, 1e-7);
    maker.Add(hole({uv(.15,.15), uv(.15,.45), uv(.4,.45), uv(.4,.15)}));
    maker.Add(hole({uv(.6,.2), uv(.6,.8), uv(.85,.8), uv(.85,.6), uv(.72,.6), uv(.72,.2)}));
    TopoDS_Face trimmed = maker.Face();
    BRepLib::BuildCurves3d(trimmed);
    Require(BRepCheck_Analyzer(trimmed).IsValid(), "two holes, including a concave one, are valid");
    for (const auto orientation : {TopAbs_FORWARD, TopAbs_REVERSED}) {
        trimmed.Orientation(orientation);
        const auto source = kernel::StoreShape(trimmed);
        const auto before = kernel::CaptureOutputShape(source);
        const auto refit = kernel::RefitSurface(source, .05, Tolerance());
        Require(refit.HasValue(), "trimmed curved face can be simplified: " + Why(refit));
        TopoDS_Shape output;
        Require(kernel::LookupShape(refit.Value().surface.handle, output), "refit shape exists");
        const auto made = TopoDS::Face(TopExp_Explorer(output, TopAbs_FACE).Current());
        Require(made.Orientation() == orientation, "surface orientation is preserved");
        int loops = 0;
        for (TopExp_Explorer at(made, TopAbs_WIRE); at.More(); at.Next()) ++loops;
        Require(loops == 3, "outer loop and both holes remain distinct");
        const std::vector<std::pair<gp_Pnt2d, TopAbs_State>> probes{
            {uv(.25,.3),TopAbs_OUT}, {uv(.65,.5),TopAbs_OUT}, {uv(.8,.7),TopAbs_OUT},
            {uv(.8,.4),TopAbs_IN}, {uv(.5,.5),TopAbs_IN}, {uv(.95,.95),TopAbs_IN}};
        for (const auto& [point, expected] : probes) {
            Require(BRepClass_FaceClassifier(made, point, 1e-7).State() == expected,
                "material and openings, including the concave notch, remain in place");
        }
        Require(kernel::CaptureOutputShape(source).Value() == before.Value(),
            "the source's geometry and trim pcurves are byte-identical");
        kernel::ReleaseShape(refit.Value().surface.handle);
        kernel::ReleaseShape(source);
    }
    kernel::ReleaseShape(support);
}

KACHA_V2_TEST(kernel_surface_edit, refit_preserves_rational_curvature_and_a_periodic_seam)
{
    namespace kernel = kachakacha::v2::kernel;
    const double pi = std::acos(-1.0);
    for (const double sweep : {pi/2, 2*pi}) {
        const occ::handle<Geom_Surface> cylinder = new Geom_CylindricalSurface(
            gp_Ax3(gp_Pnt(10000,-20000,30000), gp_Dir(0,0,1)), 10);
        const occ::handle<Geom_Surface> bounded = new Geom_RectangularTrimmedSurface(
            cylinder, 0., sweep, 0., 20.);
        const auto surface = GeomConvert::SurfaceToBSplineSurface(bounded);
        if (sweep > pi) surface->SetUPeriodic();
        for (int i = 1; i < 40; ++i) {
            surface->InsertUKnot(sweep*i/40., 1, 1e-12, false);
            surface->InsertVKnot(20.*i/40, 1, 1e-12, false);
        }
        TopoDS_Face face = BRepBuilderAPI_MakeFace(surface, 1e-7).Face();
        BRep_Builder().NaturalRestriction(face, false); // Also exercise retained seam pcurves.
        Require(BRepCheck_Analyzer(face).IsValid(), "native rational surface is valid");
        const auto source = kernel::StoreShape(face);
        const auto before = kernel::CaptureOutputShape(source);
        const auto refit = kernel::RefitSurface(source, .01, Tolerance());
        Require(refit.HasValue(), "rational curved face can be simplified: " + Why(refit));
        TopoDS_Shape output;
        Require(kernel::LookupShape(refit.Value().surface.handle, output), "refit exists");
        const auto made = TopoDS::Face(TopExp_Explorer(output, TopAbs_FACE).Current());
        const auto fitted = BRep_Tool::Surface(made);
        for (int i = 0; i <= 80; ++i) {
            const double u = sweep*i/80.;
            Require(surface->Value(u, 10).Distance(fitted->Value(u, 10))
                    <= refit.Value().deviationMm + 1e-8,
                "reported conservative bound covers rational surface between knots");
        }
        Require(kernel::CaptureOutputShape(source).Value() == before.Value(),
            "rational weights, seam and source are unchanged");
        kernel::ReleaseShape(refit.Value().surface.handle);
        kernel::ReleaseShape(source);
    }
}

KACHA_V2_TEST(kernel_surface_edit, refit_never_drops_a_second_face_or_accepts_nonfinite_tolerance)
{
    namespace kernel = kachakacha::v2::kernel;
    const auto a = ManyPoleSurface(), b = FlatRectangle(0,10,0,10,50);
    TopoDS_Shape faceA, faceB;
    Require(kernel::LookupShape(a, faceA) && kernel::LookupShape(b, faceB), "both faces exist");
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    builder.Add(compound, faceA);
    builder.Add(compound, faceB);
    const auto source = kernel::StoreShape(compound);
    const auto before = kernel::CaptureOutputShape(source);
    const auto count = kernel::CachedShapeCount();
    const auto refit = kernel::RefitSurface(source, .05, Tolerance());
    Require(!refit.HasValue(), "unsupported compound is refused instead of returning only its first face");
    for (double tolerance : {0., -1., std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN()}) {
        Require(!kernel::RefitSurface(a, tolerance, Tolerance()).HasValue(),
            "non-positive and nonfinite tolerances cannot produce a shape");
    }
    Require(kernel::CachedShapeCount() == count, "failed operations do not leak native handles");
    Require(kernel::CaptureOutputShape(source).Value() == before.Value(), "failed refit leaves input intact");
    kernel::ReleaseShape(source);
    kernel::ReleaseShape(a);
    kernel::ReleaseShape(b);
}

KACHA_V2_TEST(kernel_surface_edit, refit_converts_a_trimmed_revolution_together_with_its_boundary)
{
    namespace kernel = kachakacha::v2::kernel;
    NCollection_Array1<gp_Pnt> poles(1, 4);
    poles(1) = gp_Pnt(8,0,0); poles(2) = gp_Pnt(9,0,6);
    poles(3) = gp_Pnt(12,0,13); poles(4) = gp_Pnt(10,0,20);
    NCollection_Array1<double> knots(1,2);
    knots(1) = 0; knots(2) = 1;
    NCollection_Array1<int> multiplicities(1,2);
    multiplicities.Init(4);
    const occ::handle<Geom_BSplineCurve> profile = new Geom_BSplineCurve(poles, knots, multiplicities, 3);
    for (int i = 1; i < 40; ++i) profile->InsertKnot(i/40., 1, 1e-12, false);
    const occ::handle<Geom_Surface> surface = new Geom_SurfaceOfRevolution(profile,
        gp_Ax1(gp_Pnt(0,0,0), gp_Dir(0,0,1)));
    BRepBuilderAPI_MakeFace maker(surface, 0., std::acos(-1.0)/2, 0., 1., 1e-7);
    const std::vector<gp_Pnt2d> points{{.2,.2}, {.2,.6}, {.6,.6}, {.6,.2}};
    BRepBuilderAPI_MakeWire wire;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& a = points[i];
        const auto& b = points[(i+1)%points.size()];
        const occ::handle<Geom2d_Curve> line = new Geom2d_Line(a, gp_Dir2d(b.X()-a.X(), b.Y()-a.Y()));
        wire.Add(BRepBuilderAPI_MakeEdge(line, surface, 0., a.Distance(b)).Edge());
    }
    maker.Add(wire.Wire());
    TopoDS_Face face = maker.Face();
    BRepLib::BuildCurves3d(face);
    Require(BRepCheck_Analyzer(face).IsValid(), "trimmed non-analytic revolution is valid");
    const auto source = kernel::StoreShape(face);
    const auto before = kernel::CaptureOutputShape(source);
    const auto refit = kernel::RefitSurface(source, .01, Tolerance());
    Require(refit.HasValue(), "angular surface and its trim are converted consistently: " + Why(refit));
    TopoDS_Shape output;
    Require(kernel::LookupShape(refit.Value().surface.handle, output), "refitted revolution exists");
    const auto made = TopoDS::Face(TopExp_Explorer(output, TopAbs_FACE).Current());
    Require(BRepClass_FaceClassifier(made, surface->Value(.4,.4), 1e-6).State() == TopAbs_OUT,
        "opening stays at its original 3D location after angular reparameterization");
    Require(BRepClass_FaceClassifier(made, surface->Value(1.,.8), 1e-6).State() == TopAbs_IN,
        "surrounding material remains");
    Require(kernel::CaptureOutputShape(source).Value() == before.Value(), "source revolution is unchanged");
    kernel::ReleaseShape(refit.Value().surface.handle);
    kernel::ReleaseShape(source);
}

KACHA_V2_TEST(kernel_surface_edit, refit_replaces_unneeded_large_trim_tolerances_only_after_checking_actual_boundary)
{
    namespace kernel = kachakacha::v2::kernel;
    for (int mode : {0, 1, 2}) {
        const auto support = ManyPoleSurface();
        TopoDS_Shape native;
        Require(kernel::LookupShape(support, native), "source native face exists");
        auto face = TopoDS::Face(TopExp_Explorer(native, TopAbs_FACE).Current());
        BRep_Builder().NaturalRestriction(face, false);
        ShapeFix_ShapeTolerance().SetTolerance(face, 17.0, TopAbs_WIRE);
        if (mode == 1) face.Reverse();
        if (mode == 2) {
            gp_Trsf tr;
            tr.SetRotation(gp_Ax1(gp_Pnt(0,0,0), gp_Dir(1,2,3)), .71);
            tr.SetTranslationPart(gp_Vec(12,-23,34));
            face = TopoDS::Face(BRepBuilderAPI_Transform(face, tr, true).Shape());
        }
        const auto source = kernel::StoreShape(face);
        const auto before = kernel::CaptureOutputShape(source);
        const auto refit = kernel::RefitSurface(source, .05, Tolerance());
        Require(refit.HasValue(), "valid trim survives refit: " + Why(refit));
        TopoDS_Shape output;
        Require(kernel::LookupShape(refit.Value().surface.handle, output), "refit native exists");
        TopExp_Explorer originalEdges(face, TopAbs_EDGE), newEdges(output, TopAbs_EDGE);
        for (; originalEdges.More() && newEdges.More(); originalEdges.Next(), newEdges.Next()) {
            const auto edge = TopoDS::Edge(newEdges.Current());
            Require(BRep_Tool::Tolerance(edge) <= .05 + Tolerance().modelLinearMm,
                "17 mm inherited tolerance is replaced by a measured bounded value");
            BRepAdaptor_Curve a(TopoDS::Edge(originalEdges.Current())), b(edge);
            for (double t : {0., .2, .5, .8, 1.}) {
                const double u = a.FirstParameter() + (a.LastParameter()-a.FirstParameter())*t;
                const double v = b.FirstParameter() + (b.LastParameter()-b.FirstParameter())*t;
                Require(a.Value(u).Distance(b.Value(v)) < 1e-9, "actual 3D boundary stays in place");
            }
        }
        Require(!originalEdges.More() && !newEdges.More(), "no boundary edges are dropped or added");
        Require(TopoDS::Face(TopExp_Explorer(output, TopAbs_FACE).Current()).Orientation() == face.Orientation(),
            "material-side orientation remains unchanged");
        Require(kernel::CaptureOutputShape(source).Value() == before.Value(), "source tolerances and geometry are byte-identical");
        kernel::ReleaseShape(refit.Value().surface.handle);
        kernel::ReleaseShape(source);
        kernel::ReleaseShape(support);
    }
}

KACHA_V2_TEST(kernel_surface_edit, refit_does_not_hide_a_real_boundary_error_behind_large_input_tolerance)
{
    namespace kernel = kachakacha::v2::kernel;
    const auto support = ManyPoleSurface();
    TopoDS_Shape native;
    Require(kernel::LookupShape(support, native), "source native face exists");
    auto face = TopoDS::Face(TopExp_Explorer(native, TopAbs_FACE).Current());
    BRep_Builder builder;
    builder.NaturalRestriction(face, false);
    ShapeFix_ShapeTolerance().SetTolerance(face, 17.0, TopAbs_WIRE);
    const auto edge = TopoDS::Edge(TopExp_Explorer(face, TopAbs_EDGE).Current());
    const auto vertex = TopExp::FirstVertex(edge);
    builder.UpdateVertex(vertex, BRep_Tool::Pnt(vertex).Translated(gp_Vec(0,0,1)), 17.0);
    const auto source = kernel::StoreShape(face);
    const auto before = kernel::CaptureOutputShape(source);
    const auto refit = kernel::RefitSurface(source, .05, Tolerance());
    Require(!refit.HasValue(), "a real 1 mm endpoint error cannot be called a .05 mm refit");
    Require(kernel::CaptureOutputShape(source).Value() == before.Value(), "refusal preserves the original exactly");
    kernel::ReleaseShape(source);
    kernel::ReleaseShape(support);
}

KACHA_V2_TEST(kernel_surface_edit, refit_retains_collapsed_pole_edges_without_lowering_their_unmeasured_allowances)
{
    namespace kernel = kachakacha::v2::kernel;
    const double pi = std::acos(-1.0);
    const occ::handle<Geom_Surface> sphere = new Geom_SphericalSurface(gp_Ax3(), 10);
    const auto surface = GeomConvert::SurfaceToBSplineSurface(
        new Geom_RectangularTrimmedSurface(sphere, 0., 2*pi, -pi/2, pi/2));
    for (int i=1; i<40; ++i) {
        surface->InsertUKnot(2*pi*i/40., 1, 1e-12, false);
        surface->InsertVKnot(-pi/2+pi*i/40., 1, 1e-12, false);
    }
    auto face = BRepBuilderAPI_MakeFace(surface, 1e-7).Face();
    BRep_Builder().NaturalRestriction(face, false);
    int collapsed=0;
    for (TopExp_Explorer e(face, TopAbs_EDGE); e.More(); e.Next())
        if (BRep_Tool::Degenerated(TopoDS::Edge(e.Current()))) ++collapsed;
    Require(collapsed==2, "sphere has two actual collapsed pole edges");
    const auto source = kernel::StoreShape(face);
    const auto before = kernel::CaptureOutputShape(source);
    const auto refit = kernel::RefitSurface(source, .01, Tolerance());
    Require(refit.HasValue(), "rational sphere keeps its pole topology: " + Why(refit));
    TopoDS_Shape output;
    Require(kernel::LookupShape(refit.Value().surface.handle, output), "sphere refit exists");
    Require(BRepCheck_Analyzer(output).IsValid(), "collapsed boundaries remain consistent");
    Require(kernel::CaptureOutputShape(source).Value()==before.Value(), "original poles and allowances are unchanged");
    kernel::ReleaseShape(refit.Value().surface.handle);
    kernel::ReleaseShape(source);
}

KACHA_V2_TEST(kernel_surface_edit, refit_trimmed_curved_panel_allows_safe_directional_extrusion_and_step_output)
{
    namespace kernel = kachakacha::v2::kernel;
    std::ifstream input(std::filesystem::path(KACHACAD_TEST_FIXTURE_DIR)/"trimmed-curved-panel-integration.brep");
    Require(input.good(), "actual trimmed curved panel fixture is available");
    TopoDS_Shape shape;
    BRepTools::Read(shape, input, BRep_Builder());
    const auto source = kernel::StoreShape(shape);
    const auto before = kernel::CaptureOutputShape(source);
    const auto refit = kernel::RefitSurface(source, .05, Tolerance());
    Require(refit.HasValue(), "actual trimmed panel can be simplified: " + Why(refit));
    Require(refit.Value().deviationMm < .000074, "complete surface deviation remains within the measured bound");
    const auto extrusion = kernel::BuildSurfaceExtrude(refit.Value().surface.handle, {-1,1,-1}, 0, .3, Tolerance());
    Require(extrusion.HasValue() && extrusion.Value().parts.size()==1, "the panel produces one closed directional prism");
    const auto solid = extrusion.Value().parts.front().handle;
    const auto checked = kernel::CheckSolidForExport(solid, Tolerance().modelLinearMm);
    Require(checked.HasValue() && checked.Value().Ok(), "independent closure, mass and self-intersection checks all succeed");
    const auto step = kernel::BuildStepText(solid, Tolerance().modelLinearMm);
    Require(step.HasValue() && step.Value().find("ISO-10303-21")!=std::string::npos,
        "the same exact native solid can be exported to STEP: " + Why(step));
    Require(kernel::CaptureOutputShape(source).Value()==before.Value(), "refit, extrusion and export preserve the original byte-for-byte");
    kernel::ReleaseShape(solid);
    kernel::ReleaseShape(refit.Value().surface.handle);
    kernel::ReleaseShape(source);
}

KACHA_V2_TEST(kernel_surface_edit, 平面は整え直さず理由を言う)
{
    const auto refit = kachakacha::v2::kernel::RefitSurface(FlatRectangle(0, 10, 0, 10, 0), 0.05,
        Tolerance());
    Require(!refit.HasValue() && refit.Diagnostics().front().code == "KER-D006",
        "平面はいちばん簡単な形だと言う: " + Why(refit));
}

KACHA_V2_TEST(kernel_surface_edit, 対称は境目の折れ目を測り直角なら滑らか)
{
    // z = 0 の長方形で、縁 y = 0 が対称面(Y = 0)に乗っている。面は対称面に直角。
    const auto flat = FlatRectangle(0, 40, 0, 20, 0);
    const auto mirrored = kachakacha::v2::kernel::MirrorSurface(flat, {0, 0, 0}, {0, 1, 0},
        Tolerance());
    Require(mirrored.HasValue(), "写せる: " + Why(mirrored));
    Require(mirrored.Value().continuityG1Deg >= 0.0 && mirrored.Value().continuityG1Deg < 0.01,
        "境目が折れていない(" + std::to_string(mirrored.Value().continuityG1Deg) + " 度)");
    // 傾いた面(z = y / 3)は、対称面で 2 × 18.4 度 ≒ 36.9 度折れる。
    GuideSurfaceRequest tilted;
    tilted.method = GuideSurfaceMethod::PlanarBoundary;
    GuideChain outer = Path(ChainRole::OuterBoundary, 1,
        {{0, 0, 0}, {40, 0, 0}, {40, 30, 10}, {0, 30, 10}, {0, 0, 0}});
    outer.closed = true;
    tilted.chains.push_back(outer);
    const auto bent = kachakacha::v2::kernel::MirrorSurface(Build(tilted), {0, 0, 0}, {0, 1, 0},
        Tolerance());
    Require(bent.HasValue(), "傾いた面も写せる: " + Why(bent));
    Require(std::abs(bent.Value().continuityG1Deg - 36.87) < 0.5,
        "境目の折れ目を測る(" + std::to_string(bent.Value().continuityG1Deg) + " 度)");
    // 対称面から離れた面は、境目が無いと言う。
    const auto apart = kachakacha::v2::kernel::MirrorSurface(FlatRectangle(0, 10, 5, 10, 0),
        {0, 0, 0}, {0, 1, 0}, Tolerance());
    Require(apart.HasValue() && apart.Value().continuityG1Deg < 0.0, "境目は無い");
}

KACHA_V2_TEST(kernel_surface_edit, UV線は面の内側の線として取り出せる)
{
    const auto flat = FlatRectangle(0, 40, 0, 20, 0);
    const auto curves = kachakacha::v2::kernel::ExtractIsoCurves(flat, 2, 3, Tolerance());
    Require(curves.HasValue(), "取り出せる: " + Why(curves));
    Require(curves.Value().size() == 6, "U 3 本と V 3 本(" + std::to_string(curves.Value().size()) + ")");
    for (const auto& wire : curves.Value()) {
        const double length = (wire.back().EndPoint() - wire.front().StartPoint()).Length();
        Require(std::abs(length - 40.0) < 1.0e-3 || std::abs(length - 20.0) < 1.0e-3,
            "面を端から端まで渡る(" + std::to_string(length) + ")");
    }
}

KACHA_V2_TEST(kernel_surface_edit, 面へ投影した線は面の上に乗る)
{
    const auto flat = FlatRectangle(0, 40, 0, 20, 0);
    const std::vector<std::vector<CurveSegment>> wires{
        {kachakacha::v2::geometry::ArcThroughThreePoints({10, 5, 10}, {20, 12, 10}, {30, 5, 10}).Value()}};
    const auto projected = kachakacha::v2::kernel::ProjectWiresOntoSurface(flat, wires, {0, 0, -1},
        Tolerance());
    Require(projected.HasValue(), "落とせる: " + Why(projected));
    for (const auto& wire : projected.Value()) {
        for (const auto& segment : wire) {
            for (int k = 0; k <= 10; ++k) {
                Require(std::abs(segment.Evaluate(k / 10.0).z) < 1.0e-4, "面(z = 0)の上");
            }
        }
    }
}

KACHA_V2_TEST(kernel_surface_edit, cropped_support_refit_and_offset_preserve_active_geometry_and_holes)
{
    namespace kernel=kachakacha::v2::kernel;
    for(bool rational:{false,true})for(bool transformed:{false,true})for(bool nearKnot:{false,true})
    for(double vMinimum:{0.,-3e-15}){
        auto face=kachakacha_test_fixture::InactiveC0Support(false,rational,true,1,nearKnot,vMinimum);
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        Require(vMinimum==0||v0<0,"fixture keeps the trim's roundoff beyond the basis domain");
        if(transformed){
            gp_Trsf pose;pose.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.61);
            pose.SetTranslationPart(gp_Vec(11,-7,4));
            face=TopoDS::Face(BRepBuilderAPI_Transform(face,pose,true).Shape());
            face.Reverse();
        }
        const auto source=kernel::StoreShape(face);
        const auto before=kernel::CaptureOutputShape(source).Value();
        const auto refit=kernel::RefitSurface(source,.001,Tolerance());
        Require(refit.HasValue(),"active smooth patch is simplified despite unused crease: "+Why(refit));
        Require(refit.Value().polesAfter>0&&refit.Value().polesAfter<refit.Value().polesBefore,
            "unused support data is removed");
        Require(refit.Value().deviationMm<=.001,"complete active-domain error remains bounded");
        GuideSurfaceRequest request;request.method=GuideSurfaceMethod::OffsetGuide;request.offsetDistanceMm=.05;
        GuideChain chain;chain.role=ChainRole::SourceSurface;request.chains.push_back(chain);
        const auto analysis=kachakacha::v2::modeling::AnalyzeGuideSurfaceRequest(request,Tolerance());
        Require(analysis.HasValue(),"offset request accepted");
        for(const auto input:{source,refit.Value().surface.handle}){
            const auto offset=kernel::BuildGuideSurface(request,analysis.Value(),Tolerance(),input);
            Require(offset.HasValue(),"direct and refitted active patches offset: "+Why(offset));
            TopoDS_Shape native;Require(kernel::LookupShape(offset.Value().handle,native),"offset shape retained");
            const auto made=TopoDS::Face(TopExp_Explorer(native,TopAbs_FACE).Current());
            Require(BRepCheck_Analyzer(made).IsValid()&&made.Orientation()==face.Orientation(),
                "valid trim and orientation survive");
            int loops=0;for(TopExp_Explorer at(made,TopAbs_WIRE);at.More();at.Next())++loops;
            Require(loops==3,"both holes survive direct and post-refit offset");
            for(const auto point:{gp_Pnt2d(1.34,.3),gp_Pnt2d(1.66,.7)})
                Require(BRepClass_FaceClassifier(made,point,1e-7).State()==TopAbs_OUT,"hole is empty");
            Require(BRepClass_FaceClassifier(made,gp_Pnt2d(1.5,.5),1e-7).State()==TopAbs_IN,
                "material between holes remains");
            BRepAdaptor_Surface original(face),shifted(made);
            for(int i=0;i<=16;++i){
                const double u=1.2+.6*i/16.,v=.5;
                gp_Pnt p;gp_Vec du,dv;original.D1(u,v,p,du,dv);
                auto normal=du.Crossed(dv).Normalized();
                if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
                Require(shifted.Value(u,v).Distance(p.Translated(normal*.05))<.001,
                    "offset is from the original active support, with no reparameterization");
            }
            kernel::ReleaseShape(offset.Value().handle);
        }
        Require(kernel::CaptureOutputShape(source).Value()==before,
            "source geometry, trims and allowances remain byte-identical");
        kernel::ReleaseShape(refit.Value().surface.handle);kernel::ReleaseShape(source);
    }
}

KACHA_V2_TEST(kernel_surface_edit, active_crease_is_not_removed_by_cropping_support)
{
    namespace kernel=kachakacha::v2::kernel;
    const auto source=kernel::StoreShape(kachakacha_test_fixture::InactiveC0Support(true,false,false));
    const auto before=kernel::CaptureOutputShape(source).Value();
    Require(!kernel::RefitSurface(source,1e-6,Tolerance()).HasValue(),"genuine active crease cannot be silently simplified");
    GuideSurfaceRequest request;request.method=GuideSurfaceMethod::OffsetGuide;request.offsetDistanceMm=.05;
    GuideChain chain;chain.role=ChainRole::SourceSurface;request.chains.push_back(chain);
    const auto analysis=kachakacha::v2::modeling::AnalyzeGuideSurfaceRequest(request,Tolerance());
    Require(analysis.HasValue(),"negative offset test input is valid");
    Require(!kernel::BuildGuideSurface(request,analysis.Value(),Tolerance(),source).HasValue(),
        "crease within active domain retains offset refusal");
    Require(kernel::CaptureOutputShape(source).Value()==before,"refused operations preserve source bytes");
    kernel::ReleaseShape(source);
}

KACHA_V2_TEST(kernel_surface_edit, real_trim_extension_beyond_support_is_not_hidden_as_roundoff)
{
    namespace kernel=kachakacha::v2::kernel;
    const auto source=kernel::StoreShape(
        kachakacha_test_fixture::InactiveC0Support(false,true,true,1,false,-.001));
    const auto before=kernel::CaptureOutputShape(source).Value();
    Require(!kernel::RefitSurface(source,.001,Tolerance()).HasValue(),
        "a real extension outside the original support cannot be silently clamped");
    Require(kernel::CaptureOutputShape(source).Value()==before,"refusal leaves the actual trim unchanged");
    kernel::ReleaseShape(source);
}

#endif // KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST_MAIN("kernel_surface_edit_tests")
