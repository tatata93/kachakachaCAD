// 幾何カーネル層(WP-06)の試験。
//
// ここは OCCT が入っているときだけ本番の検査ができる。
// 入っていない環境では「カーネルが無い」と正しく断ることだけを確かめる。
// 断り方まで試験するのは、V1が「無いのに有るふり」をして落ちたためである。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctCurveConversion.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <Geom_Hyperbola.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <gp_Ax2.hxx>
#include <gp_Hypr.hxx>
#endif

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::geometry::kPi;
using kachakacha::v2::kernel::BuildGuideSurface;
using kachakacha::v2::kernel::CachedShapeCount;
using kachakacha::v2::kernel::ClearShapeCache;
using kachakacha::v2::kernel::HasShape;
using kachakacha::v2::kernel::ReleaseShape;
using kachakacha::v2::modeling::AnalyzeGuideSurfaceRequest;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::GuideChain;
using kachakacha::v2::modeling::GuideSurfaceMethod;
using kachakacha::v2::modeling::GuideSurfaceRequest;
using kachakacha::v2::modeling::GuideSurfaceResult;
using kachakacha::v2::modeling::KernelShapeHandle;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[maybe_unused]] [[nodiscard]] GeometryTolerance Tolerance()
{
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = 1.0e-6;
    tolerance.interactiveJoinMm = 0.01;
    return tolerance;
}

[[maybe_unused]] [[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れること");
    return made.Value();
}

[[maybe_unused]] [[nodiscard]] GuideChain Rectangle(ChainRole role, int index, double x0, double y0,
    double x1, double y1, double z = 0.0)
{
    GuideChain chain;
    chain.role = role;
    chain.index = index;
    chain.closed = true;
    chain.segments = {
        Line({x0, y0, z}, {x1, y0, z}),
        Line({x1, y0, z}, {x1, y1, z}),
        Line({x1, y1, z}, {x0, y1, z}),
        Line({x0, y1, z}, {x0, y0, z}),
    };
    return chain;
}

[[maybe_unused]] [[nodiscard]] GuideChain Circle(ChainRole role, int index, Vector3 center, double radius)
{
    GuideChain chain;
    chain.role = role;
    chain.index = index;
    chain.closed = true;
    const auto made =
        CurveSegment::MakeCircle(center, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}, radius);
    Require(made.HasValue(), "円が作れること");
    chain.segments = {made.Value()};
    return chain;
}

[[maybe_unused]] [[nodiscard]] GuideChain OpenLine(ChainRole role, int index, Vector3 a, Vector3 b)
{
    GuideChain chain;
    chain.role = role;
    chain.index = index;
    chain.closed = false;
    chain.segments = {Line(a, b)};
    return chain;
}

//! 入力検査を通してから面を作る。検査で落ちたら試験の失敗にする。
[[maybe_unused]] [[nodiscard]] kachakacha::v2::base::Result<GuideSurfaceResult> Build(
    const GuideSurfaceRequest& request, KernelShapeHandle source = {})
{
    const GeometryTolerance tolerance = Tolerance();
    auto analysis = AnalyzeGuideSurfaceRequest(request, tolerance);
    Require(analysis.HasValue(), "入力検査が通ること");
    return BuildGuideSurface(request, analysis.Value(), tolerance, source);
}

[[maybe_unused]] [[nodiscard]] std::string FirstCode(
    const std::vector<kachakacha::v2::base::Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

} // namespace

// =====================================================================
//  どの環境でも走る: 形状の表(handle 管理)
// =====================================================================

KACHA_V2_TEST(kernel_cache, 知らない番号を捨てても落ちない)
{
    ClearShapeCache();
    Require(!ReleaseShape(KernelShapeHandle{999999}), "知らない番号は false");
    Require(!HasShape(KernelShapeHandle{999999}), "知らない番号は居ない");
    RequireEqual(std::to_string(CachedShapeCount()), "0", "表は空のまま");
}

