//! 画面の読みやすさのケース(オーナー指摘 2026-09-11)。
//!
//! ここで見るのは形ではなく **手がかり** である。どこに描いているのか、
//! いま選んでいるのはどれか、カーソルの下にあるのは何か、いま描けるのか。
//! どれも「できている」と言えるのに画面から読めないと、使えないのと同じになる。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2ArrayDialog.h"
#include "V2ParameterDock.h"
#include "V2PartDock.h"
#include "V2PatternDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include "kachakacha/view/ViewOrientation.h"

#include <QPointF>
#include <QString>

#include <cmath>
#include <optional>
#include <string>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;

//! 文書のなかで最初に見つかるその種類のもの。無ければ Nil。
[[nodiscard]] EntityId FirstOfKind(V2MainWindow& window, EntityKind kind)
{
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kind) {
            return entity.id;
        }
    }
    return EntityId{};
}

//! 画面に線を1本引く。0,0 から 40,0 まで。
void DrawOneLine(V2MainWindow& window)
{
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    auto& viewport = window.Viewport();
    const auto first = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{0.0, 0.0, 0.0});
    const auto second = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{40.0, 0.0, 0.0});
    if (!first.has_value() || !second.has_value()) {
        return;
    }
    viewport.ClickAt(QPointF(first->x, first->y));
    viewport.ClickAt(QPointF(second->x, second->y));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
}

//! 指定の2点へ線を1本引く。重なりを作るために場所を変えて呼ぶ。
[[nodiscard]] bool DrawLineBetween(V2MainWindow& window,
    const kachakacha::v2::geometry::Vector3& from,
    const kachakacha::v2::geometry::Vector3& to)
{
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    auto& viewport = window.Viewport();
    const auto first = viewport.Mapping().Project(from);
    const auto second = viewport.Mapping().Project(to);
    if (!first.has_value() || !second.has_value()) {
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        return false;
    }
    viewport.ClickAt(QPointF(first->x, first->y));
    viewport.ClickAt(QPointF(second->x, second->y));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    return true;
}

//! 交わる2本を引いて、その交点の画面位置を返す。
//! 横線 y=10、縦線 x=20。交点 (20,10) では2本が同じ画面の点に重なる。
[[nodiscard]] std::optional<QPointF> DrawCrossingLines(V2MainWindow& window)
{
    if (!DrawLineBetween(window, kachakacha::v2::geometry::Vector3{0.0, 10.0, 0.0},
            kachakacha::v2::geometry::Vector3{40.0, 10.0, 0.0})) {
        return std::nullopt;
    }
    if (!DrawLineBetween(window, kachakacha::v2::geometry::Vector3{20.0, -10.0, 0.0},
            kachakacha::v2::geometry::Vector3{20.0, 30.0, 0.0})) {
        return std::nullopt;
    }
    const auto cross = window.Viewport().Mapping().Project(
        kachakacha::v2::geometry::Vector3{20.0, 10.0, 0.0});
    if (!cross.has_value()) {
        return std::nullopt;
    }
    return QPointF(cross->x, cross->y);
}

