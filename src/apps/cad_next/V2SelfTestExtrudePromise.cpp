//! 押し出しの約束(Codex P1-EXTRUDE-R1 の指摘の回帰試験)。
//!
//! ここで見るのは「作れるか」ではなく **約束を守っているか** である。
//!   - 下見を出しただけでは文書が変わらない。やめれば始める前とまったく同じ。
//!   - 1回の操作は1回の取り消しで完全に戻る。
//!   - 棚で選んだ演算のまま作られる。表示と実行が食い違わない。

#include "V2SelfTest.h"

#include "V2ExtrudeDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/modeling/ExtrudeInput.h"
#include "kachakacha/modeling/ToolController.h"

#include <QPointF>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {


//! 閉じた矩形を1つ引いて選ぶ。押し出しの相手になる。
[[nodiscard]] bool DrawClosedRectangle(V2MainWindow& window)
{
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    // 見ている場所も戻す。戻さないと、同じ画面位置に引いた2つ目の矩形が
    // 別の場所に出て、足す・引くの相手と重ならなくなる。
    viewport.SetViewCenter(kachakacha::v2::geometry::Vector3{});
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Rectangle);
    viewport.ClickAt(QPointF(viewport.width() * 0.35, viewport.height() * 0.35));
    viewport.HoverAt(QPointF(viewport.width() * 0.65, viewport.height() * 0.65));
    viewport.ClickAt(QPointF(viewport.width() * 0.65, viewport.height() * 0.65));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire));
    return !viewport.Selection().entityIds.empty();
}

[[nodiscard]] int CountParts(V2MainWindow& window)
{
    return CountOfKind(window, kachakacha::v2::domain::EntityKind::Part);
}

//! R1 B2。面の押し引きを始めてやめても、文書がまったく変わらないこと。
[[nodiscard]] bool CaseFacePushPullCancelLeavesDocumentUntouched(V2MainWindow& window)
{
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    window.RunCommand("part.extrude");
    window.RunCommand("part.extrude");
    kachakacha::v2::base::EntityId part;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kachakacha::v2::domain::EntityKind::Part) {
            part = entity.id;
        }
    }
    if (!Explain("立体ができる", !part.IsNil())) {
        return false;
    }
    auto& viewport = window.Viewport();
    kachakacha::v2::app::SelectionSet faceSelection;
    kachakacha::v2::app::SelectionRef face;
    face.entityId = part;
    face.kind = kachakacha::v2::app::SelectionElementKind::Face;
    face.pickedFaceIndex = 0;
    faceSelection.ordered.push_back(face);
    faceSelection.entityIds.push_back(part);

    // やめ方は3通り。どれでも文書は始める前とまったく同じでなければならない。
    const char* const kWays[] = {"Esc", "棚のキャンセル", "道具替え"};
    for (int way = 0; way < 3; ++way) {
        viewport.SetSelection(faceSelection);
        const std::uint64_t revision = window.Session().GetDocument().Revision();
        const std::size_t entities =
            window.Session().GetDocument().Snapshot().entities.size();
        window.RunCommand("part.extrude");   // 下見を出す
        if (!Explain((std::string(kWays[way]) + ": 下見を出しただけでは文書が変わらない")
                         .c_str(),
                window.Session().GetDocument().Revision() == revision)) {
            return false;
        }
        if (way == 0) {
            viewport.CancelTool();
            window.RunCommand("view.fit_all");
            window.EndExtrudePreview();
        } else if (way == 1) {
            window.ExtrudeDock().PressCancel();
        } else {
            window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
            window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
        }
        if (!Explain((std::string(kWays[way]) + ": やめても文書が変わらない").c_str(),
                window.Session().GetDocument().Revision() == revision)) {
            return false;
        }
        if (!Explain((std::string(kWays[way]) + ": 物の数も変わらない").c_str(),
                window.Session().GetDocument().Snapshot().entities.size() == entities)) {
            return false;
        }
        if (!Explain((std::string(kWays[way]) + ": 下見も残らない").c_str(),
                !viewport.ExtrudeHandleShown())) {
            return false;
        }
    }
    return Explain("面の押し引きをやめても文書は不変", true);
}

