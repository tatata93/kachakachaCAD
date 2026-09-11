// 形状ガイドの入力検査(AT-GEO-001〜007)。
// ここは「作れないものを作れないと言う」層。V1が変な面を出したのは、
// この判断が無いまま OCCT へ渡し、返ってきたそれらしい何かを採用したため。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::AnalyzeGuideSurfaceRequest;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::CheckSurfaceFit;
using kachakacha::v2::modeling::GuideChain;
using kachakacha::v2::modeling::GuideSurfaceMethod;
using kachakacha::v2::modeling::GuideSurfaceRequest;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

void RequireCount(std::size_t actual, std::size_t expected, const std::string& why)
{
    RequireEqual(std::to_string(actual), std::to_string(expected), why);
}

[[nodiscard]] GeometryTolerance Tolerance()
{
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = 1.0e-6;
    tolerance.interactiveJoinMm = 0.01;
    return tolerance;
}

[[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れること");
    return made.Value();
}

//! 閉じた四角の輪郭。
[[nodiscard]] GuideChain Rectangle(ChainRole role, int index, double x0, double y0,
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

//! 閉じた円の輪郭。
[[nodiscard]] GuideChain Circle(ChainRole role, int index, Vector3 center, double radius)
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

//! 開いた1本の線。
[[nodiscard]] GuideChain OpenLine(ChainRole role, int index, Vector3 a, Vector3 b)
{
    GuideChain chain;
    chain.role = role;
    chain.index = index;
    chain.closed = false;
    chain.segments = {Line(a, b)};
    return chain;
}

//! 折れ線ぶんの開いた鎖。
[[nodiscard]] GuideChain OpenPath(ChainRole role, int index, std::vector<Vector3> points)
{
    GuideChain chain;
    chain.role = role;
    chain.index = index;
    chain.closed = false;
    for (std::size_t at = 1; at < points.size(); ++at) {
        chain.segments.push_back(Line(points[at - 1], points[at]));
    }
    return chain;
}

void RequireRejects(const GuideSurfaceRequest& request, const std::string& expectedCode,
    const std::string& why)
{
    const auto result = AnalyzeGuideSurfaceRequest(request, Tolerance());
    Require(!result.HasValue(), "断ること: " + why);
    Require(!result.Diagnostics().empty(), "診断が付くこと: " + why);
    const bool found = std::any_of(result.Diagnostics().begin(), result.Diagnostics().end(),
        [&](const auto& diagnostic) { return diagnostic.code == expectedCode; });
    Require(found, "診断コード " + expectedCode + ": " + why + " (実際 "
            + result.Diagnostics().front().code + " / "
            + result.Diagnostics().front().summaryJa + ")");
}

[[nodiscard]] auto Accept(const GuideSurfaceRequest& request, const std::string& why)
{
    auto result = AnalyzeGuideSurfaceRequest(request, Tolerance());
    Require(result.HasValue(), "受け入れること: " + why + " ("
            + (result.Diagnostics().empty() ? std::string("診断なし")
                                            : result.Diagnostics().front().summaryJa)
            + ")");
    return result;
}

} // namespace

// ---------------------------------------------------------------- AT-GEO-001

KACHA_V2_TEST(guideSurface, 平面の外周と穴を分ける)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 100, 60));
    request.chains.push_back(Circle(ChainRole::HoleBoundary, 1, {30.0, 30.0, 0.0}, 8.0));
    const auto result = Accept(request, "外周1・穴1");
    RequireCount(result.Value().planarLoops.size(), 2, "輪郭の数");
    Require(!result.Value().planarLoops[0].isHole, "1つ目は外周");
    Require(result.Value().planarLoops[1].isHole, "2つ目は穴");
    RequireCount(result.Value().planarLoops[0].holes.size(), 1, "外周に属する穴の数");
    RequireNear(result.Value().planeFit.maximumDeviationMm, 0.0, 1e-9, "平面からのずれ");
}

KACHA_V2_TEST(guideSurface, 穴が複数でも外周へ結びつく)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 100, 60));
    request.chains.push_back(Circle(ChainRole::HoleBoundary, 1, {20.0, 30.0, 0.0}, 6.0));
    request.chains.push_back(Circle(ChainRole::HoleBoundary, 2, {50.0, 30.0, 0.0}, 6.0));
    request.chains.push_back(Rectangle(ChainRole::HoleBoundary, 3, 70, 20, 90, 40));
    const auto result = Accept(request, "外周1・穴3");
    RequireCount(result.Value().planarLoops[0].holes.size(), 3, "穴の数");
}

KACHA_V2_TEST(guideSurface, 曲線を含む外周も扱える)
{
    // 直線2本と円弧2本でできた角丸の枠。
    GuideChain chain;
    chain.role = ChainRole::OuterBoundary;
    chain.index = 1;
    chain.closed = true;
    chain.segments = {
        Line({10.0, 0.0, 0.0}, {90.0, 0.0, 0.0}),
        CurveSegment::MakeCircularArc({90.0, 10.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, -1.0, 0.0},
            10.0, 0.0, 1.5707963267948966)
            .Value(),
        Line({100.0, 10.0, 0.0}, {100.0, 50.0, 0.0}),
        Line({100.0, 50.0, 0.0}, {10.0, 50.0, 0.0}),
        Line({10.0, 50.0, 0.0}, {10.0, 0.0, 0.0}),
    };
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(chain);
    const auto result = Accept(request, "曲線を含む外周");
    RequireCount(result.Value().planarLoops.size(), 1, "輪郭の数");
    Require(!result.Value().planarLoops[0].isHole, "外周であること");
}