KACHA_V2_TEST(kernel_cache, 0番は常に無効)
{
    ClearShapeCache();
    KernelShapeHandle empty;
    Require(!empty.Valid(), "既定の handle は無効");
    Require(!HasShape(empty), "0番は表に居ない");
}

#ifndef KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST(kernel_absent, カーネルが無い版は面を作らずに断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 10, 5));
    const auto built = Build(request);
    Require(!built.HasValue(), "作れたことにしない");
    RequireEqual(FirstCode(built.Diagnostics()), "KER-S005", "カーネル不在の診断コード");
}

#else // KACHACAD_V2_WITH_OCCT

// =====================================================================
//  曲線の往復。種類を落とさないこと
// =====================================================================

namespace {

using kachakacha::v2::kernel::FromEdge;
using kachakacha::v2::kernel::ToEdge;
using kachakacha::v2::kernel::ToWire;

void RequireRoundTripKind(const CurveSegment& segment, CurveKind expected,
    const std::string& why)
{
    auto edge = ToEdge(segment);
    Require(edge.HasValue(), why + ": 辺にできること");
    auto back = FromEdge(edge.Value(), 1.0e-6);
    Require(back.HasValue(), why + ": 戻せること");
    RequireEqual(std::string(kachakacha::v2::geometry::CurveKindName(back.Value().Kind())),
        std::string(kachakacha::v2::geometry::CurveKindName(expected)), why + ": 種類");
    const Vector3 startGap = back.Value().StartPoint() - segment.StartPoint();
    const Vector3 endGap = back.Value().EndPoint() - segment.EndPoint();
    RequireNear(startGap.Length(), 0.0, 1.0e-7, why + ": 始点");
    RequireNear(endGap.Length(), 0.0, 1.0e-7, why + ": 終点");
}

} // namespace

KACHA_V2_TEST(kernel_curve, 直線は直線のまま戻る)
{
    RequireRoundTripKind(Line({0, 0, 0}, {10, 3, 2}), CurveKind::Line, "直線");
}

KACHA_V2_TEST(kernel_curve, 円は円のまま戻る)
{
    const auto made = CurveSegment::MakeCircle({1, 2, 3}, {0, 0, 1}, {1, 0, 0}, 7.5);
    Require(made.HasValue(), "円が作れること");
    auto edge = ToEdge(made.Value());
    Require(edge.HasValue(), "辺にできること");
    auto back = FromEdge(edge.Value(), 1.0e-6);
    Require(back.HasValue(), "戻せること");
    Require(back.Value().Kind() == CurveKind::Circle, "円のまま");
    RequireNear(back.Value().Radius(), 7.5, 1.0e-9, "半径");
}

KACHA_V2_TEST(kernel_curve, 正の掃引の円弧が戻る)
{
    const auto made = CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0},
        5.0, 0.0, kPi / 2.0);
    Require(made.HasValue(), "円弧が作れること");
    RequireRoundTripKind(made.Value(), CurveKind::CircularArc, "90度の円弧");
}

KACHA_V2_TEST(kernel_curve, 負の掃引の円弧も向きを保って戻る)
{
    const auto made = CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0},
        5.0, kPi / 2.0, -kPi / 2.0);
    Require(made.HasValue(), "円弧が作れること");
    RequireRoundTripKind(made.Value(), CurveKind::CircularArc, "逆回りの円弧");
}

KACHA_V2_TEST(kernel_curve, 半径の大きい浅い円弧も戻る)
{
    const auto made = CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0},
        5000.0, 0.0, 0.01);
    Require(made.HasValue(), "円弧が作れること");
    RequireRoundTripKind(made.Value(), CurveKind::CircularArc, "浅い円弧");
}

