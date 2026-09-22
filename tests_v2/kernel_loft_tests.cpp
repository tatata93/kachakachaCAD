// ロフト(断面 2〜任意 + ガイド 0〜任意 + 中心線 0〜1)と四辺面を、実際に核で作る。
//
// OCCT が無い版ではこの試験の中身は走らない(核の無い版は面を作らずに断る。
// それは kernel_surface_tests の kernel_absent が見る)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/fabrication/SurfacePatch.h"
#include "kachakacha/geometry/ArcBuilders.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
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

KACHA_V2_TEST(kernel_loft, 両端のガイド2本と3断面は網で作り全部の線を通る)
{
    // 2026-09-22 から 2 本のレールで掃く作りをやめ、断面とガイドの網(Gordon)にした。
    // 網は全部の線を通すので、曲線網の試験と同じ 0.02 mm で見る。
    const auto built = Build(Nose(3, {0.0, 40.0}));
    Require(built.HasValue(), "作れる: " + Why(built));
    Require(built.Value().maximumDeviationMm <= 0.02,
        "全部の線から 0.02 mm 以内(" + std::to_string(built.Value().maximumDeviationMm) + ")");
}

namespace {

//! 面の標本の 1 筋(行か列)に沿った高さの山の数。
[[nodiscard]] int PeaksAlong(const kachakacha::v2::fabrication::SurfacePatchSamples& samples,
    bool alongRow, std::size_t fixed)
{
    const std::size_t count = alongRow ? samples.columnCount : samples.rowCount;
    const auto z = [&](std::size_t k) {
        return alongRow ? samples.At(fixed, k).z : samples.At(k, fixed).z;
    };
    int peaks = 0;
    for (std::size_t k = 1; k + 1 < count; ++k) {
        peaks += z(k) > z(k - 1) + 1.0e-6 && z(k) >= z(k + 1) ? 1 : 0;
    }
    return peaks;
}

} // namespace

KACHA_V2_TEST(kernel_loft, はしご形の3断面と両端のガイドは断面の間で波打たない)
{
    // PC の撮影(ui/12、2026-09-22): 高さ 12・16・10 のアーチ 3 本と両端の直線ガイド 2 本を
    // 2 本のレールで掃くと、山が 3 つのはずが 7 つに波打った。網で作れば、どの筋も山は 1 つ。
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::LoftSections;
    const double xs[3] = {0.0, 40.0, 80.0};
    const double heights[3] = {12.0, 16.0, 10.0};
    for (int s = 0; s < 3; ++s) {
        const double lift = heights[s] / 0.75;
        GuideChain chain;
        chain.role = ChainRole::Section;
        chain.index = s + 1;
        chain.segments.push_back(CurveSegment::MakeCubicBezier({Vector3{xs[s], 0.0, 0.0},
            Vector3{xs[s], 0.0, lift}, Vector3{xs[s], 40.0, lift}, Vector3{xs[s], 40.0, 0.0}})
                .Value());
        request.chains.push_back(chain);
    }
    request.chains.push_back(Path(ChainRole::GuideU, 1, {{0, 0, 0}, {80, 0, 0}}));
    request.chains.push_back(Path(ChainRole::GuideU, 2, {{0, 40, 0}, {80, 40, 0}}));
    const auto built = Build(request);
    Require(built.HasValue(), "作れる: " + Why(built));
    const auto& samples = built.Value().samples;
    Require(samples.Valid(), "面の標本がある");
    const int acrossRows = PeaksAlong(samples, false, samples.columnCount / 2);
    const int acrossColumns = PeaksAlong(samples, true, samples.rowCount / 2);
    Require(acrossRows <= 1 && acrossColumns <= 1,
        "真ん中の筋の山は 1 つ(" + std::to_string(acrossRows) + " / "
            + std::to_string(acrossColumns) + ")");
    double highest = 0.0;
    for (const Vector3& point : samples.points) {
        highest = std::max(highest, point.z);
    }
    Require(highest <= 16.0 * 1.05, "いちばん高い断面(16)を大きく越えない: "
            + std::to_string(highest));
    Require(built.Value().maximumDeviationMm <= 0.02,
        "全部の線から 0.02 mm 以内(" + std::to_string(built.Value().maximumDeviationMm) + ")");
}

