// 面の解析の標本(kernel/OcctSurfaceAnalysis.h)を、実際の面で測る。
//
// 平面・円筒・円錐はガウス曲率 0(ほぼ可展)、球は 1/R²(強い二重曲率)。円筒の平均曲率は
// 1/(2R)。隣り合う面の境目は、同じ平面なら G2、直角なら G0。
#include "kachakacha/app/SurfaceAnalysis.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctSurfaceAnalysis.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#endif

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::app::ClassifyDevelopability;
using kachakacha::v2::app::ContinuityGrade;
using kachakacha::v2::app::DevelopabilityClass;
using kachakacha::v2::app::GradeContinuity;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::GuideChain;
using kachakacha::v2::modeling::GuideSurfaceMethod;
using kachakacha::v2::modeling::GuideSurfaceRequest;
using kachakacha::v2::modeling::KernelShapeHandle;
using kachakacha::v2::modeling::SurfaceAnalysisData;
using kachakacha::v2::test::Require;

namespace {

[[maybe_unused]] [[nodiscard]] GeometryTolerance Tolerance()
{
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = 1.0e-6;
    tolerance.interactiveJoinMm = 0.01;
    return tolerance;
}

[[maybe_unused]] [[nodiscard]] KernelShapeHandle Rectangle(const std::vector<Vector3>& corners)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    GuideChain outer;
    outer.role = ChainRole::OuterBoundary;
    outer.index = 1;
    outer.closed = true;
    for (std::size_t k = 0; k < corners.size(); ++k) {
        outer.segments.push_back(
            CurveSegment::MakeLine(corners[k], corners[(k + 1) % corners.size()]).Value());
    }
    request.chains.push_back(outer);
    const auto analysis = kachakacha::v2::modeling::AnalyzeGuideSurfaceRequest(request, Tolerance());
    Require(analysis.HasValue(), "入力検査が通ること");
    const auto built = kachakacha::v2::kernel::BuildGuideSurface(request, analysis.Value(), Tolerance());
    Require(built.HasValue(), "面が作れること");
    return built.Value().handle;
}

[[maybe_unused]] [[nodiscard]] double MaxAbs(const SurfaceAnalysisData& data, bool gaussian)
{
    double worst = 0.0;
    for (const auto& triangle : data.triangles) {
        for (int k = 0; k < 3; ++k) {
            worst = std::max(worst, std::abs(gaussian ? triangle.gaussian[static_cast<std::size_t>(k)]
                                                      : triangle.mean[static_cast<std::size_t>(k)]));
        }
    }
    return worst;
}

} // namespace

#ifndef KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST(kernel_surface_analysis_absent, カーネルが無い版では解析せずに断る)
{
    Require(!kachakacha::v2::kernel::AnalyzeSurfaceShape(KernelShapeHandle{}).HasValue(),
        "解析できたことにしない");
}

#else

namespace {

[[nodiscard]] KernelShapeHandle FaceOfType(const TopoDS_Shape& shape, GeomAbs_SurfaceType type)
{
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const TopoDS_Face face = TopoDS::Face(explorer.Current());
        if (BRepAdaptor_Surface(face).GetType() == type) {
            return kachakacha::v2::kernel::StoreShape(face);
        }
    }
    return KernelShapeHandle{};
}

[[nodiscard]] SurfaceAnalysisData Analyze(const KernelShapeHandle& handle)
{
    const auto data = kachakacha::v2::kernel::AnalyzeSurfaceShape(handle, 30, 6);
    Require(data.HasValue(), "解析できる");
    return data.Value();
}

} // namespace

