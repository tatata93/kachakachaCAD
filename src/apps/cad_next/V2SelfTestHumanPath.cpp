//! 人の道(Human-path)の試験(オーナー指示 2026-09-15 §16)。
//!
//! これまでの自己試験は、人が通る道を通っていなかった。
//!
//!   - 選択は `SetSelection(SelectAllOfKind(...))` で **文書から種類ごと直に拾う**
//!   - 面は `SelectionRef` を手で組んで `pickedFaceIndex = 0` と書く
//!   - 窓は `SetExtrudeChooser` などで答えを差し込む
//!   - 棚は `ExtrudeDock().TypeDistanceMm(...)` で **不可視のまま widget を直に叩く**
//!
//! Qt の widget は不可視でも値を持ちシグナルも出すので、これで全部通ってしまう。
//! 230 件通っていたのに、人には押し出しの棚が1枚も見えていなかった。
//!
//! **ここに置くケースは、次を守る。**
//!   1. 選ぶのは `SelectAt`(実際に拾う道)だけ。`SetSelection` は使わない
//!   2. 棚が **見えているか** を必ず確かめる
//!   3. 合図は `HandleToolKey`(窓が受ける道)を通す
//!
//! 守れないものは、ここではなく従来の試験に置く。

#include "V2SelfTest.h"

#include "V2ExtrudeDock.h"
#include "V2MainWindow.h"
#include "V2PartDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/CommandParameters.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/modeling/ToolController.h"

#include <QPointF>
#include <QString>

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::Shelf;
using kachakacha::v2::domain::EntityKind;

//! 上から見て矩形を1つ引く。**道具を持って画面を押す。**人と同じ道。
[[nodiscard]] bool DrawRectangleByHand(V2MainWindow& window)
{
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetViewCenter(kachakacha::v2::geometry::Vector3{});
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Rectangle);
    viewport.SetSnapSuppressed(true);
    viewport.ClickAt(QPointF(viewport.width() * 0.35, viewport.height() * 0.35));
    viewport.HoverAt(QPointF(viewport.width() * 0.65, viewport.height() * 0.65));
    viewport.ClickAt(QPointF(viewport.width() * 0.65, viewport.height() * 0.65));
    viewport.SetSnapSuppressed(false);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    return CountOfKind(window, EntityKind::Wire) >= 1;
}

//! 画面のどこを押せば、その線を拾えるか。**拾う道をそのまま使う。**
[[nodiscard]] bool ClickOnAnyCurve(V2MainWindow& window, Qt::KeyboardModifiers modifiers)
{
    auto& viewport = window.Viewport();
    for (const auto& curve : window.Session().Scene().curves) {
        const auto screen = viewport.Mapping().Project(curve.segment.Evaluate(0.5));
        if (!screen.has_value()) {
            continue;
        }
        viewport.SelectAt(QPointF(screen->x, screen->y), modifiers);
        if (!viewport.Selection().entityIds.empty()) {
            return true;
        }
    }
    return false;
}

