// 線の上に置いて押す編集(トリム・延長・分割)の計画(app/HoverEditPlan)。
#include "kachakacha/app/HoverEditPlan.h"
#include "kachakacha/base/TestHarness.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using kachakacha::v2::app::HoverEditOutcome;
using kachakacha::v2::app::HoverEditPick;
using kachakacha::v2::app::PlanExtend;
using kachakacha::v2::app::PlanSplit;
using kachakacha::v2::app::PlanTrim;
using kachakacha::v2::app::WireCurvesOf;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::SegmentId;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::SnapCurve;
using kachakacha::v2::modeling::SnapScene;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] EntityId Ent(std::uint8_t number)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = number;
    return EntityId(kachakacha::v2::base::Uuid(bytes));
}

[[nodiscard]] SegmentId Seg(std::uint8_t number)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[14] = number;
    return SegmentId(kachakacha::v2::base::Uuid(bytes));
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

//! ワイヤーA(y=0, x=-50..50, entity1/seg1)と、それを挟む縦線B(x=-10, entity2/seg2)・
//! C(x=10, entity3/seg3)。
[[nodiscard]] SnapScene ThreeLineScene()
{
    SnapScene scene;
    scene.curves.push_back(SnapCurve{Ent(1), Seg(1), Line({-50, 0, 0}, {50, 0, 0}), false});
    scene.curves.push_back(SnapCurve{Ent(2), Seg(2), Line({-10, -20, 0}, {-10, 20, 0}), false});
    scene.curves.push_back(SnapCurve{Ent(3), Seg(3), Line({10, -20, 0}, {10, 20, 0}), false});
    return scene;
}

//! ワイヤーAだけの場面(交わる線が無い)。
[[nodiscard]] SnapScene LoneLineScene()
{
    SnapScene scene;
    scene.curves.push_back(SnapCurve{Ent(1), Seg(1), Line({-50, 0, 0}, {50, 0, 0}), false});
    return scene;
}

//! 閉じた矩形(0,0)-(40,0)-(40,20)-(0,20)、entity5/seg51..54。
//! それを x=20 で縦断する線(entity6/seg6)。
[[nodiscard]] SnapScene RectangleScene()
{
    SnapScene scene;
    scene.curves.push_back(SnapCurve{Ent(5), Seg(51), Line({0, 0, 0}, {40, 0, 0}), false});
    scene.curves.push_back(SnapCurve{Ent(5), Seg(52), Line({40, 0, 0}, {40, 20, 0}), false});
    scene.curves.push_back(SnapCurve{Ent(5), Seg(53), Line({40, 20, 0}, {0, 20, 0}), false});
    scene.curves.push_back(SnapCurve{Ent(5), Seg(54), Line({0, 20, 0}, {0, 0, 0}), false});
    scene.curves.push_back(SnapCurve{Ent(6), Seg(6), Line({20, -5, 0}, {20, 25, 0}), false});
    return scene;
}

//! 円(半径10、entity7/seg7)と、それを横切る水平線(entity8/seg8)。
[[nodiscard]] SnapScene CircleScene()
{
    SnapScene scene;
    const auto circle = CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 10.0);
    Require(circle.HasValue(), "円が作れる");
    scene.curves.push_back(SnapCurve{Ent(7), Seg(7), circle.Value(), false});
    scene.curves.push_back(SnapCurve{Ent(8), Seg(8), Line({-20, 0, 0}, {20, 0, 0}), false});
    return scene;
}

//! 線(0,0)-(10,0)(entity9/seg9)と、その先の縦の境目(x=30、entity10/seg10)。
[[nodiscard]] SnapScene ExtendScene()
{
    SnapScene scene;
    scene.curves.push_back(SnapCurve{Ent(9), Seg(9), Line({0, 0, 0}, {10, 0, 0}), false});
    scene.curves.push_back(SnapCurve{Ent(10), Seg(10), Line({30, -50, 0}, {30, 50, 0}), false});
    return scene;
}