[[nodiscard]] bool CaseTabCyclesOverlappingCandidates(V2MainWindow& window)
{
    // 重なった線は、いちばん近い1本しか選べなかった。交点の多い図面では、
    // 奥の線を選ぶために線を動かすしかなく、そこで手が止まる。
    const auto cross = DrawCrossingLines(window);
    if (!Explain("交わる2本を引ける", cross.has_value())) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    viewport.HoverAt(*cross);
    if (!Explain((std::string("交点では候補が2つ以上ある(実際は ")
                     + std::to_string(viewport.CandidateCount()) + ")").c_str(),
            viewport.CandidateCount() >= 2)) {
        return false;
    }
    if (!Explain("はじめは先頭の候補", viewport.CandidateIndex() == 0)) {
        return false;
    }
    const EntityId front = viewport.HoveredEntityId();
    if (!Explain("先頭の候補を出している", !front.IsNil())) {
        return false;
    }
    // Tab で次の候補へ送る。出すものが変わるだけで、選択は動かない。
    if (!Explain("Tabで送れる", viewport.CycleCandidate(false))) {
        return false;
    }
    if (!Explain((std::string("候補の番号が進む(実際は ")
                     + std::to_string(viewport.CandidateIndex()) + ")").c_str(),
            viewport.CandidateIndex() == 1)) {
        return false;
    }
    const EntityId next = viewport.HoveredEntityId();
    if (!Explain("別の候補になる", !next.IsNil() && next != front)) {
        return false;
    }
    if (!Explain((std::string("Tabだけでは選択が変わらない(実際は ")
                     + std::to_string(kachakacha::v2::app::SelectionItemCount(
                         viewport.Selection()))
                     + " 件)").c_str(),
            kachakacha::v2::app::SelectionItemCount(viewport.Selection()) == 0)) {
        return false;
    }
    // 押すと、いま出している候補が選ばれる。別のものが選ばれては送った意味が無い。
    viewport.SelectAt(*cross, Qt::NoModifier);
    if (!Explain("Tabの後のクリックは出している候補を選ぶ",
            kachakacha::v2::app::IsSelected(viewport.Selection(), next))) {
        return false;
    }
    // 候補の数だけ送れば、元の番号へ戻る(順送りで循環する)。
    const int before = viewport.CandidateIndex();
    for (int step = 0; step < viewport.CandidateCount(); ++step) {
        (void)viewport.CycleCandidate(false);
    }
    if (!Explain((std::string("一周すると元の番号へ戻る(") + std::to_string(before)
                     + " → " + std::to_string(viewport.CandidateIndex()) + ")").c_str(),
            viewport.CandidateIndex() == before)) {
        return false;
    }
    // 別の場所へ移ったら番号を捨てる。捨てないと、次に交点へ来たときに
    // いくつ目から始まるのかが読めない。
    viewport.HoverAt(QPointF(cross->x() + 200.0, cross->y() + 200.0));
    viewport.HoverAt(*cross);
    if (!Explain((std::string("別の場所へ移った後は先頭から(実際は ")
                     + std::to_string(viewport.CandidateIndex()) + ")").c_str(),
            viewport.CandidateIndex() == 0)) {
        return false;
    }
    return Explain("同じ場所の先頭候補は毎回同じ", viewport.HoveredEntityId() == front);
}

[[nodiscard]] bool CaseAltClickTakesTheDeeperCandidate(V2MainWindow& window)
{
    // Alt+クリックは「もう1つ奥」。Tab を押さずに、重なりの下を直接選ぶ。
    const auto cross = DrawCrossingLines(window);
    if (!Explain("交わる2本を引ける", cross.has_value())) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    viewport.HoverAt(*cross);
    const EntityId front = viewport.HoveredEntityId();
    if (!Explain("交点で候補が2つ以上ある", viewport.CandidateCount() >= 2)) {
        return false;
    }
    // 素のクリックは手前の候補。ここが変わると、いつもの選択が壊れる。
    viewport.SelectAt(*cross, Qt::NoModifier);
    if (!Explain("素のクリックは手前の候補",
            kachakacha::v2::app::IsSelected(viewport.Selection(), front))) {
        return false;
    }
    // Alt+クリックは次(奥)の候補。
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    viewport.HoverAt(*cross);
    viewport.SelectAt(*cross, Qt::AltModifier);
    if (!Explain((std::string("Alt+クリックで1件だけ選ばれる(実際は ")
                     + std::to_string(kachakacha::v2::app::SelectionItemCount(
                         viewport.Selection()))
                     + " 件)").c_str(),
            kachakacha::v2::app::SelectionItemCount(viewport.Selection()) == 1)) {
        return false;
    }
    if (!Explain("Alt+クリックは手前の候補を選ばない",
            !kachakacha::v2::app::IsSelected(viewport.Selection(), front))) {
        return false;
    }
    return Explain("選ばれたのは、いま出している奥の候補",
        kachakacha::v2::app::IsSelected(viewport.Selection(),
            viewport.HoveredEntityId()));
}

[[nodiscard]] bool CaseWorkPlanesAreDrawn(V2MainWindow& window)
{
    // 作業平面が画面に出ていないと「どこに描いているのか」が読めない。
    // V1 と同じく、文書にある作図面はすべて出す。
    const int drawn = window.Viewport().WorkPlaneViewCount();
    return Explain((std::string("原点の3面が画面に出る(実際は ") + std::to_string(drawn)
                       + ")").c_str(),
        drawn == 3);
}

[[nodiscard]] bool CaseTreePickReachesViewport(V2MainWindow& window)
{
    // 左メニューで選んだものが 3D 画面の選択になる(オーナー指摘)。
    DrawOneLine(window);
    const EntityId wire = FirstOfKind(window, EntityKind::Wire);
    if (!Explain("線が1本ある", !wire.IsNil())) {
        return false;
    }
    if (!Explain("一覧にその行がある", window.SelectTreeRowForEntity(wire))) {
        return false;
    }
    return Explain((std::string("3D 画面の選択になる(実際は ")
                       + std::to_string(window.Viewport().Selection().entityIds.size())
                       + " 件)").c_str(),
        kachakacha::v2::app::IsSelected(window.Viewport().Selection(), wire));
}

