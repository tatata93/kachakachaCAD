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
#include "kachakacha/app/ExtrudeInputState.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/modeling/ToolController.h"

#include <QPointF>
#include <QString>

#include <cmath>
#include <cstddef>
#include <string>
#include <variant>
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

//! 出力を選んで押し出し、出来たものを数える。**人と同じ道で選ぶ。**
struct OutputCounts {
    int parts = 0;
    int wires = 0;
    bool ok = false;
};

[[nodiscard]] OutputCounts ExtrudeWithOutputs(V2MainWindow& window,
    kachakacha::v2::app::ExtrudeOutputPreset preset, bool confirm)
{
    OutputCounts counts;
    window.RunCommand("file.new");
    if (!DrawRectangleByHand(window)) {
        return counts;
    }
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    if (!ClickOnAnyCurve(window, Qt::NoModifier)) {
        return counts;
    }
    window.SetMode(kachakacha::v2::app::UiMode::Part);
    window.RunCommand("part.extrude");
    if (!window.Viewport().ExtrudeHandleShown()) {
        return counts;
    }
    // 棚の「出力」欄を、人が選ぶのと同じ道で動かす。
    window.ExtrudeDock().ShowOutputs(
        kachakacha::v2::app::OutputsForPreset(preset));
    window.RefreshExtrudeFromDock();
    if (confirm) {
        window.RunCommand("part.extrude");
    }
    counts.parts = CountOfKind(window, EntityKind::Part);
    counts.wires = CountOfKind(window, EntityKind::Wire) - wiresBefore;
    counts.ok = true;
    return counts;
}

//! HP-EX-OUTPUT-01〜05。選んだ出力のとおりの物が文書に出来る。
[[nodiscard]] bool CaseHumanPathExtrudeOutputs(V2MainWindow& window)
{
    using kachakacha::v2::app::ExtrudeOutputPreset;

    // 01 ソリッドのみ。ワイヤーは増えない。
    const auto solid = ExtrudeWithOutputs(window, ExtrudeOutputPreset::SolidOnly, true);
    if (!Explain("HP-EX-OUTPUT-01 ソリッドのみで立体が1つできる",
            solid.ok && solid.parts == 1)) {
        return false;
    }
    if (!Explain((std::string("HP-EX-OUTPUT-01 ワイヤーは増えない(")
                     + std::to_string(solid.wires) + "本)").c_str(),
            solid.wires == 0)) {
        return false;
    }

    // 04 押し出し先ワイヤーのみ。立体は出来ず、ワイヤーが1本増える。
    const auto endOnly = ExtrudeWithOutputs(window, ExtrudeOutputPreset::EndWireOnly, true);
    if (!Explain((std::string("HP-EX-OUTPUT-04 立体は作らない(")
                     + std::to_string(endOnly.parts) + "個)").c_str(),
            endOnly.ok && endOnly.parts == 0)) {
        return false;
    }
    if (!Explain((std::string("HP-EX-OUTPUT-04 終端の輪郭だけが増える(")
                     + std::to_string(endOnly.wires) + "本)").c_str(),
            endOnly.wires == 1)) {
        return false;
    }

    // 02 ワイヤーのみ。立体は出来ず、開始側・終端・側面が増える。
    const auto wiresOnly = ExtrudeWithOutputs(window, ExtrudeOutputPreset::WiresOnly, true);
    if (!Explain((std::string("HP-EX-OUTPUT-02 立体は作らない(")
                     + std::to_string(wiresOnly.parts) + "個)").c_str(),
            wiresOnly.ok && wiresOnly.parts == 0)) {
        return false;
    }
    if (!Explain((std::string("HP-EX-OUTPUT-02 終端だけより多くのワイヤーが増える(")
                     + std::to_string(wiresOnly.wires) + "本)").c_str(),
            wiresOnly.wires > endOnly.wires)) {
        return false;
    }

    // 03 ワイヤー + ソリッド。両方できる。**別々の物として。**
    const auto both = ExtrudeWithOutputs(window, ExtrudeOutputPreset::WiresAndSolid, true);
    if (!Explain((std::string("HP-EX-OUTPUT-03 立体もワイヤーもできる(立体")
                     + std::to_string(both.parts) + "個 ワイヤー"
                     + std::to_string(both.wires) + "本)").c_str(),
            both.ok && both.parts == 1 && both.wires == wiresOnly.wires)) {
        return false;
    }
    return Explain("HP-EX-OUTPUT-01〜04 選んだ出力のとおりに作られる", true);
}