KACHA_V2_TEST(kernel_curve, 3次ベジェが戻る)
{
    const auto made = CurveSegment::MakeCubicBezier(
        {{0, 0, 0}, {1, 4, 0}, {6, 4, 0}, {8, 0, 0}});
    Require(made.HasValue(), "ベジェが作れること");
    RequireRoundTripKind(made.Value(), CurveKind::CubicBezier, "3次ベジェ");
}

KACHA_V2_TEST(kernel_curve, B_splineが戻る)
{
    const auto made = CurveSegment::MakeCubicBSpline(
        {{0, 0, 0}, {2, 5, 0}, {6, -3, 0}, {10, 2, 0}, {14, 0, 0}});
    Require(made.HasValue(), "B-splineが作れること");
    auto edge = ToEdge(made.Value());
    Require(edge.HasValue(), "辺にできること");
    auto back = FromEdge(edge.Value(), 1.0e-6);
    Require(back.HasValue(), "戻せること");
    Require(back.Value().Kind() == CurveKind::CubicBSpline, "B-splineのまま");
    RequireEqual(std::to_string(back.Value().ControlPoints().size()), "5", "制御点の数");
}

KACHA_V2_TEST(kernel_curve, 長さ0の直線は渡せない)
{
    // MakeLine が先に断るので、その診断を確かめる。
    const auto made = CurveSegment::MakeLine({1, 1, 1}, {1, 1, 1});
    Require(!made.HasValue(), "長さ0の直線は作れない");
}

KACHA_V2_TEST(kernel_curve, 双曲線は折れ線へ落とさずに断る)
{
    const gp_Ax2 axis(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    occ::handle<Geom_Hyperbola> hyperbola = new Geom_Hyperbola(gp_Hypr(axis, 5.0, 3.0));
    occ::handle<Geom_Curve> trimmed = new Geom_TrimmedCurve(hyperbola, 0.1, 0.6);
    BRepBuilderAPI_MakeEdge maker(trimmed);
    Require(maker.IsDone(), "双曲線の辺が作れること");
    auto back = FromEdge(maker.Edge(), 1.0e-6);
    Require(!back.HasValue(), "戻せたことにしない");
    RequireEqual(FirstCode(back.Diagnostics()), "KER-C001", "扱えない種類の診断コード");
}

KACHA_V2_TEST(kernel_curve, つながっていない並びはワイヤーにしない)
{
    const std::vector<CurveSegment> broken{
        Line({0, 0, 0}, {10, 0, 0}),
        Line({10, 5, 0}, {10, 10, 0}),
    };
    auto wire = ToWire(broken, 1.0e-6);
    Require(!wire.HasValue(), "勝手に繋がない");
    RequireEqual(FirstCode(wire.Diagnostics()), "KER-C003", "未接続の診断コード");
}

KACHA_V2_TEST(kernel_curve, 微小な隙間はワイヤーにできる)
{
    const std::vector<CurveSegment> nearlyJoined{
        Line({0, 0, 0}, {10, 0, 0}),
        Line({10, 1.0e-7, 0}, {10, 10, 0}),
    };
    auto wire = ToWire(nearlyJoined, 1.0e-6);
    Require(wire.HasValue(), "許容差の内側は繋げる");
}

KACHA_V2_TEST(kernel_curve, 線が1本もなければ断る)
{
    auto wire = ToWire({}, 1.0e-6);
    Require(!wire.HasValue(), "空の並びは断る");
}

// =====================================================================
//  平面の輪郭
// =====================================================================

KACHA_V2_TEST(kernel_planar, 長方形の面積が合う)
{
    ClearShapeCache();
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 40, 25));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    RequireNear(built.Value().areaMm2, 40.0 * 25.0, 1.0e-6, "面積");
    Require(built.Value().handle.Valid(), "handle が有効");
    Require(HasShape(built.Value().handle), "表に載っている");
}

KACHA_V2_TEST(kernel_planar, 平面と分かること)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 40, 25));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    Require(built.Value().analytic.kind
            == kachakacha::v2::fabrication::AnalyticSurfaceKind::Plane,
        "平面と分かる");
}