[[nodiscard]] bool CaseViewportPickReachesTree(V2MainWindow& window)
{
    // 逆も同じ。3D 画面で選んだものが左メニューで光る。
    DrawOneLine(window);
    const EntityId wire = FirstOfKind(window, EntityKind::Wire);
    if (!Explain("線が1本ある", !wire.IsNil())) {
        return false;
    }
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{{wire}});
    return Explain((std::string("一覧の1行が光る(実際は ")
                       + std::to_string(window.TreeSelectedRowCount()) + " 行)").c_str(),
        window.TreeSelectedRowCount() == 1);
}

[[nodiscard]] bool CaseCursorChangesWithTool(V2MainWindow& window)
{
    // 作図中と選択中でカーソルが変わらないと、いま描けるのかが手元で分からない。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    if (!Explain("選択中は十字ではない", !window.Viewport().DrawingCursorShown())) {
        return false;
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    if (!Explain("線を引くときは十字", window.Viewport().DrawingCursorShown())) {
        return false;
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    return Explain("選択へ戻すと十字も戻る", !window.Viewport().DrawingCursorShown());
}

[[nodiscard]] bool CaseHoverFindsTheWireUnderTheCursor(V2MainWindow& window)
{
    // カーソルの下の線が分かること。押してみるまで分からないのでは選びにくい。
    DrawOneLine(window);
    const EntityId wire = FirstOfKind(window, EntityKind::Wire);
    if (!Explain("線が1本ある", !wire.IsNil())) {
        return false;
    }
    auto& viewport = window.Viewport();
    const auto middle = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{20.0, 0.0, 0.0});
    if (!Explain("線の真ん中が画面に出る", middle.has_value())) {
        return false;
    }
    viewport.HoverAt(QPointF(middle->x, middle->y));
    if (!Explain("線の上ではその線を掴む", viewport.HoveredEntityId() == wire)) {
        return false;
    }
    // 線から離れたところでは何も掴まない。掴みっぱなしだと、光り続けて紛らわしい。
    viewport.HoverAt(QPointF(middle->x + 200.0, middle->y + 200.0));
    return Explain("線から離れると離す", viewport.HoveredEntityId().IsNil());
}

[[nodiscard]] bool CaseFacingSelectionBringsItIntoView(V2MainWindow& window)
{
    // 「選択に正対」は向きだけでなく、真ん中と大きさも合わせる(V1 と同じ)。
    // 向きだけ変えて画面の外に置いたままだと「きいていない」ようにしか見えない。
    const EntityId plane = FirstOfKind(window, EntityKind::WorkPlane);
    if (!Explain("作業平面がある", !plane.IsNil())) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetViewCenter(kachakacha::v2::geometry::Vector3{5000.0, 5000.0, 0.0});
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{{plane}});
    window.RunCommand("view.align_selection");
    const auto center = viewport.ViewCenter();
    return Explain((std::string("注視点が選んだ面へ戻る(実際は ")
                       + std::to_string(center.x) + ", " + std::to_string(center.y)
                       + ")").c_str(),
        std::abs(center.x) < 1.0 && std::abs(center.y) < 1.0);
}