KACHA_V2_TEST(guideSurface, 傾いた平面でも扱える)
{
    // z = x の面に載る四角。
    GuideChain chain;
    chain.role = ChainRole::OuterBoundary;
    chain.index = 1;
    chain.closed = true;
    chain.segments = {
        Line({0.0, 0.0, 0.0}, {10.0, 0.0, 10.0}),
        Line({10.0, 0.0, 10.0}, {10.0, 20.0, 10.0}),
        Line({10.0, 20.0, 10.0}, {0.0, 20.0, 0.0}),
        Line({0.0, 20.0, 0.0}, {0.0, 0.0, 0.0}),
    };
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(chain);
    const auto result = Accept(request, "傾いた四角");
    RequireNear(result.Value().planeFit.maximumDeviationMm, 0.0, 1e-9, "ずれ0");
}

KACHA_V2_TEST(guideSurface, 同一平面に載らない輪郭を断る)
{
    GuideChain chain;
    chain.role = ChainRole::OuterBoundary;
    chain.index = 1;
    chain.closed = true;
    chain.segments = {
        Line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}),
        Line({10.0, 0.0, 0.0}, {10.0, 10.0, 5.0}),   // ここだけ持ち上がっている
        Line({10.0, 10.0, 5.0}, {0.0, 10.0, 0.0}),
        Line({0.0, 10.0, 0.0}, {0.0, 0.0, 0.0}),
    };
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(chain);
    RequireRejects(request, "GEO-G001", "ねじれた四角");
}

KACHA_V2_TEST(guideSurface, わずかに浮いた輪郭も許容差で断る)
{
    for (const double lift : {1.0e-5, 1.0e-4, 1.0e-3, 0.1}) {
        GuideChain chain;
        chain.role = ChainRole::OuterBoundary;
        chain.index = 1;
        chain.closed = true;
        chain.segments = {
            Line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}),
            Line({10.0, 0.0, 0.0}, {10.0, 10.0, lift}),
            Line({10.0, 10.0, lift}, {0.0, 10.0, 0.0}),
            Line({0.0, 10.0, 0.0}, {0.0, 0.0, 0.0}),
        };
        GuideSurfaceRequest request;
        request.method = GuideSurfaceMethod::PlanarBoundary;
        request.chains.push_back(chain);
        RequireRejects(request, "GEO-G001", "浮き " + std::to_string(lift) + " mm");
    }
}

KACHA_V2_TEST(guideSurface, 自己交差する輪郭を断る)
{
    // 8の字。
    GuideChain chain;
    chain.role = ChainRole::OuterBoundary;
    chain.index = 1;
    chain.closed = true;
    chain.segments = {
        Line({0.0, 0.0, 0.0}, {10.0, 10.0, 0.0}),
        Line({10.0, 10.0, 0.0}, {10.0, 0.0, 0.0}),
        Line({10.0, 0.0, 0.0}, {0.0, 10.0, 0.0}),
        Line({0.0, 10.0, 0.0}, {0.0, 0.0, 0.0}),
    };
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(chain);
    RequireRejects(request, "GEO-G007", "8の字");
}

KACHA_V2_TEST(guideSurface, 重なった外周どうしを断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 20, 20));
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 2, 10, 10, 30, 30));
    RequireRejects(request, "GEO-G007", "重なる外周");
}

KACHA_V2_TEST(guideSurface, 外周に接する穴を断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 20, 20));
    // 右辺にちょうど触れる穴。
    request.chains.push_back(Rectangle(ChainRole::HoleBoundary, 1, 10, 5, 20, 15));
    RequireRejects(request, "GEO-G007", "外周に接する穴");
}

KACHA_V2_TEST(guideSurface, 離れた外周が複数なら知らせる)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 20, 20));
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 2, 50, 0, 70, 20));
    const auto result = Accept(request, "離れた外周2つ");
    const bool told = std::any_of(result.Value().notes.begin(), result.Value().notes.end(),
        [](const auto& note) { return note.code == "GEO-G102"; });
    Require(told, "複数の面になることを知らせること");
}

KACHA_V2_TEST(guideSurface, 内外の指定が逆でも位置関係で決め直す)
{
    // 利用者が外周と穴を取り違えて選んだ場合。位置関係が正であって、指定は参考。
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::HoleBoundary, 1, 0, 0, 100, 60));
    request.chains.push_back(Circle(ChainRole::OuterBoundary, 1, {30.0, 30.0, 0.0}, 8.0));
    const auto result = Accept(request, "内外が逆の指定");
    Require(!result.Value().planarLoops[0].isHole, "大きいほうが外周");
    Require(result.Value().planarLoops[1].isHole, "小さいほうが穴");
    const bool told = std::any_of(result.Value().notes.begin(), result.Value().notes.end(),
        [](const auto& note) { return note.code == "GEO-G101"; });
    Require(told, "直したことを黙っていないこと");
}

KACHA_V2_TEST(guideSurface, 入れ子が3重でも正しく分かれる)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 100, 100));
    request.chains.push_back(Rectangle(ChainRole::HoleBoundary, 1, 20, 20, 80, 80));
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 2, 40, 40, 60, 60));
    const auto result = Accept(request, "3重の入れ子");
    Require(!result.Value().planarLoops[0].isHole, "一番外は外周");
    Require(result.Value().planarLoops[1].isHole, "真ん中は穴");
    Require(!result.Value().planarLoops[2].isHole, "一番内は外周");
    RequireCount(result.Value().planarLoops[0].holes.size(), 1, "外周の穴");
}

KACHA_V2_TEST(guideSurface, 開いた輪郭では平面を作らない)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(OpenLine(ChainRole::OuterBoundary, 1, {0, 0, 0}, {10, 0, 0}));
    RequireRejects(request, "GEO-G009", "開いた線");
}

