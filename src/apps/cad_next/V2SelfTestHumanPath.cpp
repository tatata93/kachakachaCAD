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
#include "V2FabricationDock.h"
#include "V2MainWindow.h"
#include "V2PartDock.h"
#include "V2SurfaceDock.h"
#include "V2Viewport.h"
#include "V2WorkPlaneDock.h"

#include "kachakacha/app/CommandParameters.h"
#include "kachakacha/app/ExtrudeInputState.h"
#include "kachakacha/app/OriginPlanes.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/SurfaceInputState.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/modeling/ToolController.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <QPointF>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::Shelf;
using kachakacha::v2::domain::EntityKind;

[[nodiscard]] int CountVisibleOfKind(V2MainWindow& window, EntityKind kind)
{
    int count = 0;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kind
            && entity.visibility == kachakacha::v2::domain::Visibility::Visible) {
            ++count;
        }
    }
    return count;
}

} // namespace

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

//! 5本の別々の直線で、閉じた輪郭を描く。Rectangle 1個ではないことが本題。
[[nodiscard]] bool DrawWireLoopByHand(V2MainWindow& window,
    const std::vector<QPointF>& points)
{
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetViewCenter(kachakacha::v2::geometry::Vector3{});
    viewport.SetVisibleWidthMm(200.0);
    viewport.SetSnapSuppressed(true);
    for (std::size_t index = 0; index < points.size(); ++index) {
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
        viewport.ClickAt(points[index]);
        viewport.HoverAt(points[(index + 1) % points.size()]);
        viewport.ClickAt(points[(index + 1) % points.size()]);
    }
    viewport.SetSnapSuppressed(false);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    return true;
}

[[nodiscard]] bool DrawFiveWireLoopByHand(V2MainWindow& window)
{
    auto& viewport = window.Viewport();
    const std::vector<QPointF> points{{viewport.width() * 0.30, viewport.height() * 0.32},
        {viewport.width() * 0.62, viewport.height() * 0.32},
        {viewport.width() * 0.70, viewport.height() * 0.50},
        {viewport.width() * 0.60, viewport.height() * 0.70},
        {viewport.width() * 0.30, viewport.height() * 0.70}};
    return DrawWireLoopByHand(window, points)
        && CountOfKind(window, EntityKind::Wire) == 5;
}