[[nodiscard]] bool CaseRightShelfFollowsTheTool(V2MainWindow& window)
{
    // 右は「いま使っている道具の設定」だけを出す(オーナー指摘)。
    // 9枚積むと1枚あたりが潰れて、見出しだけが並ぶ画面になる。
    using kachakacha::v2::app::Shelf;
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    if (!Explain("線のときは作図の棚", window.ShelfShown(Shelf::Drawing))) {
        return false;
    }
    if (!Explain("そのとき編集の棚は出ていない", !window.ShelfShown(Shelf::Edit))) {
        return false;
    }
    if (!Explain("そのとき製作の棚も出ていない", !window.ShelfShown(Shelf::Fabrication))) {
        return false;
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    if (!Explain("選択に戻すと編集の棚", window.ShelfShown(Shelf::Edit))) {
        return false;
    }
    if (!Explain("作図の棚は引っ込む", !window.ShelfShown(Shelf::Drawing))) {
        return false;
    }
    // モードを変えると、選択道具の相手も変わる。
    window.SetMode(kachakacha::v2::app::UiMode::Fabrication);
    if (!Explain("製作モードの選択は製作の棚", window.ShelfShown(Shelf::Fabrication))) {
        return false;
    }
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    // 出ている棚は、いつでも 2 枚まで。
    int shown = 0;
    for (const Shelf shelf : kachakacha::v2::app::AllShelves()) {
        if (window.ShelfShown(shelf)) {
            ++shown;
        }
    }
    return Explain((std::string("右に出ている棚は2枚まで(実際は ") + std::to_string(shown)
                       + ")").c_str(),
        shown <= 2);
}

[[nodiscard]] bool CaseTreeFilterNarrowsTheList(V2MainWindow& window)
{
    // 一覧の絞り込み(V1 の「名前・種類で絞り込み」)。
    // 物が増えると数十行になり、目で探すのはすぐに無理になる。
    DrawOneLine(window);
    const int all = window.VisibleEntityRowCount();
    if (!Explain((std::string("はじめは全部見える(") + std::to_string(all) + " 行)").c_str(),
            all >= 4)) {
        return false;
    }
    // 種類で絞る。原点の3面だけが残る(軸は「軸」、線は「ワイヤー」)。
    window.SetEntityFilterText(QStringLiteral("作業平面"));
    const int planes = window.VisibleEntityRowCount();
    if (!Explain((std::string("作業平面だけ残る(") + std::to_string(planes) + " 行)").c_str(),
            planes == 3)) {
        return false;
    }
    // 当たらない語なら何も残らない。
    window.SetEntityFilterText(QStringLiteral("そんな名前は無い"));
    if (!Explain((std::string("当たらなければ空(")
                     + std::to_string(window.VisibleEntityRowCount()) + " 行)").c_str(),
            window.VisibleEntityRowCount() == 0)) {
        return false;
    }
    // 空に戻せば元どおり。戻らないと、絞り込んだまま迷子になる。
    window.SetEntityFilterText(QString());
    return Explain((std::string("空に戻すと全部戻る(")
                       + std::to_string(window.VisibleEntityRowCount()) + " 行)").c_str(),
        window.VisibleEntityRowCount() == all);
}

[[nodiscard]] bool CaseDrawingDoesNotGrabOffPlaneWires(V2MainWindow& window)
{
    // 3次元の空間に2次元の図面が何枚も浮いているのが、このCADの形である。
    // 画面では手前の面と奥の面が重なって見えるので、薄くするだけでは
    // 別の面の線を掴んでしまう。作図中は作業平面の上の線しか掴まない。
    DrawOneLine(window);
    const EntityId wire = FirstOfKind(window, EntityKind::Wire);
    if (!Explain("XY の上に線が1本ある", !wire.IsNil())) {
        return false;
    }
    auto& viewport = window.Viewport();
    const auto middle = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{20.0, 0.0, 0.0});
    if (!Explain("線の真ん中が画面に出る", middle.has_value())) {
        return false;
    }
    // 選択道具なら掴める。掴めないと、別の面のものを直せなくなる。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.HoverAt(QPointF(middle->x, middle->y));
    if (!Explain("選択道具なら掴める", viewport.HoveredEntityId() == wire)) {
        return false;
    }
    // 作業平面を側面 YZ へ移すと、その線は面の外になる。
    // 正面 XZ では外にならない ── X 軸に沿う線は y=0 なので、XZ の上にも載っている。
    const auto planes = window.PlaneComboCount();
    bool moved = false;
    for (int index = 0; index < planes; ++index) {
        if (window.PlaneComboText(index).contains(QStringLiteral("YZ"))) {
            window.SelectPlaneCombo(index);
            moved = true;
            break;
        }
    }
    if (!Explain("側面 YZ へ移せる", moved)) {
        return false;
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    viewport.HoverAt(QPointF(middle->x, middle->y));
    if (!Explain("作図中は別の面の線を掴まない", viewport.HoveredEntityId().IsNil())) {
        return false;
    }
    // 印を外せば掴める。要るときに切れないと、かえって使えない。
    auto display = viewport.DisplaySettingsNow();
    display.dimOffPlaneLines = false;
    window.ApplyDisplaySettings(display);
    viewport.HoverAt(QPointF(middle->x, middle->y));
    return Explain("「常に薄く」を外せば掴める", viewport.HoveredEntityId() == wire);
}

//! 閉じた矩形を1つ引く。押し出しの相手になる。
[[nodiscard]] bool DrawRectangleForExtrude(V2MainWindow& window)
{
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Rectangle);
    auto& viewport = window.Viewport();
    const auto first = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{0.0, 0.0, 0.0});
    const auto second = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{30.0, 20.0, 0.0});
    if (!first.has_value() || !second.has_value()) {
        return false;
    }
    viewport.ClickAt(QPointF(first->x, first->y));
    viewport.ClickAt(QPointF(second->x, second->y));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire));
    return !viewport.Selection().entityIds.empty();
}