KACHA_V2_TEST(guideSurface, 輪郭が無ければ断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    RequireRejects(request, "GEO-G009", "入力なし");
}

KACHA_V2_TEST(guideSurface, 穴だけでは断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::HoleBoundary, 1, 0, 0, 20, 20));
    request.chains.push_back(Rectangle(ChainRole::HoleBoundary, 2, 30, 0, 50, 20));
    // どちらも他に含まれないので、両方とも外周になる。これは正しく受け入れる。
    const auto result = Accept(request, "含まれない2つは外周扱い");
    Require(!result.Value().planarLoops[0].isHole, "外周1");
    Require(!result.Value().planarLoops[1].isHole, "外周2");
}

// ---------------------------------------------------------------- AT-GEO-002

KACHA_V2_TEST(guideSurface, 2断面から面が作れる)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::RuledSections;
    request.chains.push_back(OpenPath(ChainRole::Section, 1,
        {{0, 0, 0}, {10, 5, 0}, {20, 0, 0}}));
    request.chains.push_back(OpenPath(ChainRole::Section, 2,
        {{0, 0, 30}, {10, 8, 30}, {20, 0, 30}}));
    const auto result = Accept(request, "2断面");
    RequireCount(result.Value().sectionOrdering.chainIndices.size(), 2, "断面の数");
}

KACHA_V2_TEST(guideSurface, 向きが逆の断面でもねじれないよう揃える)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::RuledSections;
    request.chains.push_back(OpenPath(ChainRole::Section, 1,
        {{0, 0, 0}, {10, 5, 0}, {20, 0, 0}}));
    // 2本目だけ逆向きに描いてある。
    request.chains.push_back(OpenPath(ChainRole::Section, 2,
        {{20, 0, 30}, {10, 8, 30}, {0, 0, 30}}));
    const auto result = Accept(request, "向きが逆の断面");
    const bool told = std::any_of(result.Value().notes.begin(), result.Value().notes.end(),
        [](const auto& note) { return note.code == "GEO-G103"; });
    Require(told, "向きを揃え直したことを知らせること");
}

KACHA_V2_TEST(guideSurface, 開いた断面と閉じた断面を混ぜたら断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::RuledSections;
    request.chains.push_back(OpenPath(ChainRole::Section, 1,
        {{0, 0, 0}, {10, 5, 0}, {20, 0, 0}}));
    request.chains.push_back(Rectangle(ChainRole::Section, 2, 0, 0, 20, 10, 30.0));
    RequireRejects(request, "GEO-G002", "open と closed が混在");
}

KACHA_V2_TEST(guideSurface, 同じ位置の2断面を断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::RuledSections;
    request.chains.push_back(OpenPath(ChainRole::Section, 1,
        {{0, 0, 0}, {10, 5, 0}, {20, 0, 0}}));
    request.chains.push_back(OpenPath(ChainRole::Section, 2,
        {{0, 0, 0}, {10, 5, 0}, {20, 0, 0}}));
    RequireRejects(request, "GEO-G003", "重なった断面");
}

KACHA_V2_TEST(guideSurface, 2断面の方法に3断面を渡したら断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::RuledSections;
    for (int index = 0; index < 3; ++index) {
        request.chains.push_back(OpenPath(ChainRole::Section, index + 1,
            {{0, 0, index * 10.0}, {20, 0, index * 10.0}}));
    }
    RequireRejects(request, "GEO-G009", "断面が3本");
}

KACHA_V2_TEST(guideSurface, 断面が1本では断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::RuledSections;
    request.chains.push_back(OpenPath(ChainRole::Section, 1, {{0, 0, 0}, {20, 0, 0}}));
    RequireRejects(request, "GEO-G009", "断面が1本");
}

KACHA_V2_TEST(guideSurface, 閉じた断面2つでも作れる)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::RuledSections;
    request.chains.push_back(Circle(ChainRole::Section, 1, {0.0, 0.0, 0.0}, 10.0));
    request.chains.push_back(Circle(ChainRole::Section, 2, {0.0, 0.0, 40.0}, 6.0));
    const auto result = Accept(request, "閉じた断面2つ");
    RequireCount(result.Value().sectionOrdering.chainIndices.size(), 2, "断面の数");
}

// ---------------------------------------------------------------- AT-GEO-003

KACHA_V2_TEST(guideSurface, 5断面を重心の並びで順序付ける)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::LoftSections;
    // わざと順不同で渡す。
    const double heights[]{40.0, 0.0, 30.0, 10.0, 20.0};
    for (int index = 0; index < 5; ++index) {
        const double z = heights[index];
        request.chains.push_back(OpenPath(ChainRole::Section, index + 1,
            {{0, 0, z}, {10, 5 + z * 0.1, z}, {20, 0, z}}));
    }
    const auto result = Accept(request, "5断面");
    const auto& order = result.Value().sectionOrdering.chainIndices;
    RequireCount(order.size(), 5, "断面の数");
    // 並べ替えた結果が z の昇順になっていること。
    double previous = -1.0;
    for (const std::size_t index : order) {
        const double z = request.chains[index].segments.front().StartPoint().z;
        Require(z > previous, "zの昇順であること");
        previous = z;
    }
}

KACHA_V2_TEST(guideSurface, Segment数が違う断面でも扱える)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::LoftSections;
    request.chains.push_back(OpenPath(ChainRole::Section, 1, {{0, 0, 0}, {20, 0, 0}}));
    request.chains.push_back(OpenPath(ChainRole::Section, 2,
        {{0, 0, 10}, {5, 3, 10}, {10, 4, 10}, {15, 3, 10}, {20, 0, 10}}));
    request.chains.push_back(OpenPath(ChainRole::Section, 3,
        {{0, 0, 20}, {10, 2, 20}, {20, 0, 20}}));
    const auto result = Accept(request, "Segment数が違う断面");
    RequireCount(result.Value().sectionOrdering.chainIndices.size(), 3, "断面の数");
}

