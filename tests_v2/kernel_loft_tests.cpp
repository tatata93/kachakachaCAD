// ロフト(断面 2〜任意 + ガイド 0〜任意 + 中心線 0〜1)と四辺面を、実際に核で作る。
//
// OCCT が無い版ではこの試験の中身は走らない(核の無い版は面を作らずに断る。
// それは kernel_surface_tests の kernel_absent が見る)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/ArcBuilders.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::kernel::BuildGuideSurface;
using kachakacha::v2::modeling::AnalyzeGuideSurfaceRequest;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::FourEdgeStyle;
using kachakacha::v2::modeling::GuideChain;
using kachakacha::v2::modeling::GuideSurfaceMethod;
using kachakacha::v2::modeling::GuideSurfaceRequest;
using kachakacha::v2::modeling::GuideSurfaceResult;
using kachakacha::v2::modeling::LoftSolver;
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

[[maybe_unused]] [[nodiscard]] GuideChain Arc(ChainRole role, int index, Vector3 a, Vector3 m,
    Vector3 b)
{
    GuideChain chain;
    chain.role = role;
    chain.index = index;
    chain.segments.push_back(kachakacha::v2::geometry::ArcThroughThreePoints(a, m, b).Value());
    return chain;
}

//! 断面は x 方向に並ぶ円弧(y = 0..40 を渡る山形)、ガイドは y = 一定の円弧。
//! 断面とガイドは格子点で交わる。bulge はガイドの真ん中の持ち上げ(3 本目を動かす試験用)。
[[maybe_unused]] [[nodiscard]] double Height(double x, double y)
{
    return 12.0 * std::sin(3.14159265358979323846 * y / 40.0) * (1.0 - 0.3 * x / 100.0);
}

[[maybe_unused]] [[nodiscard]] GuideSurfaceRequest Nose(int sections,
    const std::vector<double>& railYs, double interiorBulge = 0.0)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::LoftSections;
    std::vector<double> xs;
    for (int s = 0; s < sections; ++s) {
        xs.push_back(100.0 * s / (sections - 1));
    }
    for (int s = 0; s < sections; ++s) {
        const double x = xs[static_cast<std::size_t>(s)];
        request.chains.push_back(Arc(ChainRole::Section, s + 1, {x, 0, Height(x, 0)},
            {x, 20, Height(x, 20)}, {x, 40, Height(x, 40)}));
    }
    for (std::size_t r = 0; r < railYs.size(); ++r) {
        const double y = railYs[r];
        // ガイドは各断面の上の同じ y の点を通る折れ線。内側のガイドは断面の間で持ち上げる。
        std::vector<Vector3> points;
        const bool interior = y > 0.0 && y < 40.0;
        for (std::size_t s = 0; s < xs.size(); ++s) {
            // 断面は円弧なので、断面の上の y の点は円弧から取る。
            const auto& arc = request.chains[s].segments.front();
            const auto closest = arc.ClosestPoint({xs[s], y, 0.0});
            (void)closest;
            // 円弧の上で y が一致する点を二分法で探す。
            double lo = 0.0;
            double hi = 1.0;
            for (int i = 0; i < 60; ++i) {
                const double mid = (lo + hi) * 0.5;
                (arc.Evaluate(mid).y < y ? lo : hi) = mid;
            }
            points.push_back(arc.Evaluate((lo + hi) * 0.5));
            if (interior && s + 1 < xs.size() && interiorBulge != 0.0) {
                const auto& next = request.chains[s + 1].segments.front();
                double lo2 = 0.0;
                double hi2 = 1.0;
                for (int i = 0; i < 60; ++i) {
                    const double mid = (lo2 + hi2) * 0.5;
                    (next.Evaluate(mid).y < y ? lo2 : hi2) = mid;
                }
                const Vector3 a = points.back();
                const Vector3 b = next.Evaluate((lo2 + hi2) * 0.5);
                points.push_back((a + b) * 0.5 + Vector3{0, 0, interiorBulge});
            }
        }
        request.chains.push_back(Path(ChainRole::GuideU, static_cast<int>(r) + 1, points));
    }
    return request;
}