[[nodiscard]] bool CaseExtrudedPartAppearsOnScreen(V2MainWindow& window)
{
    // V2 は押し出しても画面に何も出なかった。核の形を画面へ渡す道が無く、
    // 書き出しにしか使っていなかった。工程5から先が目で確かめられない。
    if (!Explain("矩形を引ける", DrawRectangleForExtrude(window))) {
        return false;
    }
    if (!Explain((std::string("押し出す前は画面に形が無い(")
                     + std::to_string(window.Viewport().ShapeViewCount()) + ")").c_str(),
            window.Viewport().ShapeViewCount() == 0)) {
        return false;
    }
    window.RunCommand("part.extrude");
    if (!Explain((std::string("押し出すと画面に形が出る(")
                     + std::to_string(window.Viewport().ShapeViewCount()) + " 個)").c_str(),
            window.Viewport().ShapeViewCount() == 1)) {
        return false;
    }
    // 名前が増えただけでは駄目。塗る三角形が本当にあること。
    return Explain((std::string("塗る三角形がある(")
                       + std::to_string(window.Viewport().ShapeTriangleCount())
                       + " 枚)").c_str(),
        window.Viewport().ShapeTriangleCount() >= 12);
}

[[nodiscard]] bool CaseShapeCanBePickedOnScreen(V2MainWindow& window)
{
    // 見えているのに掴めない、をなくす。塗った面の内側を押して選べること。
    if (!Explain("矩形を引ける", DrawRectangleForExtrude(window))) {
        return false;
    }
    window.RunCommand("part.extrude");
    if (!Explain("画面に形が出ている", window.Viewport().ShapeViewCount() == 1)) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    // 矩形の真ん中(線の上ではないところ)を押す。
    const auto middle = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{15.0, 10.0, 0.0});
    if (!Explain("真ん中が画面に出る", middle.has_value())) {
        return false;
    }
    viewport.SelectAt(QPointF(middle->x, middle->y), Qt::NoModifier);
    if (!Explain((std::string("押すと何か選ばれる(")
                     + std::to_string(viewport.Selection().entityIds.size())
                     + " 件)").c_str(),
            !viewport.Selection().entityIds.empty())) {
        return false;
    }
    // 「立体・面を出す」を外したら、塗りも当たり判定も消える。
    auto display = viewport.DisplaySettingsNow();
    display.shapesVisible = false;
    window.ApplyDisplaySettings(display);
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    viewport.SelectAt(QPointF(middle->x, middle->y), Qt::NoModifier);
    const bool empty = viewport.Selection().entityIds.empty();
    display.shapesVisible = true;
    window.ApplyDisplaySettings(display);
    return Explain("出さない設定なら掴めない", empty);
}

[[nodiscard]] bool CaseHiddenShapeLeavesTheScreen(V2MainWindow& window)
{
    // 「選択を隠す」で形も消えること。名前だけ消えて形が残ると、
    // 隠したつもりのものが画面に居座る。
    if (!Explain("矩形を引ける", DrawRectangleForExtrude(window))) {
        return false;
    }
    window.RunCommand("part.extrude");
    if (!Explain("画面に形が出ている", window.Viewport().ShapeViewCount() == 1)) {
        return false;
    }
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Part));
    window.RunCommand("view.hide_selected");
    if (!Explain((std::string("隠すと画面から消える(")
                     + std::to_string(window.Viewport().ShapeViewCount()) + ")").c_str(),
            window.Viewport().ShapeViewCount() == 0)) {
        return false;
    }
    window.RunCommand("view.show_all");
    return Explain("出し直すと戻る", window.Viewport().ShapeViewCount() == 1);
}

[[nodiscard]] bool CasePatternPreviewShowsPages(V2MainWindow& window)
{
    // 型紙は作れたが、画面で確かめる道が無かった。SVG や DXF に出して
    // 別の道具で開くまで、紙に収まっているのかも分からない。
    if (!Explain("はじめは空", window.PatternDock().PageCount() == 0)) {
        return false;
    }
    if (!Explain("ページを見ていない", window.PatternDock().CurrentPage() == -1)) {
        return false;
    }
    // 部材を作って型紙まで進める。
    if (!Explain("矩形を引ける", DrawRectangleForExtrude(window))) {
        return false;
    }
    window.RunCommand("part.extrude");
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Part));
    window.RunCommand("fabrication.create");
    window.RunCommand("fabrication.create_pattern");
    if (!Explain((std::string("型紙が下見に出る(")
                     + std::to_string(window.PatternDock().PageCount()) + " 枚)").c_str(),
            window.PatternDock().PageCount() >= 1)) {
        return false;
    }
    if (!Explain((std::string("1枚目を見ている(")
                     + std::to_string(window.PatternDock().CurrentPage()) + ")").c_str(),
            window.PatternDock().CurrentPage() == 0)) {
        return false;
    }
    // 何枚目か・紙の大きさ・線の数を言う。空の紙が出ていないかを見るため。
    const QString summary = window.PatternDock().SummaryText();
    if (!Explain((std::string("枚数と紙の大きさを言う(") + summary.toStdString()
                     + ")").c_str(),
            summary.contains(QStringLiteral("mm")) && summary.contains(QStringLiteral("線")))) {
        return false;
    }
    // 端で止まる。輪にすると何枚目にいるのか分からなくなる。
    window.PatternDock().ShowPage(-1);
    return Explain("前へは戻れない(1枚目)", window.PatternDock().CurrentPage() == 0);
}