KACHA_V2_TEST(guideSurface, 断面の並べ替えが決定的である)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::LoftSections;
    for (int index = 0; index < 6; ++index) {
        const double z = static_cast<double>((index * 7) % 6) * 10.0;
        request.chains.push_back(OpenPath(ChainRole::Section, index + 1,
            {{0, 0, z}, {20, 0, z}}));
    }
    const auto first = Accept(request, "1回目");
    for (int repeat = 0; repeat < 5; ++repeat) {
        const auto again = Accept(request, "繰り返し");
        Require(again.Value().sectionOrdering.chainIndices
                == first.Value().sectionOrdering.chainIndices,
            "何度やっても同じ順になること");
    }
}

KACHA_V2_TEST(guideSurface, 断面が2本ではLoftにできない)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::LoftSections;
    request.chains.push_back(OpenPath(ChainRole::Section, 1, {{0, 0, 0}, {20, 0, 0}}));
    request.chains.push_back(OpenPath(ChainRole::Section, 2, {{0, 0, 10}, {20, 0, 10}}));
    RequireRejects(request, "GEO-G009", "断面が2本");
}

// ---------------------------------------------------------------- AT-GEO-004

namespace {

//! 両端で接続した2本のガイドと、その間を渡る断面。オーナー提示ケースの形。
[[nodiscard]] GuideSurfaceRequest MakeGuidedLoftFixture()
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::GuidedLoft;
    // ガイドは x 方向に伸びる2本。y = 0 と y = 40。両端(x=0, x=100)で断面がつなぐ。
    request.chains.push_back(OpenPath(ChainRole::GuideU, 1,
        {{0, 0, 0}, {25, 0, 8}, {50, 0, 10}, {75, 0, 8}, {100, 0, 0}}));
    request.chains.push_back(OpenPath(ChainRole::GuideU, 2,
        {{0, 40, 0}, {25, 40, 5}, {50, 40, 6}, {75, 40, 5}, {100, 40, 0}}));
    // 断面はガイドの間を渡る。ガイド上の点から始まり、ガイド上の点で終わる。
    request.chains.push_back(OpenPath(ChainRole::Section, 1,
        {{0, 0, 0}, {0, 20, 3}, {0, 40, 0}}));
    request.chains.push_back(OpenPath(ChainRole::Section, 2,
        {{50, 0, 10}, {50, 20, 12}, {50, 40, 6}}));
    request.chains.push_back(OpenPath(ChainRole::Section, 3,
        {{100, 0, 0}, {100, 20, 3}, {100, 40, 0}}));
    return request;
}

} // namespace

KACHA_V2_TEST(guideSurface, 両端接続ガイドと3断面を受け入れる)
{
    const auto result = Accept(MakeGuidedLoftFixture(), "ガイド2・断面3");
    RequireCount(result.Value().crossings.size(), 6, "接続の数(断面3×ガイド2)");
    RequireCount(result.Value().sectionOrdering.chainIndices.size(), 3, "断面の順");
    Require(result.Value().virtualSectionParameters.empty(),
        "両端に断面があるので仮想断面は要らない");
}

KACHA_V2_TEST(guideSurface, 断面の端がガイド内部に接していても受け入れる)
{
    GuideSurfaceRequest request = MakeGuidedLoftFixture();
    // 真ん中の断面の端を、ガイドのSegment内部(節点でない場所)へ移す。
    request.chains[3] = OpenPath(ChainRole::Section, 2,
        {{37.5, 0, 9.0}, {37.5, 20, 11.0}, {37.5, 40, 5.5}});
    const auto result = Accept(request, "Segment内部への接続");
    RequireCount(result.Value().crossings.size(), 6, "接続の数");
}

KACHA_V2_TEST(guideSurface, ガイドから離れた断面を断る)
{
    for (const double gap : {0.05, 0.5, 5.0}) {
        GuideSurfaceRequest request = MakeGuidedLoftFixture();
        request.chains[3] = OpenPath(ChainRole::Section, 2,
            {{50, gap, 10}, {50, 20, 12}, {50, 40, 6}});
        RequireRejects(request, "GEO-G004", "隙間 " + std::to_string(gap) + " mm");
    }
}

KACHA_V2_TEST(guideSurface, 断面の交差順がガイドで食い違えば断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::GuidedLoft;
    request.chains.push_back(OpenPath(ChainRole::GuideU, 1,
        {{0, 0, 0}, {50, 0, 0}, {100, 0, 0}}));
    // 2本目のガイドは向きが逆(x=100 から始まる)。断面の交差順が入れ替わる。
    request.chains.push_back(OpenPath(ChainRole::GuideU, 2,
        {{100, 40, 0}, {50, 40, 0}, {0, 40, 0}}));
    request.chains.push_back(OpenPath(ChainRole::Section, 1,
        {{0, 0, 0}, {0, 20, 2}, {0, 40, 0}}));
    request.chains.push_back(OpenPath(ChainRole::Section, 2,
        {{100, 0, 0}, {100, 20, 2}, {100, 40, 0}}));
    RequireRejects(request, "GEO-G006", "ガイドの向きが逆");
}