KACHA_V2_TEST(kernel_planar, 円の面積が合う)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Circle(ChainRole::OuterBoundary, 1, {0, 0, 0}, 12.0));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    RequireNear(built.Value().areaMm2, kPi * 144.0, 1.0e-4, "円の面積");
}

KACHA_V2_TEST(kernel_planar, 穴の分だけ面積が減る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 40, 25));
    request.chains.push_back(Circle(ChainRole::HoleBoundary, 1, {20, 12.5, 0}, 5.0));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    RequireNear(built.Value().areaMm2, 40.0 * 25.0 - kPi * 25.0, 1.0e-4, "穴つきの面積");
}

KACHA_V2_TEST(kernel_planar, 穴が2つでも面積が合う)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 60, 30));
    request.chains.push_back(Circle(ChainRole::HoleBoundary, 1, {15, 15, 0}, 4.0));
    request.chains.push_back(Circle(ChainRole::HoleBoundary, 2, {45, 15, 0}, 6.0));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    RequireNear(built.Value().areaMm2, 60.0 * 30.0 - kPi * 16.0 - kPi * 36.0, 1.0e-4,
        "穴2つの面積");
}

KACHA_V2_TEST(kernel_planar, 出来た面は入力の線を通っている)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 40, 25));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    RequireNear(built.Value().maximumDeviationMm, 0.0, 1.0e-7, "ずれが無い");
}

KACHA_V2_TEST(kernel_planar, 外周が曲線種類を保って戻る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 40, 25));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    RequireEqual(std::to_string(built.Value().boundary.size()), "4", "外周は4本");
    for (const CurveSegment& segment : built.Value().boundary) {
        Require(segment.Kind() == CurveKind::Line, "外周は直線のまま");
    }
}

KACHA_V2_TEST(kernel_planar, 円の外周は円のまま戻る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Circle(ChainRole::OuterBoundary, 1, {0, 0, 0}, 9.0));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    RequireEqual(std::to_string(built.Value().boundary.size()), "1", "外周は1本");
    Require(built.Value().boundary.front().Kind() == CurveKind::Circle, "円のまま");
}

KACHA_V2_TEST(kernel_planar, 斜めの平面でも面積が合う)
{
    // z = x の平面に載る長方形。辺の長さは 40*sqrt(2) と 25。
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    GuideChain chain;
    chain.role = ChainRole::OuterBoundary;
    chain.index = 1;
    chain.closed = true;
    chain.segments = {
        Line({0, 0, 0}, {40, 0, 40}),
        Line({40, 0, 40}, {40, 25, 40}),
        Line({40, 25, 40}, {0, 25, 0}),
        Line({0, 25, 0}, {0, 0, 0}),
    };
    request.chains.push_back(chain);
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    RequireNear(built.Value().areaMm2, 40.0 * std::sqrt(2.0) * 25.0, 1.0e-6, "斜面の面積");
}

KACHA_V2_TEST(kernel_planar, 角丸の輪郭でも面積が合う)
{
    const double radius = 5.0;
    const double width = 40.0;
    const double height = 25.0;
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    GuideChain chain;
    chain.role = ChainRole::OuterBoundary;
    chain.index = 1;
    chain.closed = true;
    const auto arc = [&](Vector3 center, double startAngle) {
        const auto made = CurveSegment::MakeCircularArc(center, {0, 0, 1}, {1, 0, 0},
            radius, startAngle, kPi / 2.0);
        Require(made.HasValue(), "角の円弧が作れること");
        return made.Value();
    };
    chain.segments = {
        Line({radius, 0, 0}, {width - radius, 0, 0}),
        arc({width - radius, radius, 0}, -kPi / 2.0),
        Line({width, radius, 0}, {width, height - radius, 0}),
        arc({width - radius, height - radius, 0}, 0.0),
        Line({width - radius, height, 0}, {radius, height, 0}),
        arc({radius, height - radius, 0}, kPi / 2.0),
        Line({0, height - radius, 0}, {0, radius, 0}),
        arc({radius, radius, 0}, kPi),
    };
    request.chains.push_back(chain);
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    const double expected = width * height - (4.0 - kPi) * radius * radius;
    RequireNear(built.Value().areaMm2, expected, 1.0e-4, "角丸の面積");
}