//! R1 B3。1回の押し出しが1回の取り消しで完全に戻ること。
[[nodiscard]] bool CaseExtrudeIsOneUndoStep(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    const std::size_t before = window.Session().GetDocument().Snapshot().entities.size();
    window.RunCommand("part.extrude");
    window.RunCommand("part.extrude");
    if (!Explain("立体ができる", CountParts(window) == 1)) {
        return false;
    }
    window.RunCommand("edit.undo");
    if (!Explain((std::string("1回の取り消しで完全に戻る(") + std::to_string(before)
                     + " → "
                     + std::to_string(window.Session().GetDocument().Snapshot()
                               .entities.size())
                     + ")").c_str(),
            window.Session().GetDocument().Snapshot().entities.size() == before)) {
        return false;
    }
    if (!Explain("立体も消える", CountParts(window) == 0)) {
        return false;
    }
    window.RunCommand("edit.redo");
    return Explain("1回のやり直しで完全に戻る", CountParts(window) == 1);
}

//! R1 B4。棚で選んだ演算が、確定のときに既定へ戻されないこと。
[[nodiscard]] bool CaseChosenBooleanSurvivesConfirm(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("閉じた矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    window.RunCommand("part.extrude");
    window.RunCommand("part.extrude");
    kachakacha::v2::base::EntityId part;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kachakacha::v2::domain::EntityKind::Part) {
            part = entity.id;
        }
    }
    if (!Explain("立体ができる", !part.IsNil())) {
        return false;
    }
    // 立体 + 輪郭。読み取りの既定は「切削」。
    if (!Explain("2つ目の矩形を引ける", DrawClosedRectangle(window))) {
        return false;
    }
    auto& viewport = window.Viewport();
    auto both = kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire);
    both.entityIds.push_back(part);
    kachakacha::v2::app::SelectionRef solid;
    solid.entityId = part;
    both.ordered.push_back(solid);
    viewport.SetSelection(both);
    window.RunCommand("part.extrude");   // 下見と棚
    // 人が「足す」を選ぶ。
    window.ExtrudeDock().ChooseBoolean(
        kachakacha::v2::modeling::ExtrudeBooleanMode::AddToPart);
    if (!Explain("棚に「足す」が出る",
            window.ExtrudeDock().BooleanMode()
                == kachakacha::v2::modeling::ExtrudeBooleanMode::AddToPart)) {
        return false;
    }
    // 1回目より長く押す。同じ場所へ同じ大きさで短く押すと、
    // 足した形が元の立体の中へすっぽり入り、体積が変わらないので
    // 「重なっていません」(EXT-004)で断られる。それは正しい断り方である。
    window.ExtrudeDock().TypeDistanceMm(20.0);
    window.RunCommand("part.extrude");   // 確定
    // 保存された作り方が「足す」になっていること。表示だけ合っていて実行が違う、を防ぐ。
    bool found = false;
    for (const auto& feature : window.Session().GetDocument().Snapshot().features) {
        const auto* definition =
            std::get_if<kachakacha::v2::domain::ExtrudeDefinition>(&feature.definition);
        if (definition == nullptr) {
            continue;
        }
        if (definition->booleanMode
            == static_cast<int>(kachakacha::v2::modeling::ExtrudeBooleanMode::AddToPart)) {
            found = true;
        }
    }
    return Explain((std::string("選んだ演算のまま作られる(帯は ")
                       + window.StatusText().toStdString() + ")").c_str(),
        found);
}


} // namespace

std::vector<SelfTestCase> ExtrudePromiseCases()
{
    return {
        {"面の押し引きをやめても文書が変わらない",
            CaseFacePushPullCancelLeavesDocumentUntouched},
        {"押し出しは1回の取り消しで完全に戻る", CaseExtrudeIsOneUndoStep},
        {"棚で選んだ演算のまま作られる", CaseChosenBooleanSurvivesConfirm},
    };
}

} // namespace kachakacha::v2::selftest