KACHA_V2_TEST(guideSurface, 端に断面が無ければ仮想断面を作る)
{
    GuideSurfaceRequest request = MakeGuidedLoftFixture();
    // 両端の断面を消す。真ん中の1本だけ残す。
    request.chains.erase(request.chains.begin() + 4);
    request.chains.erase(request.chains.begin() + 2);
    const auto result = Accept(request, "端に断面が無い");
    RequireCount(result.Value().virtualSectionParameters.size(), 2, "仮想断面の数");
    const bool told = std::any_of(result.Value().notes.begin(), result.Value().notes.end(),
        [](const auto& note) { return note.code == "GEO-G104"; });
    Require(told, "仮想断面を作ったことを知らせること");
}

KACHA_V2_TEST(guideSurface, 仮想断面を作らない設定も効く)
{
    GuideSurfaceRequest request = MakeGuidedLoftFixture();
    request.chains.erase(request.chains.begin() + 4);
    request.chains.erase(request.chains.begin() + 2);
    request.createVirtualEndSections = false;
    const auto result = Accept(request, "仮想断面なし");
    Require(result.Value().virtualSectionParameters.empty(), "作らないこと");
}

KACHA_V2_TEST(guideSurface, ガイドが2本でなければ断る)
{
    GuideSurfaceRequest one;
    one.method = GuideSurfaceMethod::GuidedLoft;
    one.chains.push_back(OpenPath(ChainRole::GuideU, 1, {{0, 0, 0}, {100, 0, 0}}));
    one.chains.push_back(OpenPath(ChainRole::Section, 1, {{0, 0, 0}, {0, 40, 0}}));
    RequireRejects(one, "GEO-G009", "ガイド1本");

    GuideSurfaceRequest three = MakeGuidedLoftFixture();
    three.chains.push_back(OpenPath(ChainRole::GuideU, 3,
        {{0, 80, 0}, {50, 80, 0}, {100, 80, 0}}));
    RequireRejects(three, "GEO-G009", "ガイド3本");
}

KACHA_V2_TEST(guideSurface, ガイドだけで断面が無ければ断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::GuidedLoft;
    request.chains.push_back(OpenPath(ChainRole::GuideU, 1, {{0, 0, 0}, {100, 0, 0}}));
    request.chains.push_back(OpenPath(ChainRole::GuideU, 2, {{0, 40, 0}, {100, 40, 0}}));
    RequireRejects(request, "GEO-G009", "断面なし");
}

KACHA_V2_TEST(guideSurface, 閉じた断面はガイド付きロフトで断る)
{
    GuideSurfaceRequest request = MakeGuidedLoftFixture();
    request.chains[2] = Circle(ChainRole::Section, 1, {0.0, 20.0, 0.0}, 5.0);
    RequireRejects(request, "GEO-G009", "閉じた断面");
}

// ---------------------------------------------------------------- AT-GEO-005

namespace {

//! U3本・V4本の格子。全部が1回ずつ交わる。
[[nodiscard]] GuideSurfaceRequest MakeGordonFixture()
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::GordonNetwork;
    const double vPositions[]{0.0, 30.0, 60.0, 90.0};
    const double uPositions[]{0.0, 25.0, 50.0};
    for (int u = 0; u < 3; ++u) {
        // y = uPositions[u] を x 方向に走る線。
        request.chains.push_back(OpenPath(ChainRole::GuideU, u + 1,
            {{0, uPositions[u], 0}, {45, uPositions[u], 0}, {90, uPositions[u], 0}}));
    }
    for (int v = 0; v < 4; ++v) {
        request.chains.push_back(OpenPath(ChainRole::GuideV, v + 1,
            {{vPositions[v], 0, 0}, {vPositions[v], 25, 0}, {vPositions[v], 50, 0}}));
    }
    return request;
}

} // namespace

KACHA_V2_TEST(guideSurface, U3本V4本の網を受け入れる)
{
    const auto result = Accept(MakeGordonFixture(), "3×4の網");
    RequireCount(result.Value().crossings.size(), 12, "交差の数");
}

KACHA_V2_TEST(guideSurface, 網は順不同で渡しても同じ結果になる)
{
    GuideSurfaceRequest normal = MakeGordonFixture();
    GuideSurfaceRequest shuffled;
    shuffled.method = GuideSurfaceMethod::GordonNetwork;
    // 入れる順番だけ変える。
    const std::size_t order[]{4, 0, 5, 2, 6, 1, 3};
    for (const std::size_t index : order) {
        shuffled.chains.push_back(normal.chains[index]);
    }
    const auto first = Accept(normal, "そのまま");
    const auto second = Accept(shuffled, "順不同");
    RequireCount(second.Value().crossings.size(), first.Value().crossings.size(), "交差の数");
}

KACHA_V2_TEST(guideSurface, 交差が1つ欠けたら断る)
{
    GuideSurfaceRequest request = MakeGordonFixture();
    // V の1本を短くして、U の一番遠い線と交わらないようにする。
    request.chains[3] = OpenPath(ChainRole::GuideV, 1, {{0, 0, 0}, {0, 20, 0}});
    RequireRejects(request, "GEO-G005", "交差が欠けている");
}

KACHA_V2_TEST(guideSurface, 2回交わる線を断る)
{
    // V の1本が、U の1本を2回またぐ網。1回ずつ交わるという前提が崩れている。
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::GordonNetwork;
    request.chains.push_back(OpenPath(ChainRole::GuideU, 1, {{0, 0, 0}, {90, 0, 0}}));
    request.chains.push_back(OpenPath(ChainRole::GuideU, 2, {{0, 50, 0}, {90, 50, 0}}));
    request.chains.push_back(OpenPath(ChainRole::GuideV, 1,
        {{0, -10, 0}, {0, 20, 0}, {0, 60, 0}}));
    request.chains.push_back(OpenPath(ChainRole::GuideV, 2,
        {{60, 10, 0}, {60, -10, 0}, {40, -10, 0}, {40, 10, 0}, {40, 50, 0}}));
    RequireRejects(request, "GEO-G005", "1本のUを2回またぐV");
}