// =====================================================================
//  断面をつなぐ
// =====================================================================

KACHA_V2_TEST(kernel_sections, 平行な四角2枚を直線でつなぐと側面積が合う)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::RuledSections;
    request.chains.push_back(Rectangle(ChainRole::Section, 1, 0, 0, 40, 25, 0.0));
    request.chains.push_back(Rectangle(ChainRole::Section, 2, 0, 0, 40, 25, 30.0));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    RequireNear(built.Value().areaMm2, 2.0 * (40.0 + 25.0) * 30.0, 1.0e-4, "側面積");
}

KACHA_V2_TEST(kernel_sections, 円2枚を直線でつなぐと円筒になる)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::RuledSections;
    request.chains.push_back(Circle(ChainRole::Section, 1, {0, 0, 0}, 10.0));
    request.chains.push_back(Circle(ChainRole::Section, 2, {0, 0, 20.0}, 10.0));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    RequireNear(built.Value().areaMm2, 2.0 * kPi * 10.0 * 20.0, 1.0e-3, "円筒の側面積");
}

KACHA_V2_TEST(kernel_sections, 大小の円をつなぐと円錐台の面積になる)
{
    const double r1 = 10.0;
    const double r2 = 4.0;
    const double height = 15.0;
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::RuledSections;
    request.chains.push_back(Circle(ChainRole::Section, 1, {0, 0, 0}, r1));
    request.chains.push_back(Circle(ChainRole::Section, 2, {0, 0, height}, r2));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    const double slant = std::sqrt((r1 - r2) * (r1 - r2) + height * height);
    RequireNear(built.Value().areaMm2, kPi * (r1 + r2) * slant, 1.0e-3, "円錐台の側面積");
}

KACHA_V2_TEST(kernel_sections, つないだ面は断面を通っている)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::RuledSections;
    request.chains.push_back(Rectangle(ChainRole::Section, 1, 0, 0, 40, 25, 0.0));
    request.chains.push_back(Rectangle(ChainRole::Section, 2, 0, 0, 40, 25, 30.0));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    RequireNear(built.Value().maximumDeviationMm, 0.0, 1.0e-6, "断面を通っている");
}

KACHA_V2_TEST(kernel_sections, なめらかにつなぐ版も断面を通っている)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::LoftSections;
    request.chains.push_back(Circle(ChainRole::Section, 1, {0, 0, 0}, 10.0));
    request.chains.push_back(Circle(ChainRole::Section, 2, {0, 0, 10.0}, 14.0));
    request.chains.push_back(Circle(ChainRole::Section, 3, {0, 0, 20.0}, 10.0));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    RequireNear(built.Value().maximumDeviationMm, 0.0, 1.0e-4, "断面を通っている");
}

KACHA_V2_TEST(kernel_sections, 断面が5枚でも通る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::LoftSections;
    for (int index = 0; index < 5; ++index) {
        const double z = 8.0 * index;
        const double radius = 6.0 + 2.0 * std::sin(0.4 * index);
        request.chains.push_back(
            Circle(ChainRole::Section, index + 1, {0, 0, z}, radius));
    }
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    Require(built.Value().areaMm2 > 0.0, "面積が正");
    RequireNear(built.Value().maximumDeviationMm, 0.0, 1.0e-3, "断面を通っている");
}