[[nodiscard]] int CountWires(V2MainWindow& window)
{
    return CountOfKind(window, EntityKind::Wire);
}

[[nodiscard]] bool CaseLinearArrayPlacesCopies(V2MainWindow& window)
{
    // 窓を10個並べるのに、1つずつ複製して位置を打つのは間違いのもと。
    DrawOneLine(window);
    const int before = CountWires(window);
    if (!Explain("線が1本ある", before == 1)) {
        return false;
    }
    // 5個(元 + 写し4つ)、20mm おき。
    window.SetArrayChooser([](const V2ArrayChoice&, bool) {
        V2ArrayChoice choice;
        choice.count = 5;
        choice.step = kachakacha::v2::geometry::Vector3{0.0, 20.0, 0.0};
        choice.spanIsTotal = false;
        return choice;
    });
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::Wire));
    window.RunCommand("wire.array_linear");
    if (!Explain((std::string("5本になる(実際は ") + std::to_string(CountWires(window))
                     + ")").c_str(),
            CountWires(window) == 5)) {
        return false;
    }
    // まとめて1回で戻せる。10個並べたあとに10回押して戻すのでは手間が10倍になる。
    window.RunCommand("edit.undo");
    return Explain((std::string("1回で元に戻る(実際は ")
                       + std::to_string(CountWires(window)) + " 本)").c_str(),
        CountWires(window) == 1);
}

[[nodiscard]] bool CaseCircularArrayDoesNotDoubleTheFirst(V2MainWindow& window)
{
    // 一周に6個なら、6個目は元の上に重なる。重ねない。
    DrawOneLine(window);
    window.SetArrayChooser([](const V2ArrayChoice&, bool) {
        V2ArrayChoice choice;
        choice.count = 6;
        choice.totalAngleDeg = 360.0;
        choice.center = kachakacha::v2::geometry::Vector3{};
        return choice;
    });
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::Wire));
    window.RunCommand("wire.array_circular");
    return Explain((std::string("6本になる(実際は ") + std::to_string(CountWires(window))
                       + ")").c_str(),
        CountWires(window) == 6);
}

[[nodiscard]] bool CaseArrayRefusesBadCount(V2MainWindow& window)
{
    // 打ち間違いは、画面が固まる前に断る。
    DrawOneLine(window);
    window.SetArrayChooser([](const V2ArrayChoice&, bool) {
        V2ArrayChoice choice;
        choice.count = 1;   // 1個は並びでない
        choice.step = kachakacha::v2::geometry::Vector3{10.0, 0.0, 0.0};
        return choice;
    });
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::Wire));
    const int before = CountWires(window);
    window.RunCommand("wire.array_linear");
    if (!Explain("線は増えない", CountWires(window) == before)) {
        return false;
    }
    // やめたときは、何も変わらないことを言う。
    window.SetArrayChooser([](const V2ArrayChoice&, bool) {
        return std::optional<V2ArrayChoice>{};
    });
    window.RunCommand("wire.array_linear");
    return Explain((std::string("やめたと言う(") + window.StatusText().toStdString()
                       + ")").c_str(),
        window.StatusText().contains(QStringLiteral("やめました")));
}

[[nodiscard]] bool CasePartModeShowsToolSettings(V2MainWindow& window)
{
    // 部品モードだけ右が「役割の表」で、板厚も厚みの付け方も治具のすき間も
    // どこにも無かった(オーナー指摘 2026-09-11)。
    using kachakacha::v2::app::Shelf;
    window.SetMode(kachakacha::v2::app::UiMode::Part);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    if (!Explain("部品の棚が出る", window.ShelfShown(Shelf::Part))) {
        return false;
    }
    // 数の棚で板厚を変えると、部品の棚にも映る。別に持つと食い違う。
    (void)window.ParameterDock().Apply(
        kachakacha::v2::app::ParameterId::ExtrudeDistance, QStringLiteral("0.75"));
    const double shown = window.PartDock().ParameterMm(
        kachakacha::v2::app::ParameterId::ExtrudeDistance);
    if (!Explain((std::string("数の棚の値が映る(実際は ") + std::to_string(shown)
                     + ")").c_str(),
            std::abs(shown - 0.75) < 1.0e-6)) {
        return false;
    }
    // 何を選んでいるかを一言で出す。押す前に「足りない」と分かるようにする。
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.PartDock().SetSelectionText(QString());
    window.SetMode(kachakacha::v2::app::UiMode::Part);
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    return Explain((std::string("選択の覚え書きが出る(")
                       + window.PartDock().SelectionText().toStdString() + ")").c_str(),
        !window.PartDock().SelectionText().isEmpty());
}