KACHA_V2_TEST(guideSurface, 交差の順が逆転していたら断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::GordonNetwork;
    request.chains.push_back(OpenPath(ChainRole::GuideU, 1,
        {{0, 0, 0}, {30, 0, 0}, {60, 0, 0}}));
    // 2本目のU線は逆向きに描いてある。V線と交わる順が入れ替わる。
    request.chains.push_back(OpenPath(ChainRole::GuideU, 2,
        {{60, 40, 0}, {30, 40, 0}, {0, 40, 0}}));
    request.chains.push_back(OpenPath(ChainRole::GuideV, 1,
        {{0, 0, 0}, {0, 20, 0}, {0, 40, 0}}));
    request.chains.push_back(OpenPath(ChainRole::GuideV, 2,
        {{60, 0, 0}, {60, 20, 0}, {60, 40, 0}}));
    RequireRejects(request, "GEO-G006", "順が逆転");
}

KACHA_V2_TEST(guideSurface, U方向が1本では網にならない)
{
    GuideSurfaceRequest request = MakeGordonFixture();
    request.chains.erase(request.chains.begin() + 1, request.chains.begin() + 3);
    RequireRejects(request, "GEO-G009", "U が1本");
}

KACHA_V2_TEST(guideSurface, V方向が1本では網にならない)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::GordonNetwork;
    request.chains.push_back(OpenPath(ChainRole::GuideU, 1, {{0, 0, 0}, {60, 0, 0}}));
    request.chains.push_back(OpenPath(ChainRole::GuideU, 2, {{0, 40, 0}, {60, 40, 0}}));
    request.chains.push_back(OpenPath(ChainRole::GuideV, 1, {{0, 0, 0}, {0, 40, 0}}));
    RequireRejects(request, "GEO-G009", "V が1本");
}

// ---------------------------------------------------------------- AT-GEO-006

KACHA_V2_TEST(guideSurface, 3辺の境界を受け入れる)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::BoundaryFill;
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 1, {0, 0, 0}, {10, 0, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 2, {10, 0, 0}, {5, 8, 3}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 3, {5, 8, 3}, {0, 0, 0}));
    const auto result = Accept(request, "3辺");
    RequireCount(result.Value().sectionOrdering.chainIndices.size(), 3, "辺の数");
}

KACHA_V2_TEST(guideSurface, 4辺の境界を受け入れる)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::BoundaryFill;
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 1, {0, 0, 0}, {10, 0, 2}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 2, {10, 0, 2}, {10, 10, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 3, {10, 10, 0}, {0, 10, 3}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 4, {0, 10, 3}, {0, 0, 0}));
    const auto result = Accept(request, "4辺");
    const bool told = std::any_of(result.Value().notes.begin(), result.Value().notes.end(),
        [](const auto& note) { return note.code == "GEO-G105"; });
    Require(told, "非平面であることを知らせること");
}

KACHA_V2_TEST(guideSurface, 辺を順不同で渡しても輪にできる)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::BoundaryFill;
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 1, {10, 10, 0}, {0, 10, 3}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 2, {0, 0, 0}, {10, 0, 2}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 3, {0, 10, 3}, {0, 0, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 4, {10, 0, 2}, {10, 10, 0}));
    const auto result = Accept(request, "順不同の4辺");
    RequireCount(result.Value().sectionOrdering.chainIndices.size(), 4, "辺の数");
}

KACHA_V2_TEST(guideSurface, 5辺は勝手に分割せず断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::BoundaryFill;
    const Vector3 points[]{{0, 0, 0}, {10, 0, 0}, {14, 8, 2}, {7, 14, 1}, {-2, 8, 2}};
    for (int index = 0; index < 5; ++index) {
        request.chains.push_back(OpenLine(ChainRole::BoundarySide, index + 1, points[index],
            points[(index + 1) % 5]));
    }
    const auto result = AnalyzeGuideSurfaceRequest(request, Tolerance());
    Require(!result.HasValue(), "断ること");
    Require(result.Diagnostics().front().detailsJa.find("Gordon") != std::string::npos
            || result.Diagnostics().front().detailsJa.find("曲線網") != std::string::npos,
        "代わりの方法を示すこと");
}

KACHA_V2_TEST(guideSurface, 2辺では断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::BoundaryFill;
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 1, {0, 0, 0}, {10, 0, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 2, {10, 0, 0}, {0, 0, 0}));
    RequireRejects(request, "GEO-G009", "2辺");
}

KACHA_V2_TEST(guideSurface, 閉じていない境界を断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::BoundaryFill;
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 1, {0, 0, 0}, {10, 0, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 2, {10, 0, 0}, {5, 8, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 3, {5, 8, 0}, {1, 1, 0}));
    RequireRejects(request, "GEO-G004", "端が閉じていない");
}

KACHA_V2_TEST(guideSurface, 離れた辺を断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::BoundaryFill;
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 1, {0, 0, 0}, {10, 0, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 2, {10, 0, 0}, {5, 8, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 3, {50, 50, 0}, {0, 0, 0}));
    RequireRejects(request, "GEO-G004", "1辺だけ離れている");
}

KACHA_V2_TEST(guideSurface, 連続条件の数が辺と合わなければ断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::BoundaryFill;
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 1, {0, 0, 0}, {10, 0, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 2, {10, 0, 0}, {5, 8, 0}));
    request.chains.push_back(OpenLine(ChainRole::BoundarySide, 3, {5, 8, 0}, {0, 0, 0}));
    request.tangentContinuity = {true, false};
    RequireRejects(request, "GEO-G009", "条件が2個で辺が3本");
}

