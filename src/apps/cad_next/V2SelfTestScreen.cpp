//! 画面の読みやすさのケース(オーナー指摘 2026-09-11)。
//!
//! ここで見るのは形ではなく **手がかり** である。どこに描いているのか、
//! いま選んでいるのはどれか、カーソルの下にあるのは何か、いま描けるのか。
//! どれも「できている」と言えるのに画面から読めないと、使えないのと同じになる。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include "kachakacha/view/ViewOrientation.h"

#include <QPointF>
#include <QString>

#include <cmath>
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
    // 作業平面を別の面(正面 XZ)へ移すと、その線は面の外になる。
    const auto planes = window.PlaneComboCount();
    for (int index = 0; index < planes; ++index) {
        if (window.PlaneComboText(index).contains(QStringLiteral("front"))) {
            window.SelectPlaneCombo(index);
            break;
        }
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
    };
}

} // namespace kachakacha::v2::selftest