[[maybe_unused]] [[nodiscard]] kachakacha::v2::base::Result<GuideSurfaceResult> Build(
    const GuideSurfaceRequest& request)
{
    const auto analysis = AnalyzeGuideSurfaceRequest(request, Tolerance());
    Require(analysis.HasValue(), "入力検査が通ること ("
            + (analysis.Diagnostics().empty() ? std::string()
                                              : analysis.Diagnostics().front().summaryJa)
            + ")");
    return BuildGuideSurface(request, analysis.Value(), Tolerance());
}

[[maybe_unused]] [[nodiscard]] std::string Why(
    const kachakacha::v2::base::Result<GuideSurfaceResult>& result)
{
    if (result.HasValue() || result.Diagnostics().empty()) {
        return {};
    }
    return result.Diagnostics().front().summaryJa + " / " + result.Diagnostics().front().detailsJa;
}

} // namespace

#ifdef KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST(kernel_loft, ガイド無しの2断面ロフトは断面を通る)
{
    const auto built = Build(Nose(2, {}));
    Require(built.HasValue(), "作れる: " + Why(built));
    Require(built.Value().maximumDeviationMm <= 1.0e-3, "断面の上に乗る");
}

KACHA_V2_TEST(kernel_loft, 両端のガイド2本と3断面は従来どおり作れる)
{
    const auto built = Build(Nose(3, {0.0, 40.0}));
    Require(built.HasValue(), "作れる(回帰): " + Why(built));
    Require(built.Value().maximumDeviationMm <= 0.25, "近似の許容の内側");
}

KACHA_V2_TEST(kernel_loft, 内側のガイド1本と3断面から面を作る)
{
    const auto built = Build(Nose(3, {20.0}));
    Require(built.HasValue(), "作れる: " + Why(built));
    Require(built.Value().maximumDeviationMm <= 0.25, "断面もガイドも許容の内側で通る");
}

KACHA_V2_TEST(kernel_loft, ガイド3本と3断面から面を作り3本目も通る)
{
    const auto built = Build(Nose(3, {0.0, 20.0, 40.0}));
    Require(built.HasValue(), "作れる: " + Why(built));
    Require(built.Value().maximumDeviationMm <= 0.25, "3本とも許容の内側で通る");
}

KACHA_V2_TEST(kernel_loft, 断面5本とガイド5本から面を作る)
{
    const auto built = Build(Nose(5, {0.0, 10.0, 20.0, 30.0, 40.0}));
    Require(built.HasValue(), "作れる: " + Why(built));
    Require(built.Value().maximumDeviationMm <= 0.25, "全部を許容の内側で通る");
}

KACHA_V2_TEST(kernel_loft, 3本目のガイドを持ち上げると面が変わる)
{
    // 入力を無視していない証拠: 内側のガイドだけを断面の間で持ち上げると、面が変わる。
    const auto flat = Build(Nose(3, {0.0, 20.0, 40.0}, 0.0));
    const auto lifted = Build(Nose(3, {0.0, 20.0, 40.0}, 3.0));
    Require(flat.HasValue() && lifted.HasValue(), "どちらも作れる: " + Why(flat) + Why(lifted));
    Require(std::abs(lifted.Value().areaMm2 - flat.Value().areaMm2) > 1.0,
        "持ち上げたぶん面積が変わる(" + std::to_string(flat.Value().areaMm2) + " → "
            + std::to_string(lifted.Value().areaMm2) + ")");
}

KACHA_V2_TEST(kernel_loft, 中心線に沿って断面を運ぶ)
{
    GuideSurfaceRequest request = Nose(3, {});
    request.chains.push_back(Path(ChainRole::Centerline, 1, {{-1, 20, 0}, {101, 20, 0}}));
    const auto built = Build(request);
    Require(built.HasValue(), "作れる: " + Why(built));
}

namespace {

//! ねじれた四辺(U0: y=0 の円弧、V1: x=60 の直線、U1: y=40 の円弧、V0: x=0 の直線)。
[[nodiscard]] GuideSurfaceRequest Patch(FourEdgeStyle style, bool shuffled = false)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::FourEdgePatch;
    request.fourEdgeStyle = style;
    std::vector<GuideChain> sides{
        Arc(ChainRole::BoundarySide, 1, {0, 0, 0}, {30, 0, 8}, {60, 0, 0}),
        Path(ChainRole::BoundarySide, 2, {{60, 0, 0}, {60, 40, 5}}),
        Arc(ChainRole::BoundarySide, 3, {60, 40, 5}, {30, 40, 14}, {0, 40, 5}),
        Path(ChainRole::BoundarySide, 4, {{0, 40, 5}, {0, 0, 0}})};
    if (shuffled) {
        // 並びも向きもばらばらに渡す(自動で輪の順と向きを決め直す)。
        std::swap(sides[1], sides[3]);
        sides[0] = Arc(ChainRole::BoundarySide, 1, {60, 0, 0}, {30, 0, 8}, {0, 0, 0});
    }
    for (auto& side : sides) {
        request.chains.push_back(side);
    }
    return request;
}

} // namespace

