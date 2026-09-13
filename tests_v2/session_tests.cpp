// 作図の1本の流れ。スナップ → ツール → 文書コマンド。
// 画面が無くても、作図の筋道がそのまま確かめられること自体がここの成果。
#include "kachakacha/app/DrawingSession.h"
#include "kachakacha/base/TestHarness.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::app::DrawingSession;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::DocumentId;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::MakeOrthographicMapping;
using kachakacha::v2::geometry::ScreenMapping;
using kachakacha::v2::geometry::ScreenPoint;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::ArcMode;
using kachakacha::v2::modeling::DrawingTool;
using kachakacha::v2::modeling::SnapKind;
using kachakacha::v2::modeling::SnapScene;
using kachakacha::v2::modeling::SnapSettings;
using kachakacha::v2::modeling::ToolSettings;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

void RequireCount(std::size_t actual, std::size_t expected, const std::string& why)
{
    RequireEqual(std::to_string(actual), std::to_string(expected), why);
}

[[nodiscard]] ScreenMapping TopView()
{
    return MakeOrthographicMapping({50.0, 50.0, 0.0}, {0.0, 0.0, -1.0}, {0.0, 1.0, 0.0},
        100.0, 1000.0, 1000.0);
}

//! 作業平面を有効にした場面。
[[nodiscard]] SnapScene PlaneScene()
{
    SnapScene scene;
    scene.workPlane.active = true;
    scene.workPlane.origin = {0.0, 0.0, 0.0};
    scene.workPlane.normal = {0.0, 0.0, 1.0};
    return scene;
}

struct Fixture {
    DeterministicIdGenerator ids{31};
    DrawingSession session;

    Fixture() : session(DocumentId{}, ids)
    {
        session.SetMapping(TopView());
        session.SetScene(PlaneScene());
    }

    [[nodiscard]] ScreenPoint At(Vector3 world) const
    {
        const auto projected = TopView().Project(world);
        Require(projected.has_value(), "画面へ写せること");
        return *projected;
    }
};

} // namespace

KACHA_V2_TEST(session, 線を1本引くと文書が変わる)
{
    Fixture fixture;
    fixture.session.SelectTool(DrawingTool::Line);
    const auto first = fixture.session.Click(fixture.At({10.0, 10.0, 0.0}));
    Require(first.placedPoint, "1点目が置けること");
    Require(!first.committed, "1点では確定しないこと");

    const auto second = fixture.session.Click(fixture.At({60.0, 10.0, 0.0}));
    Require(second.committed, "2点目で確定すること");
    RequireCount(second.createdEntityIds.size(), 1, "作られたものの数");
    RequireCount(fixture.session.GetDocument().Snapshot().entities.size(), 1,
        "文書のオブジェクト数");
    RequireCount(fixture.session.GetDocument().Snapshot().features.size(), 1, "指示の数");
}

KACHA_V2_TEST(session, 引いたばかりの線の端点へ吸着できる)
{
    // V1はここが繋がっていなかった。作ったものが、すぐ次のスナップの相手になること。
    Fixture fixture;
    fixture.session.SelectTool(DrawingTool::Line);
    Require(fixture.session.Click(fixture.At({10.0, 10.0, 0.0})).placedPoint, "1点目");
    Require(fixture.session.Click(fixture.At({60.0, 10.0, 0.0})).committed, "1本目");

    // 端点から少しずれた位置を指す。
    const auto hover = fixture.session.Hover(fixture.At({60.2, 10.0, 0.0}));
    Require(hover.snap.has_value(), "吸着すること");
    Require(hover.snap->kind == SnapKind::Endpoint, "端点であること");
    RequireNear(hover.position->x, 60.0, 1e-9, "きっかり端点");
}

KACHA_V2_TEST(session, 続けて引くと2本目の始点が1本目の端点に一致する)
{
    Fixture fixture;
    fixture.session.SelectTool(DrawingTool::Line);
    Require(fixture.session.Click(fixture.At({10.0, 10.0, 0.0})).placedPoint, "1点目");
    Require(fixture.session.Click(fixture.At({60.0, 10.0, 0.0})).committed, "1本目");
    // 端点の近くをクリックすると、吸着して同じ位置になる。
    Require(fixture.session.Click(fixture.At({60.3, 10.2, 0.0})).placedPoint, "2本目の1点目");
    const auto second = fixture.session.Click(fixture.At({60.0, 60.0, 0.0}));
    Require(second.committed, "2本目が確定すること");

    const auto& features = fixture.session.GetDocument().Snapshot().features;
    RequireCount(features.size(), 2, "指示の数");
    const auto& wire = std::get<kachakacha::v2::domain::CreateWireDefinition>(
        features[1].definition);
    RequireNear(wire.segments.front().StartPoint().x, 60.0, 1e-9, "始点が端点に一致");
    RequireNear(wire.segments.front().StartPoint().y, 10.0, 1e-9, "始点が端点に一致");
}