[[nodiscard]] bool StartsWith(const std::string& text, const std::string& prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

} // namespace

// ---- WireCurvesOf ----

KACHA_V2_TEST(hover_edit_plan, ワイヤーの線分を番号順に集めて閉じているか判定する)
{
    const auto wire = WireCurvesOf(RectangleScene(), Ent(5), Tolerance());
    Require(wire.has_value(), "矩形のワイヤーが見つかる");
    Require(wire->closedLoop, "矩形は閉じている");
    Require(wire->segments.size() == 4, "4辺");
    RequireNear(wire->segments[0].StartPoint().x, 0.0, 1.0e-9, "1辺目の始点");
    RequireNear(wire->segments[3].EndPoint().x, 0.0, 1.0e-9, "4辺目の終点は始点へ戻る");

    Require(!WireCurvesOf(RectangleScene(), Ent(99), Tolerance()).has_value(),
        "無い物体は値を持たない");
}

// ---- PlanTrim: 開いた線 ----

KACHA_V2_TEST(hover_edit_plan, 開いた線の真ん中をトリムすると二本に分かれる)
{
    HoverEditPick pick;
    pick.entityId = Ent(1);
    pick.segmentId = Seg(1);
    pick.parameter = 0.5;
    pick.point = Vector3{0, 0, 0};
    const auto outcome = PlanTrim(ThreeLineScene(), pick, Tolerance());
    Require(outcome.HasValue(), "トリムできる: " + outcome.FirstSummaryJa());
    Require(outcome.Value().chains.size() == 2, "2本になる");
    Require(!outcome.Value().previewLine.empty(), "下見の点列がある");
    Require(outcome.Value().summaryJa.find("消えます") != std::string::npos,
        "一言に「消えます」を含む");
    Require(StartsWith(outcome.Value().footerJa, "トリム:"), "フッターは「トリム:」で始まる");
}

KACHA_V2_TEST(hover_edit_plan, 端に近い側をトリムすると一本だけ残る)
{
    HoverEditPick pick;
    pick.entityId = Ent(1);
    pick.segmentId = Seg(1);
    pick.parameter = 0.1;
    pick.point = Vector3{-40, 0, 0};
    const auto outcome = PlanTrim(ThreeLineScene(), pick, Tolerance());
    Require(outcome.HasValue(), "トリムできる: " + outcome.FirstSummaryJa());
    Require(outcome.Value().chains.size() == 1, "1本だけ残る");
    RequireNear(outcome.Value().chains.front().front().StartPoint().x, -10.0, 1.0e-6,
        "残った線はx=-10から始まる");
}

// ---- PlanTrim: 閉じた折れ線 ----

KACHA_V2_TEST(hover_edit_plan, 閉じた矩形の一辺をトリムすると開いた一本の鎖になる)
{
    HoverEditPick pick;
    pick.entityId = Ent(5);
    pick.segmentId = Seg(51); // 下辺
    pick.parameter = 0.25;    // x=10
    pick.point = Vector3{10, 0, 0};
    const auto outcome = PlanTrim(RectangleScene(), pick, Tolerance());
    Require(outcome.HasValue(), "トリムできる: " + outcome.FirstSummaryJa());
    Require(outcome.Value().chains.size() == 1, "輪が開いて1本になる");
    Require(outcome.Value().chains.front().size() == 4, "4本の線分をつないだ鎖");
    const auto& chain = outcome.Value().chains.front();
    RequireNear(chain.front().StartPoint().x, 20.0, 1.0e-6, "鎖の始点はx=20");
    RequireNear(chain.front().StartPoint().y, 0.0, 1.0e-6, "鎖の始点はy=0");
    RequireNear(chain.back().EndPoint().x, 0.0, 1.0e-6, "鎖の終点はx=0");
    RequireNear(chain.back().EndPoint().y, 0.0, 1.0e-6, "鎖の終点はy=0");
}