// ---------------------------------------------------------------- OffsetGuide

KACHA_V2_TEST(guideSurface, オフセットは距離が必要)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::OffsetGuide;
    request.chains.push_back(Rectangle(ChainRole::SourceSurface, 1, 0, 0, 10, 10));
    request.offsetDistanceMm = 0.0;
    RequireRejects(request, "GEO-G009", "距離0");

    request.offsetDistanceMm = 2.5;
    Require(AnalyzeGuideSurfaceRequest(request, Tolerance()).HasValue(), "距離があれば通る");

    request.offsetDistanceMm = -2.5;
    Require(AnalyzeGuideSurfaceRequest(request, Tolerance()).HasValue(), "負の距離も通る");
}

KACHA_V2_TEST(guideSurface, オフセットの元は1つだけ)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::OffsetGuide;
    request.chains.push_back(Rectangle(ChainRole::SourceSurface, 1, 0, 0, 10, 10));
    request.chains.push_back(Rectangle(ChainRole::SourceSurface, 2, 20, 0, 30, 10));
    request.offsetDistanceMm = 2.0;
    RequireRejects(request, "GEO-G009", "元が2つ");
}

// ---------------------------------------------------------------- 共通の入力検査

KACHA_V2_TEST(guideSurface, 同じ役割で番号が重なったら断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 0, 0, 10, 10));
    request.chains.push_back(Rectangle(ChainRole::OuterBoundary, 1, 20, 0, 30, 10));
    RequireRejects(request, "GEO-G009", "番号が重複");
}

KACHA_V2_TEST(guideSurface, 番号が0以下なら断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    GuideChain chain = Rectangle(ChainRole::OuterBoundary, 0, 0, 0, 10, 10);
    request.chains.push_back(chain);
    RequireRejects(request, "GEO-G009", "番号0");
}

KACHA_V2_TEST(guideSurface, 中身の無い線を断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    GuideChain empty;
    empty.role = ChainRole::OuterBoundary;
    empty.index = 1;
    empty.closed = true;
    request.chains.push_back(empty);
    RequireRejects(request, "GEO-G009", "Segmentが無い");
}

KACHA_V2_TEST(guideSurface, 役割の取り違えを断る)
{
    // 平面の面にガイドを渡す、など。
    GuideSurfaceRequest planar;
    planar.method = GuideSurfaceMethod::PlanarBoundary;
    planar.chains.push_back(OpenPath(ChainRole::GuideU, 1, {{0, 0, 0}, {10, 0, 0}}));
    RequireRejects(planar, "GEO-G009", "平面にガイド");

    GuideSurfaceRequest gordon;
    gordon.method = GuideSurfaceMethod::GordonNetwork;
    gordon.chains.push_back(OpenPath(ChainRole::GuideU, 1, {{0, 0, 0}, {60, 0, 0}}));
    gordon.chains.push_back(OpenPath(ChainRole::GuideU, 2, {{0, 40, 0}, {60, 40, 0}}));
    gordon.chains.push_back(OpenPath(ChainRole::GuideV, 1, {{0, 0, 0}, {0, 40, 0}}));
    gordon.chains.push_back(OpenPath(ChainRole::GuideV, 2, {{60, 0, 0}, {60, 40, 0}}));
    gordon.chains.push_back(OpenPath(ChainRole::Section, 1, {{30, 0, 0}, {30, 40, 0}}));
    RequireRejects(gordon, "GEO-G009", "網に断面");
}

KACHA_V2_TEST(guideSurface, 何度呼んでも同じ答えを返す)
{
    const GuideSurfaceRequest requests[]{
        MakeGuidedLoftFixture(),
        MakeGordonFixture(),
    };
    for (const GuideSurfaceRequest& request : requests) {
        const auto first = AnalyzeGuideSurfaceRequest(request, Tolerance());
        for (int repeat = 0; repeat < 4; ++repeat) {
            const auto again = AnalyzeGuideSurfaceRequest(request, Tolerance());
            RequireEqual(again.HasValue() ? "ok" : "ng", first.HasValue() ? "ok" : "ng",
                "同じ判定");
            if (first.HasValue()) {
                RequireCount(again.Value().crossings.size(), first.Value().crossings.size(),
                    "交差の数");
                for (std::size_t at = 0; at < first.Value().crossings.size(); ++at) {
                    RequireNear(again.Value().crossings[at].distanceMm,
                        first.Value().crossings[at].distanceMm, 0.0, "距離も同じ");
                }
            }
        }
    }
}

// ---------------------------------------------------------------- 面の偏差検査

KACHA_V2_TEST(guideSurface, 面が断面を通っているかを測れる)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::RuledSections;
    request.chains.push_back(OpenPath(ChainRole::Section, 1, {{0, 0, 0}, {20, 0, 0}}));
    request.chains.push_back(OpenPath(ChainRole::Section, 2, {{0, 0, 10}, {20, 0, 10}}));

    // 断面をちょうど通る面。
    std::vector<Vector3> good;
    for (int u = 0; u <= 20; ++u) {
        for (int v = 0; v <= 10; ++v) {
            good.push_back({static_cast<double>(u), 0.0, static_cast<double>(v)});
        }
    }
    const auto ok = CheckSurfaceFit(request, good, Tolerance());
    Require(ok.withinTolerance, "通っていること");
    Require(ok.maximumDeviationMm < 1e-9, "ずれがほぼ0");

    // 断面から 0.5mm ずれた面。
    std::vector<Vector3> bad;
    for (const Vector3& point : good) {
        bad.push_back({point.x, point.y + 0.5, point.z});
    }
    const auto ng = CheckSurfaceFit(request, bad, Tolerance());
    Require(!ng.withinTolerance, "ずれを見逃さないこと");
    RequireNear(ng.maximumDeviationMm, 0.5, 1e-9, "ずれの量");
}

