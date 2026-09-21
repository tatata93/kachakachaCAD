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
#include <string>
#include <utility>
#include <vector>

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

#endif // KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST_MAIN("kernel_surface_edit_tests")