//! HP-EX-01。実際に拾って押し出し、棚と下見が見えて、確定で立体になる。
[[nodiscard]] bool CaseHumanPathExtrudeProfileOnly(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))) {
        return false;
    }
    // **拾う道で選ぶ。**種類ごとに文書から拾ってこない。
    if (!Explain("引いた線を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.SetMode(kachakacha::v2::app::UiMode::Part);
    window.RunCommand("part.extrude");
    if (!Explain((std::string("矢印が出る(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            window.Viewport().ExtrudeHandleShown())) {
        return false;
    }
    // ここが本題。**棚が人に見えているか。**
    if (!Explain("押し出しの棚が見えている", window.ShelfShown(Shelf::Extrude))) {
        return false;
    }
    // 下見も出ていること。線だけでなく、押した先の輪郭が動いていること。
    const auto& loops = window.Viewport().ExtrudeHandlePreview();
    const auto outline = window.ExtrudeOutline();
    double movedMm = -1.0;
    if (loops.size() >= 2 && !outline.empty() && loops[1].size() == outline.size()) {
        movedMm = (loops[1].front() - outline.front()).Length();
    }
    if (!Explain((std::string("下見が距離のぶん進んでいる(")
                     + std::to_string(movedMm) + "mm)").c_str(),
            movedMm > 0.0
                && std::abs(movedMm - window.Viewport().ExtrudeHandleDistanceMm()) < 1.0e-6)) {
        return false;
    }
    // 押し始めの距離が、輪郭に対して見える大きさであること。
    // 0.5mm のままだと、100mm の輪郭では線の太さと区別できない。
    if (!Explain((std::string("押し始めの距離が見える大きさ(")
                     + std::to_string(window.Viewport().ExtrudeHandleDistanceMm())
                     + "mm)").c_str(),
            window.Viewport().ExtrudeHandleDistanceMm() >= 1.0)) {
        return false;
    }
    window.RunCommand("part.extrude");   // 確定
    if (!Explain((std::string("立体ができる(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            CountOfKind(window, EntityKind::Part) == 1)) {
        return false;
    }
    // 終わったら棚は元へ戻ること。出したままだと、いま何をしているのか読めない。
    return Explain("確定したら押し出しの棚は引っ込む",
        !window.ShelfShown(Shelf::Extrude));
}

//! HP-UI-01。押し出しの最中、棚は出続ける。下見を作り直しても消えない。
[[nodiscard]] bool CaseHumanPathExtrudePanelStaysVisible(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))) {
        return false;
    }
    if (!Explain("引いた線を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.SetMode(kachakacha::v2::app::UiMode::Part);
    window.RunCommand("part.extrude");
    if (!Explain("始めた時点で棚が見えている", window.ShelfShown(Shelf::Extrude))) {
        return false;
    }
    // 下見を作り直す。ここで棚が消えていた(RefreshRightShelves が隠していた)。
    window.UpdateExtrudePreview(window.Viewport().ExtrudeHandleDistanceMm() * 2.0);
    if (!Explain("下見を作り直しても棚は見えている", window.ShelfShown(Shelf::Extrude))) {
        return false;
    }
    // 選択が変わっても消えないこと。選択が変わるたびに棚を作り直している。
    window.RunCommand("select.all");
    if (!Explain("選択が変わっても棚は見えている", window.ShelfShown(Shelf::Extrude))) {
        return false;
    }
    // やめたら引っ込むこと。
    window.EndExtrudePreview();
    return Explain("やめたら棚は引っ込む", !window.ShelfShown(Shelf::Extrude));
}

//! HP-UI-03。右の欄へ打ったあとでも、Enter と Esc が効く。
//!
//! これまでは 3D 画面に焦点が無いと届かなかった。直すには 3D を一度
//! クリックするしかなく、**そのクリックで選択が変わる。**
[[nodiscard]] bool CaseHumanPathConfirmKeysAfterTyping(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))) {
        return false;
    }
    if (!Explain("引いた線を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.SetMode(kachakacha::v2::app::UiMode::Part);
    window.RunCommand("part.extrude");
    if (!Explain("矢印が出る", window.Viewport().ExtrudeHandleShown())) {
        return false;
    }
    if (!Explain("道具が合図を引き受けている", window.ToolWantsConfirmKeys())) {
        return false;
    }
    // 右の欄へ打つ。打ったのと同じ道(棚の欄)を通す。
    window.ExtrudeDock().TypeDistanceMm(12.0);
    if (!Explain((std::string("打った値が下見に入る(")
                     + std::to_string(window.Viewport().ExtrudeHandleDistanceMm())
                     + "mm)").c_str(),
            std::abs(window.Viewport().ExtrudeHandleDistanceMm() - 12.0) < 1.0e-6)) {
        return false;
    }
    // 欄に焦点があるつもりで Enter。値は変わっていないので確定になる。
    // **3D をクリックして焦点を戻す必要は無い。**
    const bool taken = window.HandleToolKey(Qt::Key_Return, nullptr);
    if (!Explain("欄に焦点があっても Enter が届く", taken)) {
        return false;
    }
    if (!Explain((std::string("Enter で立体ができる(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            CountOfKind(window, EntityKind::Part) == 1)) {
        return false;
    }
    // Esc も同じ道で効くこと。
    if (!Explain("引いた線をもう一度拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.RunCommand("part.extrude");
    if (!Explain("もう一度矢印が出る", window.Viewport().ExtrudeHandleShown())) {
        return false;
    }
    const bool cancelled = window.HandleToolKey(Qt::Key_Escape, nullptr);
    if (!Explain("Esc が届く", cancelled)) {
        return false;
    }
    if (!Explain("Esc で下見が消える", !window.Viewport().ExtrudeHandleShown())) {
        return false;
    }
    return Explain("Esc では立体が増えない", CountOfKind(window, EntityKind::Part) == 1);
}

} // namespace

std::vector<SelfTestCase> HumanPathCases()
{
    return {
        {"HP-EX-01 拾って押して確定すると立体になる", CaseHumanPathExtrudeProfileOnly},
        {"HP-UI-01 押し出しの最中は棚が見えている", CaseHumanPathExtrudePanelStaysVisible},
        {"HP-UI-03 右の欄へ打ったあとも Enter と Esc が効く",
            CaseHumanPathConfirmKeysAfterTyping},
    };
}

} // namespace kachakacha::v2::selftest