KACHA_V2_TEST(session, 作図点を置ける)
{
    Fixture fixture;
    fixture.session.SelectTool(DrawingTool::Point);
    const auto result = fixture.session.Click(fixture.At({33.0, 44.0, 0.0}));
    Require(result.committed, "確定すること");
    const auto& entities = fixture.session.GetDocument().Snapshot().entities;
    RequireCount(entities.size(), 1, "オブジェクト数");
    Require(entities.front().kind == kachakacha::v2::domain::EntityKind::Point, "作図点");
    // 次のスナップの相手になっていること。
    const auto hover = fixture.session.Hover(fixture.At({33.1, 44.0, 0.0}));
    Require(hover.snap.has_value() && hover.snap->kind == SnapKind::DrawingPoint,
        "作図点へ吸着すること");
}

KACHA_V2_TEST(session, ポリラインを右クリックで確定できる)
{
    Fixture fixture;
    fixture.session.SelectTool(DrawingTool::Polyline);
    for (const Vector3 point : {Vector3{10, 10, 0}, Vector3{40, 10, 0},
             Vector3{40, 40, 0}, Vector3{10, 40, 0}}) {
        Require(fixture.session.Click(fixture.At(point)).placedPoint, "点を置けること");
    }
    const auto finished = fixture.session.FinishTool();
    Require(finished.committed, "確定すること");
    const auto& features = fixture.session.GetDocument().Snapshot().features;
    const auto& wire = std::get<kachakacha::v2::domain::CreateWireDefinition>(
        features.front().definition);
    RequireCount(wire.segments.size(), 3, "線の数");
}

KACHA_V2_TEST(session, ツールを変えると途中の点が捨てられる)
{
    Fixture fixture;
    fixture.session.SelectTool(DrawingTool::Line);
    Require(fixture.session.Click(fixture.At({10.0, 10.0, 0.0})).placedPoint, "1点目");
    fixture.session.SelectTool(DrawingTool::Circle);
    // 直線の1点目が残っていたら、円が1クリックで確定してしまう。
    const auto click = fixture.session.Click(fixture.At({50.0, 50.0, 0.0}));
    Require(!click.committed, "1クリックでは確定しないこと");
}

KACHA_V2_TEST(session, Escで途中をやめられる)
{
    Fixture fixture;
    fixture.session.SelectTool(DrawingTool::Polyline);
    Require(fixture.session.Click(fixture.At({10.0, 10.0, 0.0})).placedPoint, "1点目");
    Require(fixture.session.Click(fixture.At({40.0, 10.0, 0.0})).placedPoint, "2点目");
    fixture.session.CancelTool();
    Require(!fixture.session.FinishTool().committed, "確定できないこと");
    RequireCount(fixture.session.GetDocument().Snapshot().entities.size(), 0,
        "文書が変わっていないこと");
}

KACHA_V2_TEST(session, 1点戻せる)
{
    Fixture fixture;
    fixture.session.SelectTool(DrawingTool::Polyline);
    Require(fixture.session.Click(fixture.At({10.0, 10.0, 0.0})).placedPoint, "1点目");
    Require(fixture.session.Click(fixture.At({40.0, 10.0, 0.0})).placedPoint, "2点目");
    Require(fixture.session.Click(fixture.At({40.0, 40.0, 0.0})).placedPoint, "3点目");
    Require(fixture.session.UndoLastPoint(), "戻せること");
    const auto finished = fixture.session.FinishTool();
    Require(finished.committed, "確定すること");
    const auto& wire = std::get<kachakacha::v2::domain::CreateWireDefinition>(
        fixture.session.GetDocument().Snapshot().features.front().definition);
    RequireCount(wire.segments.size(), 1, "戻した結果、線は1本");
}