// ---- PlanTrim: 円 ----

KACHA_V2_TEST(hover_edit_plan, 円をトリムすると円弧一本になる)
{
    HoverEditPick pick;
    pick.entityId = Ent(7);
    pick.segmentId = Seg(7);
    pick.parameter = 0.75; // 下(270°)
    pick.point = Vector3{0, -10, 0};
    const auto outcome = PlanTrim(CircleScene(), pick, Tolerance());
    Require(outcome.HasValue(), "トリムできる: " + outcome.FirstSummaryJa());
    Require(outcome.Value().chains.size() == 1, "1本になる");
    Require(outcome.Value().chains.front().size() == 1, "円弧1本だけの鎖");
    Require(outcome.Value().chains.front().front().Kind()
            == kachakacha::v2::geometry::CurveKind::CircularArc,
        "残りは円弧");
    Require(outcome.Value().summaryJa.find("円弧") != std::string::npos,
        "一言に「円弧」を含む");
}

// ---- PlanExtend ----

KACHA_V2_TEST(hover_edit_plan, 終点側の端は先の境目まで延びる)
{
    HoverEditPick pick;
    pick.entityId = Ent(9);
    pick.segmentId = Seg(9);
    pick.parameter = 0.9;
    pick.point = Vector3{9, 0, 0};
    const auto outcome = PlanExtend(ExtendScene(), pick, Tolerance());
    Require(outcome.HasValue(), "延ばせる: " + outcome.FirstSummaryJa());
    RequireNear(outcome.Value().chains.front().front().EndPoint().x, 30.0, 1.0e-6,
        "境目まで延びる");
    Require(!outcome.Value().previewLine.empty(), "下見の点列がある");
    Require(StartsWith(outcome.Value().footerJa, "延長:"), "フッターは「延長:」で始まる");
}

KACHA_V2_TEST(hover_edit_plan, 境目が無い側の端は延ばせない)
{
    HoverEditPick pick;
    pick.entityId = Ent(9);
    pick.segmentId = Seg(9);
    pick.parameter = 0.1;
    pick.point = Vector3{1, 0, 0};
    const auto outcome = PlanExtend(ExtendScene(), pick, Tolerance());
    Require(!outcome.HasValue(), "始点側には境目が無いので断る: 断らなかった");
}

// ---- PlanSplit ----

KACHA_V2_TEST(hover_edit_plan, 押した位置に近い区切りで分ける)
{
    HoverEditPick pick;
    pick.entityId = Ent(1);
    pick.segmentId = Seg(1);
    pick.parameter = 0.45;
    pick.point = Vector3{-5, 0, 0};
    const auto outcome = PlanSplit(ThreeLineScene(), pick, Tolerance());
    Require(outcome.HasValue(), "分けられる: " + outcome.FirstSummaryJa());
    Require(outcome.Value().chains.size() == 2, "2本に分かれる");
    Require(outcome.Value().previewPoint.has_value(), "下見の点がある");
    RequireNear(outcome.Value().previewPoint->x, -10.0, 1.0e-6, "分かれる点はx=-10");
    RequireNear(outcome.Value().previewPoint->y, 0.0, 1.0e-6, "分かれる点はy=0");
}

KACHA_V2_TEST(hover_edit_plan, 交わる線が無いと分けられない)
{
    HoverEditPick pick;
    pick.entityId = Ent(1);
    pick.segmentId = Seg(1);
    pick.parameter = 0.5;
    pick.point = Vector3{0, 0, 0};
    const auto outcome = PlanSplit(LoneLineScene(), pick, Tolerance());
    Require(!outcome.HasValue(), "断る");
    RequireEqual(outcome.FirstCode(), std::string(kachakacha::v2::geometry::kSplitNoCut),
        "GEO-E024で断る");
}

KACHA_V2_TEST_MAIN("hover_edit_plan_tests")