[[nodiscard]] bool CaseTopBarStaysShort(V2MainWindow& window)
{
    // 部品モードの2段目に 19 個が横一列に並び、何から押すのか読めなかった。
    // 入口だけに絞り、残りは右の棚へ移した。
    window.SetMode(kachakacha::v2::app::UiMode::Part);
    const int shown = window.VisibleToolCount();
    if (!Explain((std::string("2段目は10個まで(実際は ") + std::to_string(shown)
                     + ")").c_str(),
            shown <= 10)) {
        return false;
    }
    // 減らしたぶんは消えていない。表を動かすものは右の棚のボタンから押せる。
    QString reason;
    (void)window.CommandEnabled("guide.row_up", &reason);
    return Explain("表を動かす命令は残っている",
        window.ActionFor("guide.row_up") != nullptr);
}

[[nodiscard]] bool CaseModeChangeReturnsToSelect(V2MainWindow& window)
{
    // 起動が「直線」で、モードを変えても道具が残っていたので、
    // 画面を押すと線が引けてしまい、選ぶことができなかった。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    window.SetMode(kachakacha::v2::app::UiMode::Part);
    if (!Explain("モードを変えると選択道具へ戻る",
            window.Session().CurrentTool()
                == kachakacha::v2::modeling::DrawingTool::Select)) {
        return false;
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Circle);
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    return Explain("作図モードへ戻しても選択道具",
        window.Session().CurrentTool() == kachakacha::v2::modeling::DrawingTool::Select);
}

[[nodiscard]] bool CaseMoveToolPicksItsTargetFirst(V2MainWindow& window)
{
    // 移動の道具で2点を押すと「先に動かす線を選んでください」と断られていた。
    // **押した後に言われる。** 1回目の押しで相手を選ぶ。
    DrawOneLine(window);
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Move);
    const auto middle = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{20.0, 0.0, 0.0});
    if (!Explain("線の真ん中が画面に出る", middle.has_value())) {
        return false;
    }
    viewport.ClickAt(QPointF(middle->x, middle->y));
    if (!Explain((std::string("1回目の押しで相手が選ばれる(")
                     + std::to_string(viewport.Selection().entityIds.size()) + " 件)").c_str(),
            viewport.Selection().entityIds.size() == 1)) {
        return false;
    }
    return Explain((std::string("次に何をするか言う(")
                       + window.StatusText().toStdString() + ")").c_str(),
        window.StatusText().contains(QStringLiteral("動かす元の点")));
}

[[nodiscard]] bool CasePointsCanBeSelected(V2MainWindow& window)
{
    // 点は線と同じ文書のものなのに、拾う道が無かった。
    // 「交点に点」で作った点も作図点も、**選ぶことができなかった**。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Point);
    auto& viewport = window.Viewport();
    const auto where = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{12.0, 8.0, 0.0});
    if (!Explain("置く場所が画面に出る", where.has_value())) {
        return false;
    }
    viewport.ClickAt(QPointF(where->x, where->y));
    const EntityId point = FirstOfKind(window, EntityKind::Point);
    if (!Explain("作図点ができる", !point.IsNil())) {
        return false;
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    viewport.SelectAt(QPointF(where->x, where->y), Qt::NoModifier);
    return Explain((std::string("その点を押すと選べる(")
                       + std::to_string(viewport.Selection().entityIds.size())
                       + " 件)").c_str(),
        kachakacha::v2::app::IsSelected(viewport.Selection(), point));
}