KACHA_V2_TEST(session, 文書のUndoで線が消える)
{
    Fixture fixture;
    fixture.session.SelectTool(DrawingTool::Line);
    Require(fixture.session.Click(fixture.At({10.0, 10.0, 0.0})).placedPoint, "1点目");
    Require(fixture.session.Click(fixture.At({60.0, 10.0, 0.0})).committed, "確定");
    RequireCount(fixture.session.GetDocument().Snapshot().entities.size(), 1, "1本ある");
    Require(fixture.session.Undo(), "取り消せること");
    RequireCount(fixture.session.GetDocument().Snapshot().entities.size(), 0, "消えたこと");
    Require(fixture.session.Redo(), "やり直せること");
    RequireCount(fixture.session.GetDocument().Snapshot().entities.size(), 1, "戻ったこと");
}

KACHA_V2_TEST(session, 途中経過が見える)
{
    Fixture fixture;
    fixture.session.SelectTool(DrawingTool::Line);
    Require(fixture.session.Click(fixture.At({10.0, 10.0, 0.0})).placedPoint, "1点目");
    const auto hover = fixture.session.Hover(fixture.At({60.0, 30.0, 0.0}));
    RequireCount(hover.preview.size(), 1, "途中の線が見えること");
    RequireNear(hover.preview.front().EndPoint().y, 30.0, 1e-9, "ポインタまで");
}

KACHA_V2_TEST(session, 案内文にスナップの種類が出る)
{
    Fixture fixture;
    fixture.session.SelectTool(DrawingTool::Line);
    Require(fixture.session.Click(fixture.At({10.0, 10.0, 0.0})).placedPoint, "1点目");
    Require(fixture.session.Click(fixture.At({60.0, 10.0, 0.0})).committed, "1本目");
    const auto hover = fixture.session.Hover(fixture.At({60.1, 10.0, 0.0}));
    Require(hover.messageJa.find("端点") != std::string::npos,
        "案内に種類が出ること (" + hover.messageJa + ")");
}

KACHA_V2_TEST(session, 抑止中はスナップなしと出る)
{
    Fixture fixture;
    fixture.session.SelectTool(DrawingTool::Line);
    Require(fixture.session.Click(fixture.At({10.0, 10.0, 0.0})).placedPoint, "1点目");
    Require(fixture.session.Click(fixture.At({60.0, 10.0, 0.0})).committed, "1本目");
    SnapSettings settings;
    settings.suppressed = true;
    fixture.session.SetSnapSettings(settings);
    const auto hover = fixture.session.Hover(fixture.At({60.1, 10.0, 0.0}));
    Require(!hover.snap.has_value(), "吸着しないこと");
    Require(hover.messageJa.find("スナップなし") != std::string::npos,
        "そう出ること (" + hover.messageJa + ")");
    // 抑止中でも、平面上の点は取れる(そこへ線は引ける)。
    Require(hover.position.has_value(), "位置は取れること");
    RequireNear(hover.position->x, 60.1, 1e-6, "指した位置そのもの");
}

namespace {

//! 2つの作図点を 2mm(画面で 20px)離して置いた場面。どちらも吸着半径 12px に入る位置を指せる。
struct TwoPointFixture : Fixture {
    kachakacha::v2::base::EntityId left = ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
    kachakacha::v2::base::EntityId right = ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();

    TwoPointFixture()
    {
        SnapScene scene = PlaneScene();
        scene.points.push_back({left, {50.0, 50.0, 0.0}});
        scene.points.push_back({right, {52.0, 50.0, 0.0}});
        session.SetScene(scene);
        session.SelectTool(DrawingTool::Line);
    }

    //! x(mm)を指したときの吸着先。吸着しなければ Nil。
    [[nodiscard]] kachakacha::v2::base::EntityId HoverAt(double x)
    {
        const auto hover = session.Hover(At({x, 50.0, 0.0}));
        return hover.snap.has_value() ? hover.snap->entityId : kachakacha::v2::base::EntityId{};
    }

    //! 左の点を持ち越している状態にする。右の点が範囲の外になる位置から入る。
    void HoldLeft()
    {
        Require(HoverAt(49.5) == left, "左の点へ吸着する(左 5px、右 25px)");
        Require(HoverAt(51.1) == left, "右が少し近くても左を持ち越す(左 11px、右 9px)");
    }
};

} // namespace