KACHA_V2_TEST(kernel_surface_analysis, 平面と円筒と円錐はガウス曲率0でほぼ可展)
{
    const auto plane = Analyze(FaceOfType(BRepPrimAPI_MakeBox(40, 30, 20).Shape(), GeomAbs_Plane));
    const auto cylinder = Analyze(FaceOfType(BRepPrimAPI_MakeCylinder(20, 40).Shape(), GeomAbs_Cylinder));
    const auto cone = Analyze(FaceOfType(BRepPrimAPI_MakeCone(20, 5, 30).Shape(), GeomAbs_Cone));
    for (const auto* data : {&plane, &cylinder, &cone}) {
        Require(!data->Empty(), "標本がある");
        Require(ClassifyDevelopability(*data).kind == DevelopabilityClass::NearlyDevelopable,
            "ほぼ可展(|K| 最大 " + std::to_string(MaxAbs(*data, true)) + ")");
    }
    Require(std::abs(MaxAbs(cylinder, false) - 1.0 / 40.0) < 1.0e-6,
        "円筒の平均曲率は 1/(2R)(" + std::to_string(MaxAbs(cylinder, false)) + ")");
    Require(!cylinder.isoLines.empty() && !cylinder.combs.empty(), "U/V 線と曲率コームがある");
}

KACHA_V2_TEST(kernel_surface_analysis, 球はガウス曲率が1割るR2乗で強い二重曲率)
{
    const auto sphere = Analyze(FaceOfType(BRepPrimAPI_MakeSphere(50).Shape(), GeomAbs_Sphere));
    Require(std::abs(MaxAbs(sphere, true) - 1.0 / 2500.0) < 1.0e-7,
        "K = 1/R²(" + std::to_string(MaxAbs(sphere, true)) + ")");
    Require(ClassifyDevelopability(sphere).kind == DevelopabilityClass::StronglyDoubleCurved,
        "強い二重曲率");
}

KACHA_V2_TEST(kernel_surface_analysis, 同じ平面の隣はG2で直角の隣はG0)
{
    const auto base = Rectangle({{0, 0, 0}, {20, 0, 0}, {20, 20, 0}, {0, 20, 0}});
    const auto flat = Rectangle({{20, 0, 0}, {40, 0, 0}, {40, 20, 0}, {20, 20, 0}});
    const auto wall = Rectangle({{0, 0, 0}, {0, 20, 0}, {0, 20, 20}, {0, 0, 20}});
    const auto report = kachakacha::v2::kernel::SurfaceEdgeContinuity(base, {flat, wall}, Tolerance());
    Require(report.HasValue() && report.Value().size() == 4, "縁 4 本を測る");
    int g2 = 0;
    int g0 = 0;
    int open = 0;
    for (const auto& sample : report.Value()) {
        const auto grade = GradeContinuity(sample);
        g2 += grade == ContinuityGrade::G2 ? 1 : 0;
        g0 += grade == ContinuityGrade::G0 ? 1 : 0;
        open += grade == ContinuityGrade::Open ? 1 : 0;
    }
    Require(g2 == 1 && g0 == 1 && open == 2, "G2 が 1、G0 が 1、開いた縁が 2(G2 "
            + std::to_string(g2) + " G0 " + std::to_string(g0) + " 開 " + std::to_string(open) + ")");
}

KACHA_V2_TEST(kernel_surface_analysis, 面を作った線の上はずれが0)
{
    const auto base = Rectangle({{0, 0, 0}, {20, 0, 0}, {20, 20, 0}, {0, 20, 0}});
    const std::vector<std::vector<CurveSegment>> chains{
        {CurveSegment::MakeLine({0, 0, 0}, {20, 0, 0}).Value()},
        {CurveSegment::MakeLine({0, 10, 1}, {20, 10, 1}).Value()}};
    const auto deviation = kachakacha::v2::kernel::DeviationFromChains(base, chains);
    Require(deviation.HasValue() && deviation.Value().size() == 2, "線 2 本を測る");
    for (const double d : deviation.Value()[0].distancesMm) {
        Require(d < 1.0e-6, "縁の上は 0");
    }
    for (const double d : deviation.Value()[1].distancesMm) {
        Require(std::abs(d - 1.0) < 1.0e-6, "1 mm 浮いた線は 1 mm");
    }
}

#endif // KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST_MAIN("kernel_surface_analysis_tests")
