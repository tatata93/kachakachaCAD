//! 画面の読みやすさのケース(オーナー指摘 2026-09-11)。
//!
//! ここで見るのは形ではなく **手がかり** である。どこに描いているのか、
//! いま選んでいるのはどれか、カーソルの下にあるのは何か、いま描けるのか。
//! どれも「できている」と言えるのに画面から読めないと、使えないのと同じになる。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
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
    };
}

} // namespace kachakacha::v2::selftest