KACHA_V2_TEST(session, 小さな揺れでは吸着先が入れ替わらない)
{
    // ui-ux-integrated-spec.md §6.1。2点の真ん中あたりで手が震えても、リングが行き来しない。
    TwoPointFixture fixture;
    Require(fixture.HoverAt(50.9) == fixture.left, "近い左へ吸着する(左 9px、右 11px)");
    Require(fixture.HoverAt(51.1) == fixture.left, "境目を越えても左のまま(左 11px、右 9px)");
    Require(fixture.HoverAt(50.9) == fixture.left, "戻っても左のまま");
    // 揺れの幅(4px)より明らかに近くなったら乗り換える。
    Require(fixture.HoverAt(51.5) == fixture.right, "右が明らかに近いと右へ(左 15px、右 5px)");
    Require(fixture.HoverAt(50.9) == fixture.right, "乗り換えたあとは右を持ち越す(左 9px、右 11px)");
}

KACHA_V2_TEST(session, ツールの切替と取消と設定変更で吸着の持ち越しを捨てる)
{
    TwoPointFixture fixture;
    fixture.HoldLeft();
    fixture.session.SelectTool(DrawingTool::Line);
    Require(fixture.HoverAt(51.1) == fixture.right, "ツールを選び直すと近い右へ吸着する");

    fixture.HoldLeft();
    fixture.session.CancelTool();
    Require(fixture.HoverAt(51.1) == fixture.right, "取り消すと近い右へ吸着する");

    fixture.HoldLeft();
    fixture.session.SetToolSettings(ToolSettings{});
    Require(fixture.HoverAt(51.1) == fixture.right, "設定を変えると近い右へ吸着する");
}

KACHA_V2_TEST(session, 場面を差し替えると吸着の持ち越しを捨てる)
{
    // 文書を開く・Undo/Redo・作業平面やグリッドの変更は、どれも SetScene を通る。
    // 場面が入れ替わっても持ち越しが残ると、もう同じ物ではない相手へ吸い付いたままになる。
    TwoPointFixture fixture;
    fixture.HoldLeft();
    SnapScene replaced = PlaneScene();
    replaced.points.push_back({fixture.left, {50.0, 50.0, 0.0}});
    replaced.points.push_back({fixture.right, {52.0, 50.0, 0.0}});
    fixture.session.SetScene(replaced);
    Require(fixture.HoverAt(51.1) == fixture.right,
        "場面を差し替えると、前の左ではなく近い右へ吸着する");
}

KACHA_V2_TEST(session, 場面の差し替え後は12pxの外の旧候補へ吸着しない)
{
    // 持ち越しがあると、範囲 12px に余裕 4px を足した 16px まで旧候補へ吸い付く。
    // 差し替えで捨てていれば、14px の位置ではもう何にも吸着しない。
    TwoPointFixture fixture;
    fixture.HoldLeft();
    Require(fixture.HoverAt(48.6) == fixture.left,
        "捨てる前は 14px でも持ち越した左へ吸着する(範囲 12px + 余裕 4px)");
    fixture.session.SetScene(PlaneScene());
    SnapScene replaced = PlaneScene();
    replaced.points.push_back({fixture.left, {50.0, 50.0, 0.0}});
    replaced.points.push_back({fixture.right, {52.0, 50.0, 0.0}});
    fixture.session.SetScene(replaced);
    Require(fixture.HoverAt(48.6).IsNil(),
        "差し替えたあとは 14px では吸着しない(旧候補が生き残っていない)");
    Require(fixture.HoverAt(48.9) == fixture.left,
        "11px なら普通に吸着する(範囲そのものは変わっていない)");
}

KACHA_V2_TEST(session, Sを離したあとは持ち越し無しから始まる)
{
    TwoPointFixture fixture;
    fixture.HoldLeft();
    SnapSettings held;
    held.suppressed = true;
    fixture.session.SetSnapSettings(held);
    Require(fixture.HoverAt(51.1).IsNil(), "S の間は吸着しない");
    fixture.session.SetSnapSettings(SnapSettings{});
    Require(fixture.HoverAt(51.1) == fixture.right, "離すと、前の左ではなく近い右へ吸着する");
}

KACHA_V2_TEST(session, 抑止を入れて切るまでHoverが無くても持ち越しは残らない)
{
    // S を押して離すまでポインタを動かさない場合、押したまま焦点が外れて解けた場合、
    // 磁石を切って戻した場合。どれも core へは SetSnapSettings だけが届く。
    TwoPointFixture fixture;
    fixture.HoldLeft();
    SnapSettings suppressed;
    suppressed.suppressed = true;
    fixture.session.SetSnapSettings(suppressed);
    fixture.session.SetSnapSettings(SnapSettings{});
    Require(fixture.HoverAt(51.1) == fixture.right,
        "Hover を挟まずに抑止を入れて切っても、前の左ではなく近い右へ吸着する");
}