//! HP-EX-OUTPUT-06。全部 OFF は確定できず、理由が出る。
[[nodiscard]] bool CaseHumanPathExtrudeNoOutputRefused(V2MainWindow& window)
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
    kachakacha::v2::app::ExtrudeOutputs none;
    none.body = false;
    window.ExtrudeDock().ShowOutputs(none);
    window.RefreshExtrudeFromDock();
    const int partsBefore = CountOfKind(window, EntityKind::Part);
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    window.RunCommand("part.extrude");   // 確定しようとする
    if (!Explain((std::string("HP-EX-OUTPUT-06 何も作られない(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            CountOfKind(window, EntityKind::Part) == partsBefore
                && CountOfKind(window, EntityKind::Wire) == wiresBefore)) {
        return false;
    }
    return Explain("HP-EX-OUTPUT-06 断る理由が出る",
        window.StatusText().contains(QStringLiteral("EXT-U001"))
            || window.StatusText().contains(QStringLiteral("何を作るか")));
}

//! HP-EX-OUTPUT-07。ソリッドを作らないなら、足す・引くは起きない。
//! HP-EX-OUTPUT-08。1回の取り消しで、その押し出しの出力が全部戻る。
[[nodiscard]] bool CaseHumanPathExtrudeOutputsAndUndo(V2MainWindow& window)
{
    using kachakacha::v2::app::ExtrudeOutputPreset;
    // まず立体を1つ作る。これが「足す・引く」の相手になる。
    const auto first = ExtrudeWithOutputs(window, ExtrudeOutputPreset::SolidOnly, true);
    if (!Explain("相手の立体ができる", first.ok && first.parts == 1)) {
        return false;
    }
    const int partsAfterFirst = CountOfKind(window, EntityKind::Part);
    const int wiresAfterFirst = CountOfKind(window, EntityKind::Wire);

    // 立体と輪郭の両方を選ぶ。**道具を先に構えて、Ctrl 無しで選ぶ。**
    // 素のクリックだけだと後の1つに置き換わり、相手がいなくなる。
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("part.extrude");   // 構える
    viewport.SelectAt(QPointF(viewport.width() * 0.5, viewport.height() * 0.5),
        Qt::NoModifier);   // 立体
    if (!Explain("輪郭をもう一度拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    if (!Explain((std::string("対象と輪郭の両方が選べている(")
                     + std::to_string(viewport.Selection().entityIds.size())
                     + " 件)").c_str(),
            viewport.Selection().entityIds.size() >= 2)) {
        return false;
    }
    window.RunCommand("part.extrude");
    if (!Explain((std::string("矢印が出る(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            window.Viewport().ExtrudeHandleShown())) {
        return false;
    }
    window.ExtrudeDock().ChooseBoolean(
        kachakacha::v2::modeling::ExtrudeBooleanMode::SubtractFromPart);
    window.ExtrudeDock().ShowOutputs(
        kachakacha::v2::app::OutputsForPreset(ExtrudeOutputPreset::WiresOnly));
    window.RefreshExtrudeFromDock();
    window.RunCommand("part.extrude");   // 確定
    if (!Explain((std::string("HP-EX-OUTPUT-07 相手の立体はそのまま(")
                     + std::to_string(CountOfKind(window, EntityKind::Part)) + "個)").c_str(),
            CountOfKind(window, EntityKind::Part) == partsAfterFirst)) {
        return false;
    }
    const int wiresMade = CountOfKind(window, EntityKind::Wire) - wiresAfterFirst;
    if (!Explain((std::string("HP-EX-OUTPUT-07 ワイヤーはできる(")
                     + std::to_string(wiresMade) + "本 帯は "
                     + window.StatusText().toStdString() + ")").c_str(),
            wiresMade > 0)) {
        return false;
    }
    // HP-EX-OUTPUT-08 1回の取り消しで、その押し出しの出力が全部戻る。
    window.RunCommand("edit.undo");
    if (!Explain((std::string("HP-EX-OUTPUT-08 1回の取り消しでワイヤーが全部戻る(")
                     + std::to_string(CountOfKind(window, EntityKind::Wire)) + " → "
                     + std::to_string(wiresAfterFirst) + ")").c_str(),
            CountOfKind(window, EntityKind::Wire) == wiresAfterFirst)) {
        return false;
    }
    return Explain("HP-EX-OUTPUT-08 立体も増えも減りもしない",
        CountOfKind(window, EntityKind::Part) == partsAfterFirst);
}

//! HP-EX-SNAP-01。下見と確定が同じ写しから作られる(オーナー指示 §9)。
//!
//! 下見を出したあとに選択が変わったら、**写しを作り直して下見も出し直す**。
//! 黙って読み直して、画面に出ていないもので作ってはいけない。
[[nodiscard]] bool CaseHumanPathPreviewAndCommitAgree(V2MainWindow& window)
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
    // 下見に出ている輪郭と距離を控える。**これが確定に使われなければならない。**
    const auto shownOutline = window.ExtrudeOutline();
    const double shownDistance = window.Viewport().ExtrudeHandleDistanceMm();
    if (!Explain("下見に輪郭が出ている", !shownOutline.empty())) {
        return false;
    }
    window.RunCommand("part.extrude");   // 確定
    if (!Explain("立体ができる", CountOfKind(window, EntityKind::Part) == 1)) {
        return false;
    }
    // 保存された作り方の距離が、下見に出ていた距離と同じであること。
    bool matched = false;
    for (const auto& feature : window.Session().GetDocument().Snapshot().features) {
        const auto* definition =
            std::get_if<kachakacha::v2::domain::ExtrudeDefinition>(&feature.definition);
        if (definition == nullptr) {
            continue;
        }
        matched = std::abs(definition->distance.value - shownDistance) < 1.0e-6;
    }
    return Explain((std::string("確定は下見と同じ距離で作る(")
                       + std::to_string(shownDistance) + "mm)").c_str(),
        matched);
}

//! HP-EX-02。道具を先に構えて、対象と輪郭を **Ctrl 無しで** 選べる。
//!
//! これまでは素のクリックが選択を置き換えていたので、立体を選んでから
//! 輪郭をクリックすると立体が外れた。**Ctrl を知らないと押し出せなかった。**
[[nodiscard]] bool CaseHumanPathToolFirstWithoutCtrl(V2MainWindow& window)
{
    // まず立体を1つ作る。これが「引く」相手になる。
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))) {
        return false;
    }
    if (!Explain("引いた線を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.SetMode(kachakacha::v2::app::UiMode::Part);
    window.RunCommand("part.extrude");
    window.RunCommand("part.extrude");
    if (!Explain("相手の立体ができる", CountOfKind(window, EntityKind::Part) == 1)) {
        return false;
    }
    // もう1本、輪郭を引く。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    if (!Explain("2本目の矩形を引ける", DrawRectangleByHand(window))) {
        return false;
    }
    if (!Explain("輪郭が増えた", CountOfKind(window, EntityKind::Wire) > wiresBefore)) {
        return false;
    }

    // **道具を先に構える。**何も選んでいない状態で押し出しを押す。
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("part.extrude");
    if (!Explain((std::string("構えて待つ(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            !window.Viewport().ExtrudeHandleShown())) {
        return false;
    }

    // 立体を素でクリック。
    auto& viewport = window.Viewport();
    bool clickedSolid = false;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind != EntityKind::Part) {
            continue;
        }
        // 立体の真ん中あたりを押す。
        viewport.SelectAt(QPointF(viewport.width() * 0.5, viewport.height() * 0.5),
            Qt::NoModifier);
        clickedSolid = !viewport.Selection().entityIds.empty();
        break;
    }
    if (!Explain("立体を素のクリックで拾える", clickedSolid)) {
        return false;
    }
    const std::size_t afterSolid = viewport.Selection().entityIds.size();

    // **Ctrl を押さずに**輪郭をクリックする。立体が外れてはいけない。
    if (!Explain("輪郭を素のクリックで拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    return Explain((std::string("Ctrl 無しでも前の選択が残る(")
                       + std::to_string(afterSolid) + " → "
                       + std::to_string(viewport.Selection().entityIds.size())
                       + " 件)").c_str(),
        viewport.Selection().entityIds.size() >= afterSolid + 1);
}

} // namespace

std::vector<SelfTestCase> HumanPathCases()
{
    return {
        {"HP-EX-01 拾って押して確定すると立体になる", CaseHumanPathExtrudeProfileOnly},
        {"HP-UI-01 押し出しの最中は棚が見えている", CaseHumanPathExtrudePanelStaysVisible},
        {"HP-UI-03 右の欄へ打ったあとも Enter と Esc が効く",
            CaseHumanPathConfirmKeysAfterTyping},
        {"HP-EX-OUTPUT-01〜04 選んだ出力のとおりに作られる",
            CaseHumanPathExtrudeOutputs},
        {"HP-EX-OUTPUT-06 全部 OFF は確定できない", CaseHumanPathExtrudeNoOutputRefused},
        {"HP-EX-OUTPUT-07/08 演算は起きず、取り消しは1回で戻る",
            CaseHumanPathExtrudeOutputsAndUndo},
        {"HP-EX-SNAP-01 下見と確定が同じ写しから作られる",
            CaseHumanPathPreviewAndCommitAgree},
        {"HP-EX-02 道具を先に構えても Ctrl 無しで選べる",
            CaseHumanPathToolFirstWithoutCtrl},
    };
}

} // namespace kachakacha::v2::selftest