[[nodiscard]] bool CaseCtrlClickTogglesSelection(V2MainWindow& window)
{
    // 複数選べないと、2本要る操作(トリム・結合・面取り)が全部使えない。
    DrawOneLine(window);
    const EntityId first = FirstOfKind(window, EntityKind::Wire);
    // 2本目を別のところへ引く。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    auto& viewport = window.Viewport();
    const auto a = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{0.0, 20.0, 0.0});
    const auto b = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{40.0, 20.0, 0.0});
    if (!Explain("2本目の場所が画面に出る", a.has_value() && b.has_value())) {
        return false;
    }
    viewport.ClickAt(QPointF(a->x, a->y));
    viewport.ClickAt(QPointF(b->x, b->y));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    const auto firstMiddle = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{20.0, 0.0, 0.0});
    const auto secondMiddle = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{20.0, 20.0, 0.0});
    viewport.SelectAt(QPointF(firstMiddle->x, firstMiddle->y), Qt::NoModifier);
    viewport.SelectAt(QPointF(secondMiddle->x, secondMiddle->y), Qt::ControlModifier);
    if (!Explain((std::string("Ctrl で2本選べる(")
                     + std::to_string(kachakacha::v2::app::SelectionItemCount(
                         viewport.Selection()))
                     + " 件)").c_str(),
            kachakacha::v2::app::SelectionItemCount(viewport.Selection()) == 2)) {
        return false;
    }
    // 同じものをもう一度 Ctrl で押せば外れる。
    viewport.SelectAt(QPointF(secondMiddle->x, secondMiddle->y), Qt::ControlModifier);
    (void)first;
    return Explain((std::string("Ctrl で外せる(")
                       + std::to_string(kachakacha::v2::app::SelectionItemCount(
                           viewport.Selection()))
                       + " 件)").c_str(),
        kachakacha::v2::app::SelectionItemCount(viewport.Selection()) == 1);
}

[[nodiscard]] bool CaseCommandWaitsForItsTargets(V2MainWindow& window)
{
    // トリムを押すと、その場で「線を2本選んでください」と言って **終わって** いた。
    // 選んでから押し直さなければならず、押す順を覚えていないと使えない。
    DrawOneLine(window);
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    window.RunCommand("wire.offset");
    if (!Explain((std::string("構えて待つ(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("選ぶと続きます")))) {
        return false;
    }
    if (!Explain((std::string("何を構えているか言える(")
                     + window.PendingCommandLabel().toStdString() + ")").c_str(),
            !window.PendingCommandLabel().isEmpty())) {
        return false;
    }
    // 「1つ以上」の条件は、そろっても勝手に走らない(2つ目を選ぶ前に終わってしまう)。
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::Wire));
    if (!Explain((std::string("そろっても待つ(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("Enter")))) {
        return false;
    }
    // Esc でやめられる。
    window.ClearPendingCommand();
    return Explain("やめれば構えが消える", window.PendingCommandLabel().isEmpty());
}

} // namespace

std::vector<SelfTestCase> ScreenCases()
{
    return {
        {"作図面が画面に出て、どこに描いているかが読める", &CaseWorkPlanesAreDrawn},
        {"左メニューで選んだものが3D画面の選択になる", &CaseTreePickReachesViewport},
        {"3D画面で選んだものが左メニューで光る", &CaseViewportPickReachesTree},
        {"道具を変えるとカーソルの形も変わる", &CaseCursorChangesWithTool},
        {"カーソルの下の線が分かる", &CaseHoverFindsTheWireUnderTheCursor},
        {"選択に正対すると、その面が画面の真ん中に来る", &CaseFacingSelectionBringsItIntoView},
        {"右の棚がいま使っている道具に付いてくる", &CaseRightShelfFollowsTheTool},
        {"一覧を名前・種類で絞り込める", &CaseTreeFilterNarrowsTheList},
        {"作図中は作業平面の外の線を掴まない", &CaseDrawingDoesNotGrabOffPlaneWires},
        {"押し出した部品が3D画面に出る", &CaseExtrudedPartAppearsOnScreen},
        {"塗った形を画面で掴める", &CaseShapeCanBePickedOnScreen},
        {"隠した形は画面からも消える", &CaseHiddenShapeLeavesTheScreen},
        {"型紙を出す前に画面で見られる", &CasePatternPreviewShowsPages},
        {"直線に並べて一度で戻せる", &CaseLinearArrayPlacesCopies},
        {"一周に並べても最後が元に重ならない", &CaseCircularArrayDoesNotDoubleTheFirst},
        {"並べる数の打ち間違いを断る", &CaseArrayRefusesBadCount},
        {"部品モードでも道具の設定が右に出る", &CasePartModeShowsToolSettings},
        {"上の帯は入口だけに絞られている", &CaseTopBarStaysShort},
        {"モードを変えると選択道具へ戻る", &CaseModeChangeReturnsToSelect},
        {"動かす道具は1回目の押しで相手を選ぶ", &CaseMoveToolPicksItsTargetFirst},
        {"作図点を押して選べる", &CasePointsCanBeSelected},
        {"Ctrlで追加と解除ができる", &CaseCtrlClickTogglesSelection},
        {"重なった候補をTabで送れる", &CaseTabCyclesOverlappingCandidates},
        {"Alt+クリックで奥の候補を選べる", &CaseAltClickTakesTheDeeperCandidate},
        {"命令は相手がそろうまで構えて待つ", &CaseCommandWaitsForItsTargets},
    };
}

} // namespace kachakacha::v2::selftest