KACHA_V2_TEST(session, 覗き見は吸着の持ち越しを変えない)
{
    // 案内文は画面の中央で様子を見る。ポインタと無関係な位置の吸着先を持ち越してはならない。
    TwoPointFixture fixture;
    fixture.HoldLeft();
    const auto peek = fixture.session.PeekHover(fixture.At({51.9, 50.0, 0.0}));
    Require(peek.snap.has_value() && peek.snap->entityId == fixture.right,
        "覗き見の答えは Hover と同じ規則で右になる(左 19px、右 1px)");
    Require(fixture.HoverAt(51.1) == fixture.left, "覗き見のあとも左を持ち越している");
}

KACHA_V2_TEST(session, 作業平面が無ければ点を置けないと言う)
{
    DeterministicIdGenerator ids{41};
    DrawingSession session(DocumentId{}, ids);
    session.SetMapping(TopView());
    // 作業平面を有効にしない。
    session.SelectTool(DrawingTool::Line);
    const auto result = session.Click({500.0, 500.0});
    Require(!result.placedPoint, "置けないこと");
    Require(!result.diagnostics.empty(), "理由を言うこと");
    RequireEqual(result.diagnostics.front().code, std::string("UI-S001"), "診断コード");
}

KACHA_V2_TEST(session, 補助線として引ける)
{
    Fixture fixture;
    ToolSettings settings;
    settings.construction = true;
    fixture.session.SetToolSettings(settings);
    fixture.session.SelectTool(DrawingTool::Line);
    Require(fixture.session.Click(fixture.At({10.0, 10.0, 0.0})).placedPoint, "1点目");
    Require(fixture.session.Click(fixture.At({60.0, 10.0, 0.0})).committed, "確定");
    Require(fixture.session.GetDocument().Snapshot().entities.front().construction,
        "補助線であること");
}

KACHA_V2_TEST(session, 円弧を3点で引ける)
{
    Fixture fixture;
    ToolSettings settings;
    settings.arcMode = ArcMode::ThreePoints;
    fixture.session.SetToolSettings(settings);
    fixture.session.SelectTool(DrawingTool::Arc);
    Require(fixture.session.Click(fixture.At({20.0, 20.0, 0.0})).placedPoint, "1点目");
    Require(fixture.session.Click(fixture.At({40.0, 40.0, 0.0})).placedPoint, "2点目");
    const auto third = fixture.session.Click(fixture.At({60.0, 20.0, 0.0}));
    Require(third.committed, "確定すること");
    const auto& wire = std::get<kachakacha::v2::domain::CreateWireDefinition>(
        fixture.session.GetDocument().Snapshot().features.front().definition);
    Require(wire.segments.front().Kind() == CurveKind::CircularArc, "円弧であること");
}

KACHA_V2_TEST(session, 作れない形はそう言って文書を変えない)
{
    Fixture fixture;
    fixture.session.SelectTool(DrawingTool::Rectangle);
    Require(fixture.session.Click(fixture.At({10.0, 10.0, 0.0})).placedPoint, "1点目");
    // 同じ行の点。つぶれた矩形になる。
    const auto second = fixture.session.Click(fixture.At({60.0, 10.0, 0.0}));
    Require(!second.committed, "確定しないこと");
    Require(!second.diagnostics.empty(), "理由を言うこと");
    RequireCount(fixture.session.GetDocument().Snapshot().entities.size(), 0,
        "文書が変わっていないこと");
}

KACHA_V2_TEST(session, 何度やっても同じ文書になる)
{
    const auto build = [] {
        DeterministicIdGenerator ids{31};
        DrawingSession session(DocumentId{}, ids);
        session.SetMapping(TopView());
        session.SetScene(PlaneScene());
        session.SelectTool(DrawingTool::Line);
        const auto at = [](Vector3 world) {
            return *TopView().Project(world);
        };
        (void)session.Click(at({10.0, 10.0, 0.0}));
        (void)session.Click(at({60.0, 10.0, 0.0}));
        (void)session.Click(at({60.0, 10.0, 0.0}));
        (void)session.Click(at({60.0, 60.0, 0.0}));
        return session.GetDocument().Snapshot();
    };
    const auto first = build();
    for (int repeat = 0; repeat < 3; ++repeat) {
        const auto again = build();
        RequireCount(again.entities.size(), first.entities.size(), "オブジェクト数");
        for (std::size_t index = 0; index < first.entities.size(); ++index) {
            RequireEqual(again.entities[index].id.ToString(),
                first.entities[index].id.ToString(), "IDまで同じ");
        }
    }
}