KACHA_V2_TEST(kernel_four_edge, 4辺で面を作り4辺を通る)
{
    const auto built = Build(Patch(FourEdgeStyle::Coons));
    Require(built.HasValue(), "作れる: " + Why(built));
    Require(built.Value().maximumDeviationMm <= 1.0e-3, "4辺の上に乗る");
}

KACHA_V2_TEST(kernel_four_edge, 並びと向きが逆でも作れる)
{
    const auto built = Build(Patch(FourEdgeStyle::Coons, true));
    Require(built.HasValue(), "作れる: " + Why(built));
    Require(built.Value().maximumDeviationMm <= 1.0e-3, "4辺の上に乗る");
}

KACHA_V2_TEST(kernel_four_edge, 張り方で形が変わる)
{
    const auto coons = Build(Patch(FourEdgeStyle::Coons));
    const auto stretch = Build(Patch(FourEdgeStyle::Stretch));
    const auto curved = Build(Patch(FourEdgeStyle::Curved));
    Require(coons.HasValue() && stretch.HasValue() && curved.HasValue(),
        "3方式とも作れる: " + Why(coons) + Why(stretch) + Why(curved));
    const double a = coons.Value().areaMm2;
    const double b = stretch.Value().areaMm2;
    const double c = curved.Value().areaMm2;
    Require(std::abs(a - b) > 1.0e-6 || std::abs(a - c) > 1.0e-6,
        "方式の違いが面に表れる(" + std::to_string(a) + ", " + std::to_string(b) + ", "
            + std::to_string(c) + ")");
}

KACHA_V2_TEST(kernel_four_edge, 内側の通る線を複数本通す)
{
    GuideSurfaceRequest request = Patch(FourEdgeStyle::Coons);
    const auto baseline = Build(request);
    request.chains.push_back(Arc(ChainRole::GuideU, 1, {30, 0, 8}, {30, 20, 16}, {30, 40, 14}));
    request.chains.push_back(Arc(ChainRole::GuideU, 2, {0, 20, 2.5}, {30, 20, 16}, {60, 20, 2.5}));
    const auto built = Build(request);
    Require(baseline.HasValue() && built.HasValue(), "作れる: " + Why(built));
    Require(built.Value().maximumDeviationMm <= 0.25, "4辺も通る線も許容の内側");
    Require(built.Value().areaMm2 > baseline.Value().areaMm2 + 1.0,
        "通る線のぶん膨らむ(" + std::to_string(baseline.Value().areaMm2) + " → "
            + std::to_string(built.Value().areaMm2) + ")");
}

namespace {

//! 支持面: z = 0 の平面の長方形(x 0..40, y -20..0)。境界面の辺 y = 0 はこの面の縁。
[[nodiscard]] kachakacha::v2::modeling::KernelShapeHandle MakeFlatSupport()
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    GuideChain outer = Path(ChainRole::OuterBoundary, 1,
        {{0, -20, 0}, {40, -20, 0}, {40, 0, 0}, {0, 0, 0}, {0, -20, 0}});
    outer.closed = true;
    request.chains.push_back(outer);
    const auto built = Build(request);
    Require(built.HasValue(), "支持面が作れる: " + Why(built));
    return built.Value().handle;
}

//! 支持面の縁 y = 0 から立ち上がる境界面(反対の辺が持ち上がっている)。
[[nodiscard]] GuideSurfaceRequest FillBesideSupport(
    kachakacha::v2::modeling::SurfaceContinuity order,
    kachakacha::v2::modeling::KernelShapeHandle support)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::BoundaryFill;
    request.chains.push_back(Path(ChainRole::BoundarySide, 1, {{0, 0, 0}, {40, 0, 0}}));
    request.chains.push_back(Path(ChainRole::BoundarySide, 2, {{40, 0, 0}, {40, 30, 10}}));
    request.chains.push_back(Path(ChainRole::BoundarySide, 3, {{40, 30, 10}, {0, 30, 10}}));
    request.chains.push_back(Path(ChainRole::BoundarySide, 4, {{0, 30, 10}, {0, 0, 0}}));
    request.chains[0].continuity = order;
    if (support.Valid()) {
        request.chains[0].supportSurfaceId =
            kachakacha::v2::base::DeterministicIdGenerator{3}.NextTyped<kachakacha::v2::base::IdKind::Entity>();
        request.chains[0].supportShapeHandle = support.value;
    }
    return request;
}

} // namespace