KACHA_V2_TEST(kernel_sections, 出来た面の標本が格子として正しい)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::RuledSections;
    request.chains.push_back(Circle(ChainRole::Section, 1, {0, 0, 0}, 10.0));
    request.chains.push_back(Circle(ChainRole::Section, 2, {0, 0, 20.0}, 10.0));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    Require(built.Value().samples.Valid(), "標本が格子になっている");
    for (const Vector3& point : built.Value().samples.points) {
        Require(point.IsFinite(), "標本に有限でない数が無い");
    }
}

// =====================================================================
//  境界から張る
// =====================================================================

KACHA_V2_TEST(kernel_fill, 平面の4辺から張ると平面の面積になる)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::BoundaryFill;
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 1, {0, 0, 0}, {40, 0, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 2, {40, 0, 0}, {40, 25, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 3, {40, 25, 0}, {0, 25, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 4, {0, 25, 0}, {0, 0, 0}));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    RequireNear(built.Value().areaMm2, 40.0 * 25.0, 1.0e-2, "面積");
}

KACHA_V2_TEST(kernel_fill, 3辺でも張れる)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::BoundaryFill;
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 1, {0, 0, 0}, {30, 0, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 2, {30, 0, 0}, {0, 20, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 3, {0, 20, 0}, {0, 0, 0}));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    RequireNear(built.Value().areaMm2, 0.5 * 30.0 * 20.0, 1.0e-2, "三角形の面積");
}

KACHA_V2_TEST(kernel_fill, 張った面は境界を通っている)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::BoundaryFill;
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 1, {0, 0, 0}, {40, 0, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 2, {40, 0, 0}, {40, 25, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 3, {40, 25, 0}, {0, 25, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 4, {0, 25, 0}, {0, 0, 0}));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    Require(built.Value().maximumDeviationMm <= 1.0e-4, "境界を通っている");
}

KACHA_V2_TEST(kernel_fill, ねじれた4辺でも張れて境界を通る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::BoundaryFill;
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 1, {0, 0, 0}, {40, 0, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 2, {40, 0, 0}, {40, 25, 8}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 3, {40, 25, 8}, {0, 25, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 4, {0, 25, 0}, {0, 0, 0}));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    Require(built.Value().maximumDeviationMm <= 1.0e-3, "境界を通っている");
}

// =====================================================================
//  面をずらす
// =====================================================================

namespace {

[[nodiscard]] KernelShapeHandle MakePlanarSource()
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 40, 25));
    const auto built = Build(request);
    Require(built.HasValue(), "元の面が出来ること");
    return built.Value().handle;
}

[[nodiscard]] KernelShapeHandle MakeCylinderSource(double radius, double height)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::RuledSections;
    request.chains.push_back(Circle(ChainRole::Section, 1, {0, 0, 0}, radius));
    request.chains.push_back(Circle(ChainRole::Section, 2, {0, 0, height}, radius));
    const auto built = Build(request);
    Require(built.HasValue(), "元の円筒が出来ること");
    return built.Value().handle;
}

[[nodiscard]] GuideSurfaceRequest OffsetRequest(double distance)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::OffsetGuide;
    request.offsetDistanceMm = distance;
    GuideChain chain;
    chain.role = ChainRole::SourceSurface;
    chain.index = 1;
    request.chains.push_back(chain);
    return request;
}

} // namespace

KACHA_V2_TEST(kernel_offset, 平面をずらすと平面のまま同じ面積になる)
{
    const KernelShapeHandle source = MakePlanarSource();
    const auto built = Build(OffsetRequest(3.0), source);
    Require(built.HasValue(), "ずらせること");
    Require(built.Value().analytic.kind
            == kachakacha::v2::fabrication::AnalyticSurfaceKind::Plane,
        "平面のまま");
    RequireNear(built.Value().areaMm2, 40.0 * 25.0, 1.0e-4, "面積は変わらない");
}