KACHA_V2_TEST(session, 数値で決めた点はそのまま置かれ吸着しない)
{
    // カーソル入力(長さ・角度)で決めた点は、寄せてはならない。
    // 1本目の端点のすぐ近くに 2本目の終点を数で置いても、端点へ吸着しない。
    Fixture fixture;
    fixture.session.SelectTool(DrawingTool::Line);
    Require(fixture.session.Click(fixture.At({10.0, 10.0, 0.0})).placedPoint, "1点目");
    Require(fixture.session.Click(fixture.At({60.0, 10.0, 0.0})).committed, "1本目");
    Require(fixture.session.Click(fixture.At({10.0, 30.0, 0.0})).placedPoint, "2本目の1点目");
    const auto placed = fixture.session.PlacePoint({60.3, 10.0, 0.0});
    Require(placed.committed, "数で置いた点で確定する");
    const auto& curves = fixture.session.Scene().curves;
    Require(!curves.empty(), "線がある");
    const auto& last = curves.back().segment;
    RequireNear(last.EndPoint().x, 60.3, 1e-9, "数のとおりに置かれ、60.0 へ寄らない");
    // 選択道具では形にならない。
    fixture.session.SelectTool(DrawingTool::Select);
    const auto refused = fixture.session.PlacePoint({0.0, 0.0, 0.0});
    Require(!refused.committed, "選択道具では文書が変わらない");
}

KACHA_V2_TEST(session, 指定した点を残すと線と点がひとまとまりで入る)
{
    Fixture fixture;
    ToolSettings settings;
    settings.keepPoints = true;
    fixture.session.SetToolSettings(settings);
    fixture.session.SelectTool(DrawingTool::Line);
    Require(fixture.session.Click(fixture.At({10.0, 10.0, 0.0})).placedPoint, "1点目");
    const auto done = fixture.session.Click(fixture.At({60.0, 10.0, 0.0}));
    Require(done.committed, "確定");
    RequireCount(done.createdEntityIds.size(), 3, "線1 + 点2");
    RequireCount(fixture.session.GetDocument().Snapshot().entities.size(), 3, "文書にも3つ");
    RequireCount(fixture.session.Scene().points.size(), 2, "点は吸着の相手になる");
    Require(fixture.session.Undo(), "戻せる");
    RequireCount(fixture.session.GetDocument().Snapshot().entities.size(), 0,
        "一度で線も点も消える");
}

KACHA_V2_TEST(session, 聞き手が先に消えても場面の知らせで落ちない)
{
    // UI-P1-007 R9 の指摘。画面は Session より先に消える。
    // 素の関数を預けると、窓を閉じたあとの SetScene で消えた this を呼ぶ。
    // 札(戻り値)を持っている間だけ呼ばれる形にしてある。
    Fixture fixture;
    int called = 0;
    {
        const auto token = fixture.session.OnSceneChanged([&called] { ++called; });
        fixture.session.SetScene(PlaneScene());
        RequireCount(static_cast<std::size_t>(called), 1, "札を持っている間は呼ばれる");
    }
    // 札を捨てた。ここから先は呼ばれない。呼ばれたら消えた相手を触っている。
    fixture.session.SetScene(PlaneScene());
    fixture.session.SetScene(PlaneScene());
    RequireCount(static_cast<std::size_t>(called), 1, "札を捨てたら呼ばれない");
}

KACHA_V2_TEST(session, 聞き手は何人でも登録できる)
{
    Fixture fixture;
    int first = 0;
    int second = 0;
    const auto tokenA = fixture.session.OnSceneChanged([&first] { ++first; });
    {
        const auto tokenB = fixture.session.OnSceneChanged([&second] { ++second; });
        fixture.session.SetScene(PlaneScene());
    }
    fixture.session.SetScene(PlaneScene());
    RequireCount(static_cast<std::size_t>(first), 2, "残っている相手は呼ばれ続ける");
    RequireCount(static_cast<std::size_t>(second), 1, "消えた相手はもう呼ばれない");
}

KACHA_V2_TEST_MAIN("session_tests")