KACHA_V2_TEST(kernel_continuity, G0とG1で支持面との境目の形が変わりG1は折れ目が小さい)
{
    using kachakacha::v2::modeling::SurfaceContinuity;
    const auto support = MakeFlatSupport();
    const auto g0 = Build(FillBesideSupport(SurfaceContinuity::G0, {}));
    const auto g1 = Build(FillBesideSupport(SurfaceContinuity::G1, support));
    Require(g0.HasValue() && g1.HasValue(), "どちらも作れる: " + Why(g0) + Why(g1));
    Require(g1.Value().continuityG1ErrorDeg >= 0.0 && g1.Value().continuityG1ErrorDeg <= 1.5,
        "G1 の折れ目を測って許容の内側(" + std::to_string(g1.Value().continuityG1ErrorDeg) + " 度)");
    Require(g0.Value().continuityG1ErrorDeg < 0.0, "G0 は測っていない(指定が無い)");
    Require(std::abs(g1.Value().areaMm2 - g0.Value().areaMm2) > 1.0e-3,
        "G1 を指定すると形が変わる(" + std::to_string(g0.Value().areaMm2) + " → "
            + std::to_string(g1.Value().areaMm2) + ")");
}

KACHA_V2_TEST(kernel_continuity, G2を指定すると曲率の差も測る)
{
    using kachakacha::v2::modeling::SurfaceContinuity;
    const auto support = MakeFlatSupport();
    const auto g2 = Build(FillBesideSupport(SurfaceContinuity::G2, support));
    Require(g2.HasValue(), "作れる: " + Why(g2));
    Require(g2.Value().continuityG2Error >= 0.0, "G2 の曲率の差を測っている");
}

KACHA_V2_TEST(kernel_continuity, 支持面の縁に乗っていない辺のG1は断る)
{
    using kachakacha::v2::modeling::SurfaceContinuity;
    const auto support = MakeFlatSupport();
    GuideSurfaceRequest request = FillBesideSupport(SurfaceContinuity::G1, support);
    // 辺 1 を持ち上げて支持面から離す(ほかの辺もつなぎ直す)。
    request.chains[0] = Path(ChainRole::BoundarySide, 1, {{0, 0, 2}, {40, 0, 2}});
    request.chains[0].continuity = SurfaceContinuity::G1;
    request.chains[0].supportSurfaceId =
        kachakacha::v2::base::DeterministicIdGenerator{4}.NextTyped<kachakacha::v2::base::IdKind::Entity>();
    request.chains[0].supportShapeHandle = support.value;
    request.chains[1] = Path(ChainRole::BoundarySide, 2, {{40, 0, 2}, {40, 30, 10}});
    request.chains[3] = Path(ChainRole::BoundarySide, 4, {{0, 30, 10}, {0, 0, 2}});
    const auto built = Build(request);
    Require(!built.HasValue(), "乗っていない辺では G1 を作らない");
    Require(Why(built).find("支持面の縁に乗っていません") != std::string::npos,
        "乗っていないと言う: " + Why(built));
}

KACHA_V2_TEST(kernel_continuity, 四辺面もG1を指定すると支持面に沿って張り直す)
{
    using kachakacha::v2::modeling::SurfaceContinuity;
    const auto support = MakeFlatSupport();
    GuideSurfaceRequest request = FillBesideSupport(SurfaceContinuity::G1, support);
    request.method = GuideSurfaceMethod::FourEdgePatch;
    const auto built = Build(request);
    Require(built.HasValue(), "作れる: " + Why(built));
    Require(built.Value().continuityG1ErrorDeg >= 0.0 && built.Value().continuityG1ErrorDeg <= 1.5,
        "G1 を測って許容の内側");
}

#endif // KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST_MAIN("kernel_loft_tests")