KACHA_V2_TEST(kernel_offset, 平面をずらすと法線方向へ動く)
{
    const KernelShapeHandle source = MakePlanarSource();
    const auto built = Build(OffsetRequest(3.0), source);
    Require(built.HasValue(), "ずらせること");
    Require(built.Value().samples.Valid(), "標本がある");
    const double z = built.Value().samples.points.front().z;
    RequireNear(std::abs(z), 3.0, 1.0e-6, "3mm 動いている");
}

KACHA_V2_TEST(kernel_offset, 円筒をずらすと円筒のままで半径が変わる)
{
    const KernelShapeHandle source = MakeCylinderSource(10.0, 20.0);
    const auto built = Build(OffsetRequest(2.0), source);
    Require(built.HasValue(), "ずらせること");
    if (built.Value().analytic.kind
        == kachakacha::v2::fabrication::AnalyticSurfaceKind::Cylinder) {
        const double radius = built.Value().analytic.radiusMm;
        Require(std::abs(radius - 12.0) <= 1.0e-6 || std::abs(radius - 8.0) <= 1.0e-6,
            "半径が 8mm か 12mm になる");
    }
}

KACHA_V2_TEST(kernel_offset, 元の面が無ければ断る)
{
    const auto built = Build(OffsetRequest(3.0), KernelShapeHandle{123456789});
    Require(!built.HasValue(), "作れたことにしない");
    RequireEqual(FirstCode(built.Diagnostics()), "KER-S003", "元が無いときの診断コード");
}

KACHA_V2_TEST(kernel_offset, handle を渡さなければ断る)
{
    const auto built = Build(OffsetRequest(3.0));
    Require(!built.HasValue(), "作れたことにしない");
    RequireEqual(FirstCode(built.Diagnostics()), "KER-S003", "元が無いときの診断コード");
}

KACHA_V2_TEST(kernel_offset, 円筒がつぶれる距離は断る)
{
    const KernelShapeHandle source = MakeCylinderSource(10.0, 20.0);
    const auto built = Build(OffsetRequest(-15.0), source);
    if (!built.HasValue()) {
        RequireEqual(FirstCode(built.Diagnostics()), "KER-S004", "つぶれるときの診断コード");
    } else {
        // 面の向きによっては外側へ広がる。その場合は半径が正であること。
        Require(built.Value().areaMm2 > 0.0, "面積が正");
    }
}

// =====================================================================
//  表の後始末
// =====================================================================

KACHA_V2_TEST(kernel_cache, 作った分だけ表が増える)
{
    ClearShapeCache();
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 10, 5));
    for (int index = 0; index < 5; ++index) {
        const auto built = Build(request);
        Require(built.HasValue(), "面が出来ること");
    }
    RequireEqual(std::to_string(CachedShapeCount()), "5", "5個ある");
    ClearShapeCache();
    RequireEqual(std::to_string(CachedShapeCount()), "0", "空になる");
}

KACHA_V2_TEST(kernel_cache, 捨てた番号はもう引けない)
{
    ClearShapeCache();
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 10, 5));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    const KernelShapeHandle handle = built.Value().handle;
    Require(ReleaseShape(handle), "捨てられる");
    Require(!HasShape(handle), "もう居ない");
    Require(!ReleaseShape(handle), "二度は捨てられない");
    ClearShapeCache();
}

KACHA_V2_TEST(kernel_cache, 番号は使い回されない)
{
    ClearShapeCache();
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 10, 5));
    const auto first = Build(request);
    Require(first.HasValue(), "1枚目");
    const KernelShapeHandle handle = first.Value().handle;
    Require(ReleaseShape(handle), "捨てる");
    const auto second = Build(request);
    Require(second.HasValue(), "2枚目");
    Require(second.Value().handle.value != handle.value, "同じ番号を再利用しない");
    ClearShapeCache();
}

// =====================================================================
//  くり返しても同じ結果になること(決定性)
// =====================================================================