[[nodiscard]] bool SelectAllVisibleWiresByHand(V2MainWindow& window)
{
    auto& viewport = window.Viewport();
    std::vector<kachakacha::v2::base::EntityId> picked;
    for (const auto& curve : window.Session().Scene().curves) {
        if (std::find(picked.begin(), picked.end(), curve.entityId) != picked.end()) {
            continue;
        }
        const auto screen = viewport.Mapping().Project(curve.segment.Evaluate(0.5));
        if (!screen.has_value()) {
            continue;
        }
        viewport.SelectAt(QPointF(screen->x, screen->y),
            picked.empty() ? Qt::NoModifier : Qt::ControlModifier);
        picked.push_back(curve.entityId);
    }
    return viewport.Selection().entityIds.size() == picked.size() && picked.size() == 5;
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

//! 画面に出ている形状ガイドの中央を押す。IDを試験から選択へ直接入れない。
[[nodiscard]] bool ClickOnAnyGuideSurface(V2MainWindow& window)
{
    auto& viewport = window.Viewport();
    for (const auto& shape : viewport.ShapeViews()) {
        if (!shape.surface || shape.mesh.Empty()) {
            continue;
        }
        const auto center = kachakacha::v2::geometry::Vector3{
            (shape.mesh.minimum.x + shape.mesh.maximum.x) * 0.5,
            (shape.mesh.minimum.y + shape.mesh.maximum.y) * 0.5,
            (shape.mesh.minimum.z + shape.mesh.maximum.z) * 0.5};
        const auto screen = viewport.Mapping().Project(center);
        if (!screen.has_value()) {
            continue;
        }
        if (!Explain("形状ガイドの塗りに当たり判定がある",
                viewport.PickShapeAt(QPointF(screen->x, screen->y)).has_value())) {
            continue;
        }
        viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
        // fabrication.create は「部品か形状ガイドを1つ」で条件を満たすため、
        // tool-first ではクリックの通知中にそのまま実行される。実行後の選択だけを
        // 見ると、正しく拾って完了した経路まで失敗扱いになる。
        if (window.FabricationModelCount() > 0) {
            return true;
        }
        for (const auto& id : viewport.Selection().entityIds) {
            if (id == shape.entityId) {
                return true;
            }
        }
    }
    return false;
}

namespace {

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
    // 3D の中に役割の札が出ていること(§7)。
    if (!Explain("3D に PROFILE の札が出ている",
            !window.Viewport().ToolRoleLabels().empty())) {
        return false;
    }
    // うすい面が敷かれていること(§8)。線だけだと厚みが読めない。
    if (!Explain((std::string("下見にうすい面が敷かれている(")
                     + std::to_string(window.Viewport().ExtrudePreviewFaces().size())
                     + "枚)").c_str(),
            !window.Viewport().ExtrudePreviewFaces().empty())) {
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
    //
    // 順は「輪郭 → 相手の立体」。構えた直後に求めているのは輪郭なので、
    // 先に立体を押すと **その面が輪郭として拾われる**(それはそれで正しい。
    // 面を押す道である)。画面が次に何を求めているかのとおりに押す。
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("part.extrude");   // 構える
    if (!Explain("輪郭を拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    viewport.SelectAt(QPointF(viewport.width() * 0.5, viewport.height() * 0.5),
        Qt::NoModifier);   // 相手の立体
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

//! HP-SF-01。「面を作る」を押すと棚が見え、下見が出て、**文書はまだ増えていない**。
[[nodiscard]] bool CaseHumanPathSurfacePreviewOnly(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))) {
        return false;
    }
    if (!Explain("引いた線を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    const int before = CountOfKind(window, EntityKind::GuideSurface);
    window.RunCommand("surface.create");
    if (!Explain("「面を作る」の棚が見えている", window.ShelfShown(Shelf::Surface))) {
        return false;
    }
    if (!Explain((std::string("下見が出ている(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            window.SurfacePreviewShown()
                && !window.Viewport().ToolPreview().empty())) {
        return false;
    }
    // 3D の中に役割の札が出ていること(§7)。
    // 棚に「境界 1本」と出ていても、画面のどの線かが分からないと選び直せない。
    const auto& labels = window.Viewport().ToolRoleLabels();
    if (!Explain((std::string("3D に役割の札が出ている(")
                     + (labels.empty() ? std::string("なし")
                                       : labels.front().text.toStdString())
                     + ")").c_str(),
            !labels.empty())) {
        return false;
    }
    // ここが本題。**確定するまで文書へ書かない**(§12)。
    return Explain("下見だけで、文書の面は増えていない",
        CountOfKind(window, EntityKind::GuideSurface) == before);
}

//! HP-SF-02。Enter で確定すると面が1枚でき、棚と下見が片付く。
[[nodiscard]] bool CaseHumanPathSurfaceConfirmWithEnter(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))) {
        return false;
    }
    if (!Explain("引いた線を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.RunCommand("surface.create");
    if (!Explain("下見が出ている", window.SurfacePreviewShown())) {
        return false;
    }
    // **3D を触らずに** Enter。焦点が右の棚にあっても効くこと(§14)。
    if (!Explain("Enter を窓が受け取る", window.HandleToolKey(Qt::Key_Return, nullptr))) {
        return false;
    }
    if (!Explain((std::string("面が1枚できる(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            CountOfKind(window, EntityKind::GuideSurface) == 1)) {
        return false;
    }
    if (!Explain("棚が片付く", !window.ShelfShown(Shelf::Surface))) {
        return false;
    }
    return Explain("下見の線と札が消える",
        window.Viewport().ToolPreview().empty()
            && window.Viewport().ToolRoleLabels().empty());
}

//! HP-SF-03。Esc でやめると、**何も作られていない**。
[[nodiscard]] bool CaseHumanPathSurfaceCancel(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))) {
        return false;
    }
    if (!Explain("引いた線を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.RunCommand("surface.create");
    if (!Explain("棚が見えている", window.ShelfShown(Shelf::Surface))) {
        return false;
    }
    if (!Explain("Esc を窓が受け取る", window.HandleToolKey(Qt::Key_Escape, nullptr))) {
        return false;
    }
    if (!Explain("棚が片付く", !window.ShelfShown(Shelf::Surface))) {
        return false;
    }
    return Explain((std::string("何も作られていない(帯は ")
                       + window.StatusText().toStdString() + ")").c_str(),
        CountOfKind(window, EntityKind::GuideSurface) == 0
            && window.Viewport().ToolPreview().empty());
}

//! HP-SF-04。作り方を変えても、入れたものは消えない(§11)。
[[nodiscard]] bool CaseHumanPathSurfaceMethodKeepsInput(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))) {
        return false;
    }
    if (!Explain("引いた線を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.RunCommand("surface.create");
    // 閉じた同一平面の輪郭1本 → 平面を薦める(§11)。境界欄へ入っている。
    if (!Explain((std::string("平面を薦めて境界へ入れる(")
                     + std::to_string(window.SurfaceInput().boundaries.size())
                     + "本)").c_str(),
            window.SurfaceInput().method
                    == kachakacha::v2::modeling::GuideSurfaceMethod::PlanarBoundary
                && window.SurfaceInput().boundaries.size() == 1)) {
        return false;
    }
    // 人が作り方を変える。**見えているカードを押す。**
    if (!Explain("ロフトのカードが見えていて押せる",
            window.SurfaceDock().ClickMethodCard(
                kachakacha::v2::modeling::GuideSurfaceMethod::LoftSections))) {
        return false;
    }
    if (!Explain("入れた境界は残っている", window.SurfaceInput().boundaries.size() == 1)) {
        return false;
    }
    if (!Explain("ロフトになっている",
            window.SurfaceInput().method
                == kachakacha::v2::modeling::GuideSurfaceMethod::LoftSections)) {
        return false;
    }
    // ロフトは断面が要る。まだ作れないので、下見も出ない。
    if (!Explain("まだ作れないので下見は出ない", !window.SurfacePreviewShown())) {
        return false;
    }
    window.HandleToolKey(Qt::Key_Escape, nullptr);
    return true;
}

[[nodiscard]] bool CaseHumanPathFiveWiresBecomeOneProfile(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("5本の別ワイヤーで閉じた輪郭を描ける",
            DrawFiveWireLoopByHand(window))) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SelectAt(QPointF(4.0, 4.0), Qt::NoModifier);
    window.RunCommand("surface.create");
    if (!Explain("面を作るを先に構えると専用棚と閉じた領域が見つかる",
            window.ShelfShown(Shelf::Surface) && viewport.ProfileRegionPicking()
                && viewport.ProfileRegionCount() == 1)) {
        return false;
    }
    const QPointF surfaceInside(viewport.width() * 0.5, viewport.height() * 0.5);
    viewport.HoverAt(surfaceInside);
    viewport.SelectAt(surfaceInside, Qt::NoModifier);
    if (!Explain("内側1クリックで面の境界5本を選べる",
            viewport.Selection().entityIds.size() == 5)) {
        return false;
    }
    if (!Explain("クリック直後に5本を平面の輪郭として下見する",
            window.SurfaceInput().method
                    == kachakacha::v2::modeling::GuideSurfaceMethod::PlanarBoundary
                && window.SurfacePreviewShown())) {
        return false;
    }
    if (!Explain("棚に5本を1つの閉じた輪郭と表示する",
            window.SurfaceDock().SlotTextJa(
                kachakacha::v2::modeling::ChainRole::BoundarySide)
                .contains(QStringLiteral("1つの閉じた輪郭")))) {
        return false;
    }
    window.HandleToolKey(Qt::Key_Escape, nullptr);
    if (!Explain("面作成をやめた後も5本を選び直せる",
            SelectAllVisibleWiresByHand(window))) {
        return false;
    }
    window.SetMode(kachakacha::v2::app::UiMode::Part);
    const auto selectedCurves = kachakacha::v2::app::SelectedCurves(
        window.Viewport().Selection(), window.Session().Scene());
    if (!Explain((std::string("部品モードでも5本が閉路のまま残る(曲線 ")
                     + std::to_string(selectedCurves.size()) + "本)").c_str(),
            kachakacha::v2::geometry::SegmentsFormClosedLoop(selectedCurves,
                window.Session().GetDocument().Snapshot().settings.tolerance))) {
        return false;
    }
    // ここからは線を1本ずつ選ばない。空白で選択を外し、道具を先に構えて、
    // 囲まれた内側を1回押す。これが人へ提供する正規の経路である。
    window.Viewport().SelectAt(QPointF(4.0, 4.0), Qt::NoModifier);
    window.RunCommand("part.extrude");
    if (!Explain("押し出しを先に構えると閉じた領域が見つかる",
            viewport.ProfileRegionPicking() && viewport.ProfileRegionCount() == 1)) {
        return false;
    }
    const QPointF inside(viewport.width() * 0.5, viewport.height() * 0.5);
    viewport.HoverAt(inside);
    if (!Explain("輪郭線ではなく内側がHover対象になる",
            viewport.HoveredProfileRegion().has_value())) {
        return false;
    }
    viewport.SelectAt(inside, Qt::NoModifier);
    if (!Explain("内側1クリックで5本すべてが入力になる",
            viewport.Selection().entityIds.size() == 5)) {
        return false;
    }
    if (!Explain((std::string("クリック直後に同じ5本を1つの輪郭として下見できる(選択 ")
                     + std::to_string(window.Viewport().Selection().entityIds.size())
                     + "本、帯は " + window.StatusText().toStdString() + ")").c_str(),
            window.Viewport().ExtrudeHandleShown())) {
        return false;
    }
    window.RunCommand("part.extrude");
    return Explain("5本の輪郭から立体を確定できる",
        CountOfKind(window, EntityKind::Part) == 1);
}

//! PROFILE-03〜05/07。穴・複数領域・再クリック解除・開いた線を、
//! **道具を先に構えた画面クリック**で確かめる。
[[nodiscard]] bool CaseHumanPathProfileRegionVariants(V2MainWindow& window)
{
    window.RunCommand("file.new");
    auto& viewport = window.Viewport();
    const auto point = [&viewport](double x, double y) {
        return QPointF(viewport.width() * x, viewport.height() * y);
    };
    if (!DrawWireLoopByHand(window,
            {point(0.18, 0.20), point(0.82, 0.20), point(0.82, 0.80), point(0.18, 0.80)})
        || !DrawWireLoopByHand(window,
            {point(0.42, 0.40), point(0.58, 0.40), point(0.58, 0.60), point(0.42, 0.60)})) {
        return false;
    }
    viewport.SelectAt(QPointF(4.0, 4.0), Qt::NoModifier);
    window.RunCommand("part.extrude");
    if (!Explain("PROFILE-03 外周と内周を穴付きの1領域にする",
            viewport.ProfileRegionCount() == 1)) {
        return false;
    }
    viewport.SelectAt(point(0.30, 0.50), Qt::NoModifier);
    if (!Explain("PROFILE-03 内側1クリックで外周と穴の8本を取り込む",
            viewport.Selection().entityIds.size() == 8)) {
        return false;
    }
    viewport.SelectAt(point(0.30, 0.50), Qt::NoModifier);
    if (!Explain("PROFILE-05 選択済み領域の再クリックで解除する",
            viewport.Selection().entityIds.empty())) {
        return false;
    }
    window.HandleToolKey(Qt::Key_Escape, nullptr);

    window.RunCommand("file.new");
    if (!DrawWireLoopByHand(window,
            {point(0.12, 0.30), point(0.40, 0.30), point(0.40, 0.70), point(0.12, 0.70)})
        || !DrawWireLoopByHand(window,
            {point(0.60, 0.30), point(0.88, 0.30), point(0.88, 0.70), point(0.60, 0.70)})) {
        return false;
    }
    viewport.SelectAt(QPointF(4.0, 4.0), Qt::NoModifier);
    window.RunCommand("part.extrude");
    if (!Explain("PROFILE-04 独立した閉領域を2個見つける",
            viewport.ProfileRegionCount() == 2)) {
        return false;
    }
    viewport.SelectAt(point(0.26, 0.50), Qt::NoModifier);
    viewport.SelectAt(point(0.74, 0.50), Qt::NoModifier);
    if (!Explain("PROFILE-04 Ctrl無しの2クリックで2輪郭を追加する",
            viewport.Selection().entityIds.size() == 8)) {
        return false;
    }
    window.HandleToolKey(Qt::Key_Escape, nullptr);

    window.RunCommand("file.new");
    viewport.SetSnapSuppressed(true);
    const std::vector<QPointF> open{point(0.25, 0.30), point(0.70, 0.30),
        point(0.70, 0.70), point(0.25, 0.70)};
    for (std::size_t index = 1; index < open.size(); ++index) {
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
        viewport.ClickAt(open[index - 1]);
        viewport.HoverAt(open[index]);
        viewport.ClickAt(open[index]);
    }
    viewport.SetSnapSuppressed(false);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SelectAt(QPointF(4.0, 4.0), Qt::NoModifier);
    window.RunCommand("part.extrude");
    const bool openRefused = viewport.ProfileRegionPicking()
        && viewport.ProfileRegionCount() == 0
        && window.StatusText().contains(QStringLiteral("閉じ"));
    window.HandleToolKey(Qt::Key_Escape, nullptr);
    return Explain("PROFILE-07 開いた線から内部領域を作らず理由を示す", openRefused);
}

//! いま画面に出ている立体の、上下の広がり(mm)。押せたかどうかを高さで見る。
[[nodiscard]] double SolidHeightMm(V2MainWindow& window)
{
    double height = -1.0;
    for (const auto& shape : window.Viewport().ShapeViews()) {
        if (shape.surface || shape.mesh.Empty()) {
            continue;
        }
        height = shape.mesh.maximum.z - shape.mesh.minimum.z;
    }
    return height;
}

//! HP-EX-03。**立体の面を画面から拾って**押す。
//!
//! 面を押す道は、これまで `SelectionRef` を手で組んで `pickedFaceIndex = 0` と
//! 書く試験しか無かった。それでは「人が面を拾えるか」を何も確かめていない。
//! ここでは素のクリックで面を拾い、棚と札と下見が見えることまで見る。
[[nodiscard]] bool CaseHumanPathPushAFace(V2MainWindow& window)
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
    window.RunCommand("part.extrude");   // 確定。これが押す相手になる。
    if (!Explain("押す相手の立体ができる", CountOfKind(window, EntityKind::Part) == 1)) {
        return false;
    }
    const double before = SolidHeightMm(window);
    if (!Explain((std::string("立体の高さが読める(") + std::to_string(before)
                     + "mm)").c_str(),
            before > 0.0)) {
        return false;
    }

    // **道具を先に構えてから、立体の真ん中を素で押す。**
    // 構えている間、押し出しが求めているのは輪郭なので、面が前に出る(§6)。
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("part.extrude");
    viewport.SelectAt(QPointF(viewport.width() * 0.5, viewport.height() * 0.5),
        Qt::NoModifier);
    bool pickedFace = false;
    for (const auto& ref : viewport.Selection().ordered) {
        if (ref.kind == kachakacha::v2::app::SelectionElementKind::Face
            && ref.pickedFaceIndex.has_value()) {
            pickedFace = true;
        }
    }
    // ここが本題。**手で番号を書かずに、押した場所から面が拾えたか。**
    if (!Explain((std::string("素のクリックで面が拾える(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            pickedFace)) {
        return false;
    }

    if (!Explain((std::string("矢印が出る(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            viewport.ExtrudeHandleShown())) {
        return false;
    }
    if (!Explain("押し出しの棚が見えている", window.ShelfShown(Shelf::Extrude))) {
        return false;
    }
    if (!Explain("3D に役割の札が出ている", !viewport.ToolRoleLabels().empty())) {
        return false;
    }
    // 見える距離を打ってから、**3D を触らずに** Enter。
    window.ExtrudeDock().TypeDistanceMm(12.0);
    window.RefreshExtrudeFromDock();
    if (!Explain("Enter を窓が受け取る", window.HandleToolKey(Qt::Key_Return, nullptr))) {
        return false;
    }
    if (!Explain((std::string("見える立体は1つのまま(")
                     + std::to_string(CountVisibleOfKind(window, EntityKind::Part))
                     + "個)").c_str(),
            CountVisibleOfKind(window, EntityKind::Part) == 1)) {
        return false;
    }
    const double after = SolidHeightMm(window);
    return Explain((std::string("面を押したぶん高くなる(") + std::to_string(before)
                       + "mm → " + std::to_string(after) + "mm)").c_str(),
        after > before + 1.0);
}

//! HP-UI-02。**選んだものが画面に出ている**(§7)。
//!
//! これまで「対象」と「輪郭」は1本の文字列だった。どちらを選び直すのかが
//! 読めず、3D のどれがその役割なのかも分からなかった。
//! ここでは、右の棚の2欄と 3D の札の両方が、選んだものを名前で指すことを見る。
[[nodiscard]] bool CaseHumanPathSelectionIsVisible(V2MainWindow& window)
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
    if (!Explain("押し出しの棚が見えている", window.ShelfShown(Shelf::Extrude))) {
        return false;
    }
    // 輪郭の欄に名前が出ていること。「(選んでいません)」のままではない。
    const QString profileText = window.ExtrudeDock().ProfileTextJa();
    if (!Explain((std::string("輪郭の欄に名前が出ている(")
                     + profileText.toStdString() + ")").c_str(),
            !profileText.isEmpty()
                && profileText != QStringLiteral("(選んでいません)"))) {
        return false;
    }
    // **対象と輪郭は別の欄である。**1本の文字列に混ぜない。
    const QString targetText = window.ExtrudeDock().TargetTextJa();
    if (!Explain((std::string("対象の欄は別に出ている(") + targetText.toStdString()
                     + ")").c_str(),
            !targetText.isEmpty() && targetText != profileText)) {
        return false;
    }
    // 3D の札も、その輪郭を指していること。
    bool sawProfileLabel = false;
    for (const auto& label : window.Viewport().ToolRoleLabels()) {
        if (label.text.startsWith(QStringLiteral("PROFILE"))) {
            sawProfileLabel = true;
        }
    }
    if (!Explain("3D に PROFILE の札が出ている", sawProfileLabel)) {
        return false;
    }
    // 一番下の一行にも、同じものが並んでいること。
    const std::string footer = window.ToolFooterTextJa().toStdString();
    if (!Explain((std::string("一番下の一行にも出ている(") + footer + ")").c_str(),
            footer.find("PROFILE=") != std::string::npos
                && footer.find("TARGET=") != std::string::npos)) {
        return false;
    }
    window.HandleToolKey(Qt::Key_Escape, nullptr);
    return Explain("やめると札も一行も消える",
        window.Viewport().ToolRoleLabels().empty()
            && window.ToolFooterTextJa().isEmpty());
}

//! HP-FAB-01。面を画面から拾って近似し、70%曲げからワイヤーを作る。
[[nodiscard]] bool CaseHumanPathSurfaceToFabricationWire(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))
        || !Explain("境界を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.RunCommand("surface.create");
    if (!Explain("面の下見が見える", window.SurfacePreviewShown())
        || !Explain("Enterで面を確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("形状ガイドができる",
            CountOfKind(window, EntityKind::GuideSurface) == 1)) {
        return false;
    }

    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Isometric);
    viewport.FitToDocument();
    // 空所を押して前工程の境界選択を外す。選択をIDで注入しない。
    viewport.SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);
    window.SetMode(kachakacha::v2::app::UiMode::Fabrication);
    window.RunCommand("fabrication.create");
    // 道具から始める(引継ぎ 3)。押しただけでは何も作らず、棚が構える。
    if (!Explain("近似を押すと棚が構え、まだ何も作らない",
            window.ApproxShelfShown() && window.FabricationModelCount() == 0)
        || !Explain("構えてから面を画面で拾える", ClickOnAnyGuideSurface(window))) {
        return false;
    }
    auto& dock = window.FabricationDock();
    // 既定の候補は棚の方式どおり(帯)。70% の曲げに帯が要るので、ここで確かめる。
    if (!Explain("既定の候補は帯(B)", dock.SelectedCandidateShown() == 1)
        || !Explain("Enterで確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("製作モデルができる", window.FabricationModelCount() == 1)
        || !Explain("製作の棚が見えている", window.ShelfShown(Shelf::Fabrication))) {
        return false;
    }

    dock.SetStageIndex(1);
    if (!Explain("曲げ確認の工程が見える", dock.StageIndex() == 1)) {
        return false;
    }
    dock.SetAssemblyPercent(70.0);
    dock.PressApplyAssembly();
    if (!Explain((std::string("70%が欄と形へ反映される(実際 ")
                     + std::to_string(dock.AssemblyPercent()) + "%)").c_str(),
            std::abs(dock.AssemblyPercent() - 70.0) < 1.0e-6)) {
        return false;
    }

    const int wiresBefore = CountVisibleOfKind(window, EntityKind::Wire);
    window.RunCommand("fabrication.freeze_state");
    if (!Explain("70%の状態からワイヤーが増える",
            CountVisibleOfKind(window, EntityKind::Wire) > wiresBefore)) {
        return false;
    }
    return Explain("生成後も元の近似モデルが残る",
        window.FabricationModelCount() == 1);
}

} // namespace

//! 上から見て、画面の割合で指した場所に矩形を1つ引く。引いた線の番号を返す。
[[nodiscard]] kachakacha::v2::base::EntityId DrawRectangleAtByHand(V2MainWindow& window,
    double x0, double y0, double x1, double y1)
{
    auto& viewport = window.Viewport();
    const int before = CountOfKind(window, EntityKind::Wire);
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetViewCenter(kachakacha::v2::geometry::Vector3{});
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Rectangle);
    viewport.SetSnapSuppressed(true);
    viewport.ClickAt(QPointF(viewport.width() * x0, viewport.height() * y0));
    viewport.HoverAt(QPointF(viewport.width() * x1, viewport.height() * y1));
    viewport.ClickAt(QPointF(viewport.width() * x1, viewport.height() * y1));
    viewport.SetSnapSuppressed(false);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    if (CountOfKind(window, EntityKind::Wire) != before + 1) {
        return kachakacha::v2::base::EntityId{};
    }
    // いちばん新しい線。
    kachakacha::v2::base::EntityId newest;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Wire) {
            newest = entity.id;
        }
    }
    return newest;
}

//! その線の上を押す。**画面で拾う道をそのまま使う。**番号を選択へ直接入れない。
[[nodiscard]] bool ClickOnCurveOf(V2MainWindow& window, const kachakacha::v2::base::EntityId& id)
{
    auto& viewport = window.Viewport();
    for (const auto& curve : window.Session().Scene().curves) {
        if (curve.entityId != id) {
            continue;
        }
        const auto screen = viewport.Mapping().Project(curve.segment.Evaluate(0.5));
        if (!screen.has_value()) {
            continue;
        }
        viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
        return true;
    }
    return false;
}

namespace {

[[nodiscard]] bool Holds(const std::vector<kachakacha::v2::base::EntityId>& list,
    const kachakacha::v2::base::EntityId& id)
{
    return std::find(list.begin(), list.end(), id) != list.end();
}

//! HP-SF-06。**3D のクリックがどの欄へ入るかが、いつも見えている**
//! (引継ぎ 2026-09-17 の 1)。断面へ順に押し、再び押して外し、
//! ガイドの欄へ替えて押し、欄ごと解除する。全部 3D の素のクリックと
//! 見えているボタンで行う。Ctrl も番号も使わない。
[[nodiscard]] bool CaseHumanPathSurfaceSlotsFollowClicks(V2MainWindow& window)
{
    using kachakacha::v2::modeling::ChainRole;
    using kachakacha::v2::modeling::GuideSurfaceMethod;
    window.RunCommand("file.new");
    const auto a = DrawRectangleAtByHand(window, 0.12, 0.12, 0.38, 0.38);
    const auto b = DrawRectangleAtByHand(window, 0.62, 0.12, 0.88, 0.38);
    const auto c = DrawRectangleAtByHand(window, 0.12, 0.62, 0.38, 0.88);
    if (!Explain("矩形を3つ手で引ける", !a.IsNil() && !b.IsNil() && !c.IsNil())) {
        return false;
    }
    // 何も選ばずに道具を押す。
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("surface.create");
    if (!Explain("棚が見えている", window.ShelfShown(Shelf::Surface))) {
        return false;
    }
    if (!Explain("空入力は境界の欄が「ここへ選ぶ」になっている",
            window.SurfaceDock().ActiveSlotShown() == ChainRole::BoundarySide)) {
        return false;
    }
    // ロフトのカードを押すと、断面の欄が「ここへ選ぶ」に変わる。
    if (!Explain("ロフトのカードが押せる",
            window.SurfaceDock().ClickMethodCard(GuideSurfaceMethod::LoftSections))) {
        return false;
    }
    if (!Explain("ロフトでは断面の欄が「ここへ選ぶ」",
            window.SurfaceDock().ActiveSlotShown() == ChainRole::Section)) {
        return false;
    }
    // 3D で順に押す。
    if (!Explain("線を順に押せる", ClickOnCurveOf(window, a) && ClickOnCurveOf(window, b)
                && ClickOnCurveOf(window, c))) {
        return false;
    }
    const auto& in = window.SurfaceInput();
    if (!Explain((std::string("押した順に断面へ入る(") + std::to_string(in.sections.size())
                     + "本)").c_str(),
            in.sections.size() == 3 && in.sections[0] == a && in.sections[1] == b
                && in.sections[2] == c)) {
        return false;
    }
    if (!Explain("3D の印も3本", window.Viewport().Selection().entityIds.size() == 3)) {
        return false;
    }
    if (!Explain((std::string("欄に名前が並ぶ(")
                     + window.SurfaceDock().SlotTextJa(ChainRole::Section).toStdString()
                     + ")").c_str(),
            window.SurfaceDock().SlotTextJa(ChainRole::Section).startsWith(
                QStringLiteral("3本")))) {
        return false;
    }
    // もう一度押すと外れる。Ctrl は要らない。
    if (!Explain("2本目をもう一度押せる", ClickOnCurveOf(window, b))) {
        return false;
    }
    if (!Explain("再び押した線は断面から外れる",
            in.sections.size() == 2 && !Holds(in.sections, b)
                && !Holds(window.Viewport().Selection().entityIds, b))) {
        return false;
    }
    // ロフトにガイドの欄は無い。押せないことも見えている。
    if (!Explain("ロフトではガイドの「ここへ選ぶ」が押せない",
            !window.SurfaceDock().ClickActivate(ChainRole::GuideU))) {
        return false;
    }
    // 案内付きロフトへ替え、ガイドの欄へ替えてから押す。
    if (!Explain("案内付きロフトのカードが押せる",
            window.SurfaceDock().ClickMethodCard(GuideSurfaceMethod::GuidedLoft))) {
        return false;
    }
    if (!Explain("ガイドの「ここへ選ぶ」が押せる",
            window.SurfaceDock().ClickActivate(ChainRole::GuideU))) {
        return false;
    }
    if (!Explain("ガイドの欄が「ここへ選ぶ」になった",
            window.SurfaceDock().ActiveSlotShown() == ChainRole::GuideU
                && in.activeSlot == ChainRole::GuideU)) {
        return false;
    }
    if (!Explain("線を押せる", ClickOnCurveOf(window, b))) {
        return false;
    }
    if (!Explain("ガイドへ入り、断面はそのまま",
            in.guides.size() == 1 && in.guides[0] == b && in.sections.size() == 2)) {
        return false;
    }
    // 断面に入っている線をガイドの欄で押すと、断面からガイドへ移る。
    if (!Explain("断面の線をガイドの欄で押せる", ClickOnCurveOf(window, a))) {
        return false;
    }
    if (!Explain("1本は1つの欄にしか入らない",
            in.guides.size() == 2 && in.sections.size() == 1 && in.sections[0] == c)) {
        return false;
    }
    // 欄ごと解除。3D の印も消える。
    if (!Explain("断面の「解除」が押せる", window.SurfaceDock().ClickClear(ChainRole::Section))) {
        return false;
    }
    if (!Explain("断面が空になり、3D の印はガイドの2本だけ",
            in.sections.empty() && window.Viewport().Selection().entityIds.size() == 2)) {
        return false;
    }
    // 一番下の一行にも、次のクリックがどこへ入るかが出ている。
    const auto footer = window.ToolFooterTextJa().toStdString();
    if (!Explain((std::string("一番下の一行に NEXT= が出ている(") + footer + ")").c_str(),
            footer.find("NEXT=ガイド") != std::string::npos)) {
        return false;
    }
    window.HandleToolKey(Qt::Key_Escape, nullptr);
    return Explain("やめると 3D の印も消える",
        window.Viewport().Selection().entityIds.empty()
            && !window.ShelfShown(Shelf::Surface));
}

//! 上面から `offsetMm` 離した作業平面を作って、使う状態にする(場面づくり)。
[[nodiscard]] bool UseTopPlaneOffsetBy(V2MainWindow& window, double offsetMm)
{
    window.SetWorkPlaneChooser({});
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("workplane.create");
    V2WorkPlaneDock* dock = window.WorkPlaneDock();
    if (dock == nullptr) {
        return false;
    }
    const auto top = kachakacha::v2::app::OriginPlaneId(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::modeling::StandardPlaneKind::XY);
    if (!top.has_value()) {
        return false;
    }
    kachakacha::v2::app::WorkPlaneChoice choice;
    choice.method = kachakacha::v2::modeling::WorkPlaneMethod::OffsetFromPlane;
    choice.referencePlaneId = top;
    choice.offsetMm = offsetMm;
    dock->SetChoice(choice);
    if (!dock->CanCreate()) {
        return false;
    }
    dock->PressCreate();
    return std::abs(window.Viewport().WorkPlane().origin.z - offsetMm) < 1.0e-6;
}

//! 保存される作り方に「断面順の手動固定」が残っているか。
[[nodiscard]] bool GuideSurfaceLockedInDocument(V2MainWindow& window)
{
    for (const auto& feature : window.Session().GetDocument().Snapshot().features) {
        const auto* guide =
            std::get_if<kachakacha::v2::domain::CreateGuideSurfaceDefinition>(
                &feature.definition);
        if (guide != nullptr) {
            return guide->lockSectionOrder;
        }
    }
    return false;
}

//! HP-SF-07。ロフトを道具から始めて、断面を **わざと順不同に** 押す。
//! 「3. 断面順」には押した順ではなく **採用した順** が出る。
//! 手動固定にすると表示順がそのまま固定され、↑で入れ替えた順が
//! そのまま生成順として確定まで届く(引継ぎ 2026-09-17 の 2)。
[[nodiscard]] bool CaseHumanPathLoftOrderFollowsAdoption(V2MainWindow& window)
{
    using kachakacha::v2::app::SurfaceOrdering;
    using kachakacha::v2::modeling::ChainRole;
    using kachakacha::v2::modeling::GuideSurfaceMethod;
    window.RunCommand("file.new");
    // 高さの違う断面3つ。下から z=0, 30, 60。
    const auto low = DrawRectangleAtByHand(window, 0.30, 0.30, 0.70, 0.70);
    if (!Explain("下の断面を引ける", !low.IsNil())) {
        return false;
    }
    if (!Explain("30mm 上の作業平面を使える", UseTopPlaneOffsetBy(window, 30.0))) {
        return false;
    }
    const auto middle = DrawRectangleAtByHand(window, 0.34, 0.34, 0.66, 0.66);
    if (!Explain("60mm 上の作業平面を使える", UseTopPlaneOffsetBy(window, 60.0))) {
        return false;
    }
    const auto high = DrawRectangleAtByHand(window, 0.40, 0.40, 0.60, 0.60);
    if (!Explain("3つの断面を引ける", !middle.IsNil() && !high.IsNil())) {
        return false;
    }
    // 斜めから見る。3つの矩形を別々に押せるように。
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Isometric);
    viewport.FitToDocument();
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("surface.create");
    if (!Explain("ロフトのカードが押せる",
            window.SurfaceDock().ClickMethodCard(GuideSurfaceMethod::LoftSections))) {
        return false;
    }
    // **わざと順不同に押す。**上 → 下 → 中。
    if (!Explain("断面を順不同に押せる", ClickOnCurveOf(window, high)
                && ClickOnCurveOf(window, low) && ClickOnCurveOf(window, middle))) {
        return false;
    }
    const auto& in = window.SurfaceInput();
    if (!Explain((std::string("下見が出る(帯は ") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.SurfacePreviewShown())) {
        return false;
    }
    const auto shown = kachakacha::v2::app::SurfaceSectionOrder(in);
    if (!Explain("自動では押した順ではなく採用した順(下→中→上)が出る",
            shown.size() == 3 && shown[0] == low && shown[1] == middle && shown[2] == high
                && in.sections[0] == high)) {
        return false;
    }
    if (!Explain("棚の断面順も3行", window.SurfaceDock().SectionOrderTexts().size() == 3)) {
        return false;
    }
    // 手動固定へ。表示順がそのまま固定される。
    if (!Explain("手動固定が押せる",
            window.SurfaceDock().ClickOrdering(SurfaceOrdering::ManualLock))) {
        return false;
    }
    if (!Explain("固定した順は表示順のまま",
            in.ordering == SurfaceOrdering::ManualLock && in.explicitOrder == shown)) {
        return false;
    }
    // 3行目(上)を↑で2行目へ。生成順は 下 → 上 → 中 になる。
    if (!Explain("↑が押せる", window.SurfaceDock().ClickMoveRow(2, true))) {
        return false;
    }
    const auto locked = kachakacha::v2::app::SurfaceSectionOrder(in);
    if (!Explain("入れ替えた順が生成順になる",
            locked.size() == 3 && locked[0] == low && locked[1] == high
                && locked[2] == middle)) {
        return false;
    }
    // 下見はその順で作り直され、採用順も同じ(カーネルが並べ替えていない)。
    if (!Explain("固定した順のまま下見が作られる",
            window.SurfacePreviewShown() && in.adoptedOrder == locked)) {
        return false;
    }
    if (!Explain("Enter で確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))) {
        return false;
    }
    if (!Explain("面が1枚できる", CountOfKind(window, EntityKind::GuideSurface) == 1)) {
        return false;
    }
    return Explain("保存される作り方に手動固定が残る", GuideSurfaceLockedInDocument(window));
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
        {"HP-SF-01 面を作る棚と下見が見え、文書はまだ増えない",
            CaseHumanPathSurfacePreviewOnly},
        {"HP-SF-02 Enter で面が1枚できて棚が片付く",
            CaseHumanPathSurfaceConfirmWithEnter},
        {"HP-SF-03 Esc で何も作らずやめる", CaseHumanPathSurfaceCancel},
        {"HP-SF-04 作り方を変えても入れたものが消えない",
            CaseHumanPathSurfaceMethodKeepsInput},
        {"HP-SF-05 5本の別ワイヤーを1輪郭として面と押し出しに使える",
            CaseHumanPathFiveWiresBecomeOneProfile},
        {"PROFILE-03〜05/07 穴・複数・解除・開いた輪郭を画面で扱える",
            CaseHumanPathProfileRegionVariants},
        {"HP-EX-03 立体の面を画面から拾って押す", CaseHumanPathPushAFace},
        {"HP-SF-06 3D のクリックがどの欄へ入るかがいつも見えている",
            CaseHumanPathSurfaceSlotsFollowClicks},
        {"HP-SF-07 ロフトの断面順は採用順を出し、手動固定はそのまま生成順になる",
            CaseHumanPathLoftOrderFollowsAdoption},
        {"HP-UI-02 選んだものが棚と 3D と一番下の行に出ている",
            CaseHumanPathSelectionIsVisible},
        {"HP-FAB-01 面を拾い70%曲げからワイヤーを作る",
            CaseHumanPathSurfaceToFabricationWire},
    };
}

} // namespace kachakacha::v2::selftest