KACHA_V2_TEST(guideSurface, どの入力が一番ずれているかを言える)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::LoftSections;
    request.chains.push_back(OpenPath(ChainRole::Section, 1, {{0, 0, 0}, {20, 0, 0}}));
    request.chains.push_back(OpenPath(ChainRole::Section, 2, {{0, 0, 10}, {20, 0, 10}}));
    request.chains.push_back(OpenPath(ChainRole::Section, 3, {{0, 3, 20}, {20, 3, 20}}));
    std::vector<Vector3> surface;
    for (int u = 0; u <= 20; ++u) {
        for (int v = 0; v <= 20; ++v) {
            surface.push_back({static_cast<double>(u), 0.0, static_cast<double>(v)});
        }
    }
    const auto check = CheckSurfaceFit(request, surface, Tolerance());
    Require(!check.withinTolerance, "ずれていること");
    RequireCount(check.worstChainIndex, 2, "3本目が一番ずれている");
}

// 面をずらす入力は、曲線ではなく既にある面への参照である。
// 線としての中身が無いことを理由に断ってはならない(PCの kernel_surface_tests で発覚)。
KACHA_V2_TEST(guideSurface, 面をずらす入力は線を持たなくてよい)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::OffsetGuide;
    request.offsetDistanceMm = 3.0;
    GuideChain chain;
    chain.role = ChainRole::SourceSurface;
    chain.index = 1;
    request.chains.push_back(chain);
    const auto analysis = AnalyzeGuideSurfaceRequest(request, Tolerance());
    Require(analysis.HasValue(), "線が無くても通ること");
    Require(analysis.Value().method == GuideSurfaceMethod::OffsetGuide, "作り方");
}

KACHA_V2_TEST(guideSurface, 面をずらす距離が0なら断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::OffsetGuide;
    request.offsetDistanceMm = 0.0;
    GuideChain chain;
    chain.role = ChainRole::SourceSurface;
    chain.index = 1;
    request.chains.push_back(chain);
    const auto analysis = AnalyzeGuideSurfaceRequest(request, Tolerance());
    Require(!analysis.HasValue(), "0は断る");
}

KACHA_V2_TEST(guideSurface, 面をずらす元が2つあれば断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::OffsetGuide;
    request.offsetDistanceMm = 3.0;
    for (int index = 1; index <= 2; ++index) {
        GuideChain chain;
        chain.role = ChainRole::SourceSurface;
        chain.index = index;
        request.chains.push_back(chain);
    }
    const auto analysis = AnalyzeGuideSurfaceRequest(request, Tolerance());
    Require(!analysis.HasValue(), "1つだけにさせる");
}

// 回転体(V1 の回転面)。断面 1 本を軸のまわりに回す。写しを並べたロフトではない。
KACHA_V2_TEST(guideSurface, 回転体は断面1本と軸と角度で通る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::Revolve;
    request.chains.push_back(OpenLine(ChainRole::Section, 1, {30, 0, -20}, {30, 0, 20}));
    request.revolveAxisPoint = {0, 0, 0};
    request.revolveAxisDirection = {0, 0, 1};
    request.revolveAngleRad = 2.0 * 3.14159265358979323846;
    const auto analysis = AnalyzeGuideSurfaceRequest(request, Tolerance());
    Require(analysis.HasValue(), "一周でも通ること");
    Require(analysis.Value().method == GuideSurfaceMethod::Revolve, "作り方");
    RequireCount(analysis.Value().sectionOrdering.chainIndices.size(), 1, "断面は 1 本");
}

KACHA_V2_TEST(guideSurface, 回転体は断面2本や角度0や軸上の断面を断る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::Revolve;
    request.chains.push_back(OpenLine(ChainRole::Section, 1, {30, 0, -20}, {30, 0, 20}));
    request.revolveAxisDirection = {0, 0, 1};
    request.revolveAngleRad = 1.0;
    Require(AnalyzeGuideSurfaceRequest(request, Tolerance()).HasValue(), "1 本なら通る");
    GuideSurfaceRequest two = request;
    two.chains.push_back(OpenLine(ChainRole::Section, 2, {40, 0, -20}, {40, 0, 20}));
    Require(!AnalyzeGuideSurfaceRequest(two, Tolerance()).HasValue(), "2 本は断る");
    GuideSurfaceRequest zero = request;
    zero.revolveAngleRad = 0.0;
    Require(!AnalyzeGuideSurfaceRequest(zero, Tolerance()).HasValue(), "角度 0 は断る");
    GuideSurfaceRequest tooFar = request;
    tooFar.revolveAngleRad = 7.0;
    Require(!AnalyzeGuideSurfaceRequest(tooFar, Tolerance()).HasValue(), "一周を超えると断る");
    GuideSurfaceRequest noAxis = request;
    noAxis.revolveAxisDirection = {0, 0, 0};
    Require(!AnalyzeGuideSurfaceRequest(noAxis, Tolerance()).HasValue(), "軸が無ければ断る");
    GuideSurfaceRequest onAxis = request;
    onAxis.chains = {OpenLine(ChainRole::Section, 1, {0, 0, -20}, {0, 0, 20})};
    const auto refused = AnalyzeGuideSurfaceRequest(onAxis, Tolerance());
    Require(!refused.HasValue(), "軸上の断面は断る");
    RequireEqual(refused.Diagnostics().front().code, std::string("GEO-G009"), "理由の番号");
}

KACHA_V2_TEST_MAIN("guide_surface_tests")