KACHA_V2_TEST(kernel_loft, 断面1本と両端のガイドは仮想断面を足して網で作る)
{
    // 断面は x = 50 の円弧 1 本だけ。ガイドは x = 0〜100 の直線 2 本。両端に仮想断面
    // (同じ円弧を運んだもの)を足すので、円弧を 100 mm 押し出した形になる。
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::LoftSections;
    request.chains.push_back(Arc(ChainRole::Section, 1, {50, 0, 0}, {50, 20, 10}, {50, 40, 0}));
    request.chains.push_back(Path(ChainRole::GuideU, 1, {{0, 0, 0}, {100, 0, 0}}));
    request.chains.push_back(Path(ChainRole::GuideU, 2, {{0, 40, 0}, {100, 40, 0}}));
    const auto built = Build(request);
    Require(built.HasValue(), "作れる: " + Why(built));
    // 円弧: 弦 40・高さ 10 → 半径 25、中心角 2·asin(0.8)。長さ × 100 mm が面積。
    const double expected = 2.0 * std::asin(0.8) * 25.0 * 100.0;
    Require(std::abs(built.Value().areaMm2 - expected) < expected * 0.01,
        "円弧を押し出した面積(" + std::to_string(built.Value().areaMm2) + " / "
            + std::to_string(expected) + ")");
    Require(built.Value().maximumDeviationMm <= 0.02,
        "断面とガイドから 0.02 mm 以内(" + std::to_string(built.Value().maximumDeviationMm) + ")");
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

//! 3 次ベジエ 1 本の鎖。
[[nodiscard]] GuideChain Bezier(ChainRole role, int index, std::vector<Vector3> controls)
{
    GuideChain chain;
    chain.role = role;
    chain.index = index;
    chain.segments.push_back(CurveSegment::MakeCubicBezier(std::move(controls)).Value());
    return chain;
}

//! 支持面の縁 y = 0 から立ち上がる境界面(反対の辺が持ち上がり、真ん中がさらに膨らむ)。
//!
//! 脇の 2 辺は、支持面の縁で水平に出て、そこで曲がり方も 0 の 3 次ベジエ
//! (制御点 y = 0, 10, 20 が z = 0 に並ぶ)。直線にすると角で脇の辺そのものが
//! 支持面から 18 度立ち上がり、辺 1 の全長で G1 は原理的に成り立たない
//! (2026-09-22 PC: 5.1 度の折れ目が残り、正しく断られた)。円弧でも角で曲率が残り G2 が
//! 成り立たない。反対の辺 3 は真ん中が膨らむ円弧。G0 のままだと辺 1 の真ん中で面が
//! 支持面から折れて立ち上がるので、G1 を指定すると形が変わる。
[[nodiscard]] GuideSurfaceRequest FillBesideSupport(
    kachakacha::v2::modeling::SurfaceContinuity order,
    kachakacha::v2::modeling::KernelShapeHandle support)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::BoundaryFill;
    request.chains.push_back(Path(ChainRole::BoundarySide, 1, {{0, 0, 0}, {40, 0, 0}}));
    request.chains.push_back(Bezier(ChainRole::BoundarySide, 2,
        {{40, 0, 0}, {40, 10, 0}, {40, 20, 0}, {40, 30, 10}}));
    request.chains.push_back(Arc(ChainRole::BoundarySide, 3, {40, 30, 10}, {20, 30, 16},
        {0, 30, 10}));
    request.chains.push_back(Bezier(ChainRole::BoundarySide, 4,
        {{0, 30, 10}, {0, 20, 0}, {0, 10, 0}, {0, 0, 0}}));
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
    Require(g2.Value().continuityG2Error >= 0.0 && g2.Value().continuityG2Error <= 0.1,
        "G2 の曲率の差を測って許容の内側(" + std::to_string(g2.Value().continuityG2Error)
            + " /mm、折れ目 " + std::to_string(g2.Value().continuityG1ErrorDeg) + " 度)");
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

namespace {

[[nodiscard]] double Bowl(double x, double y)
{
    return x * x / 100.0 + y * y / 80.0;
}

[[nodiscard]] GuideChain OnBowl(ChainRole role, int index, Vector3 from, Vector3 to)
{
    std::vector<Vector3> points;
    for (int k = 0; k <= 40; ++k) {
        const double t = k / 40.0;
        const double x = from.x + (to.x - from.x) * t;
        const double y = from.y + (to.y - from.y) * t;
        points.push_back({x, y, Bowl(x, y)});
    }
    return Path(role, index, points);
}

[[nodiscard]] GuideSurfaceRequest BowlNetwork(GuideSurfaceMethod method, int uCount, int vCount)
{
    GuideSurfaceRequest request;
    request.method = method;
    for (int i = 0; i < uCount; ++i) {
        const double y = -20.0 + 40.0 * i / (uCount - 1);
        request.chains.push_back(OnBowl(ChainRole::GuideU, i + 1, {-30.0, y, 0}, {30.0, y, 0}));
    }
    for (int j = 0; j < vCount; ++j) {
        const double x = -30.0 + 60.0 * j / (vCount - 1);
        request.chains.push_back(OnBowl(ChainRole::GuideV, j + 1, {x, -20.0, 0}, {x, 20.0, 0}));
    }
    return request;
}

} // namespace

KACHA_V2_TEST(kernel_network, GordonはU2V2とU5V4の網を全部の線に沿って作る)
{
    for (const auto& [u, v] : {std::pair<int, int>{2, 2}, std::pair<int, int>{5, 4}}) {
        const auto built = Build(BowlNetwork(GuideSurfaceMethod::CurveNetworkExact, u, v));
        Require(built.HasValue(), "作れる(U" + std::to_string(u) + "V" + std::to_string(v) + "): "
                + Why(built));
        Require(built.Value().maximumDeviationMm <= 0.02,
            "全部の線から 0.02 mm 以内(" + std::to_string(built.Value().maximumDeviationMm) + ")");
    }
}

KACHA_V2_TEST(kernel_network, 近似の曲線網も同じ網から作れる)
{
    const auto built = Build(BowlNetwork(GuideSurfaceMethod::GordonNetwork, 5, 4));
    Require(built.HasValue(), "作れる: " + Why(built));
    Require(built.Value().maximumDeviationMm <= 0.25, "近似の許容の内側");
}

#endif // KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST_MAIN("kernel_loft_tests")