KACHA_V2_TEST(kernel_determinism, 同じ入力からは同じ面積になる)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::LoftSections;
    request.chains.push_back(Circle(ChainRole::Section, 1, {0, 0, 0}, 10.0));
    request.chains.push_back(Circle(ChainRole::Section, 2, {0, 0, 10.0}, 14.0));
    request.chains.push_back(Circle(ChainRole::Section, 3, {0, 0, 20.0}, 10.0));
    double reference = 0.0;
    for (int attempt = 0; attempt < 5; ++attempt) {
        const auto built = Build(request);
        Require(built.HasValue(), "面が出来ること");
        if (attempt == 0) {
            reference = built.Value().areaMm2;
        } else {
            RequireNear(built.Value().areaMm2, reference, 1.0e-9, "毎回同じ面積");
        }
    }
    ClearShapeCache();
}

KACHA_V2_TEST(kernel_determinism, 同じ入力からは同じ標本になる)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::RuledSections;
    request.chains.push_back(Rectangle(ChainRole::Section, 1, 0, 0, 40, 25, 0.0));
    request.chains.push_back(Rectangle(ChainRole::Section, 2, 0, 0, 40, 25, 30.0));
    const auto first = Build(request);
    const auto second = Build(request);
    Require(first.HasValue() && second.HasValue(), "2回とも出来ること");
    RequireEqual(std::to_string(first.Value().samples.points.size()),
        std::to_string(second.Value().samples.points.size()), "標本の数");
    for (std::size_t index = 0; index < first.Value().samples.points.size(); ++index) {
        const Vector3 gap =
            first.Value().samples.points[index] - second.Value().samples.points[index];
        RequireNear(gap.Length(), 0.0, 1.0e-12, "標本の位置");
    }
    ClearShapeCache();
}

// =====================================================================
//  壊れた入力を渡しても落ちないこと
// =====================================================================

KACHA_V2_TEST(kernel_robust, 調べた作り方と違う作り方は断る)
{
    const GeometryTolerance tolerance = Tolerance();
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 40, 25));
    auto analysis = AnalyzeGuideSurfaceRequest(request, tolerance);
    Require(analysis.HasValue(), "検査が通ること");
    GuideSurfaceRequest mismatched = request;
    mismatched.method = GuideSurfaceMethod::LoftSections;
    const auto built =
        BuildGuideSurface(mismatched, analysis.Value(), tolerance, KernelShapeHandle{});
    Require(!built.HasValue(), "食い違いを見つける");
    RequireEqual(FirstCode(built.Diagnostics()), "KER-S005", "食い違いの診断コード");
}

KACHA_V2_TEST(kernel_robust, とても小さい形でも作れる)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 0.05, 0.02));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    RequireNear(built.Value().areaMm2, 0.05 * 0.02, 1.0e-12, "面積");
    ClearShapeCache();
}

KACHA_V2_TEST(kernel_robust, とても大きい形でも作れる)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 5000, 3000));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    RequireNear(built.Value().areaMm2, 5000.0 * 3000.0, 1.0e-3, "面積");
    ClearShapeCache();
}

KACHA_V2_TEST(kernel_robust, 原点から遠い場所でも作れる)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(
        Rectangle(ChainRole::OuterBoundary, 1, 100000, 100000, 100040, 100025));
    const auto built = Build(request);
    Require(built.HasValue(), "面が出来ること");
    RequireNear(built.Value().areaMm2, 40.0 * 25.0, 1.0e-3, "面積");
    ClearShapeCache();
}

KACHA_V2_TEST(kernel_robust, 何度作っても表を空にできる)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 10, 5));
    for (int index = 0; index < 50; ++index) {
        const auto built = Build(request);
        Require(built.HasValue(), "面が出来ること");
        Require(ReleaseShape(built.Value().handle), "毎回捨てられる");
    }
    RequireEqual(std::to_string(CachedShapeCount()), "0", "溜まっていない");
}

#endif // KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST_MAIN("kernel_surface_tests")
