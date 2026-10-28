//! 近似の人の道(HP-AP)。引継ぎ 2026-09-17 の 3。
//!
//! 近似を押す → 3D で面を押す(もう一度押すと外れる)→ 3つの候補が **実際に作られて**
//! 部材数とずれが並ぶ → 候補を押すと下見が変わる → Enter で確定、1回の取り消しで消える。
//!
//! 基本HP-APは実際に拾う道(`SelectAt`)を通す。追加の出力行列はAPIで入力面を用意し、
//! 実Qtの候補・取消・出力を検査する。大きな手作図入力は別途stress経路で保持する。

#include "V2SelfTest.h"
#include "V2SelfTestDeleteDialog.h"

#include "V2FabricationDock.h"
#include "V2PatternDock.h"
#include "V2PreparedDocument.h"
#include "V2OutputTool.h"
#include "V2ExportDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/ApproxInput.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/domain/Feature.h"

#include <QApplication>
#include <QPointF>
#include <QVariant>
#include <QString>
#include <QPushButton>
#include <QDialog>
#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstddef>
#include <cmath>
#include <string>
#include <variant>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::Shelf;
using kachakacha::v2::domain::EntityKind;

//! 画面に見えている形状ガイドの塗りの真ん中を、素のクリックで押す。
//! 選ばれたかどうかは見ない(押し直して外す道にも使う)。当たり判定があるかだけ確かめる。
[[nodiscard]] bool PressAnyGuideSurface(V2MainWindow& window)
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
        if (!screen.has_value()
            || !viewport.PickShapeAt(QPointF(screen->x, screen->y)).has_value()) {
            continue;
        }
        viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
        return true;
    }
    return false;
}

//! 面の境界と重なるワイヤは、利用者と同じ候補切替(Tab)で区別してクリックする。
[[nodiscard]] bool PressActualWire(V2MainWindow& window)
{
    QApplication::processEvents();
    auto& viewport = window.Viewport();
    const auto curves = window.Session().Scene().curves;
    for (const auto& curve : curves) {
        const auto* entity = window.Session().GetDocument().FindEntity(curve.entityId);
        if (!entity || entity->kind != EntityKind::Wire) continue;
        const auto screen = viewport.Mapping().Project(curve.segment.Evaluate(0.5));
        if (!screen) continue;
        const QPointF point(screen->x, screen->y);
        viewport.HoverAt(point);
        const auto count = viewport.CandidateCount();
        for (std::size_t i = 0; i < count; ++i) {
            const auto candidate = viewport.CurrentCandidate();
            if (candidate && candidate->entityId == curve.entityId
                && candidate->segmentId == curve.segmentId) {
                viewport.SelectAt(point, Qt::NoModifier);
                return true;
            }
            viewport.CycleCandidate(false);
        }
    }
    return false;
}

//! 矩形から平らな面を1枚作り、選択を空にして近似を構えるところまで。
[[nodiscard]] bool ArmApproxOnFreshSurface(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))
        || !Explain("境界を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.RunCommand("surface.create");
    if (!Explain("面の下見が見える", window.SurfacePreviewShown())
        || !Explain("Enterで面を確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("形状ガイドができる", CountOfKind(window, EntityKind::GuideSurface) == 1)) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Isometric);
    viewport.FitToDocument();
    viewport.SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);   // 空所を押して選択を外す
    window.RunCommand("fabrication.create");
    return Explain("近似を押すと製作の棚が構える",
               window.ApproxShelfShown() && window.ShelfShown(Shelf::Fabrication))
        && Explain("構えただけでは何も作らない", window.FabricationModelCount() == 0)
        && Explain("対象の欄は空", window.FabricationDock().SourcesTextJa().isEmpty())
        && Explain("一番下の一行に SOURCES=0",
            window.ToolFooterTextJa().contains(QStringLiteral("SOURCES=0")));
}

//! HP-AP-01。3D で対象を拾うと候補が作られて並び、押し直すと外れる。
[[nodiscard]] bool CaseHumanPathApproxSourcesFollowClicks(V2MainWindow& window)
{
    if (!ArmApproxOnFreshSurface(window)
        || !Explain("構えてから面を画面で拾える", ClickOnAnyGuideSurface(window))) {
        return false;
    }
    auto& dock = window.FabricationDock();
    const std::string sources = dock.SourcesTextJa().toStdString();
    if (!Explain((std::string("対象の欄に名前が出る(") + sources + ")").c_str(),
            !sources.empty())
        || !Explain("3D に SOURCE の札が出る",
            !window.Viewport().ToolRoleLabels().empty()
                && window.Viewport().ToolRoleLabels().front().text.startsWith(
                    QStringLiteral("SOURCE")))) {
        return false;
    }
    // 3つの候補は見積もりではなく、実際に作った結果。部材数とずれが行に出る。
    const auto& outcomes = window.ApproxOutcomes();
    if (!Explain("候補は3つとも作ってある",
            outcomes.size() == 3 && outcomes[0].evaluated && outcomes[1].evaluated
                && outcomes[2].evaluated)) {
        return false;
    }
    for (int candidate = 0; candidate < 3; ++candidate) {
        const std::string line = dock.CandidateTextJa(candidate).toStdString();
        const bool tellsResult = line.find("部材") != std::string::npos
            || line.find("作れません") != std::string::npos
            || line.find("×") != std::string::npos;
        if (!Explain((std::string("候補の行が結果を言う(") + line + ")").c_str(),
                tellsResult)) {
            return false;
        }
    }
    const std::string footer = window.ToolFooterTextJa().toStdString();
    if (!Explain((std::string("一番下の一行に対象と候補が出る(") + footer + ")").c_str(),
            footer.find("SOURCES=1") != std::string::npos
                && footer.find("CANDIDATE=") != std::string::npos
                && footer.find("Preview only") != std::string::npos)
        || !Explain("下見が 3D に出ている", !window.Viewport().ToolPreview().empty())
        || !Explain("下見の間、文書に近似モデルは無い", window.FabricationModelCount() == 0)) {
        return false;
    }
    // もう一度同じ面を押すと外れる。棚も札も一行も空へ戻る。
    if (!Explain("同じ面をもう一度押せる", PressAnyGuideSurface(window))
        || !Explain("押し直すと対象から外れる",
            window.ApproxInput().sources.empty() && dock.SourcesTextJa().isEmpty())
        || !Explain("一行も SOURCES=0 へ戻る",
            window.ToolFooterTextJa().contains(QStringLiteral("SOURCES=0")))
        || !Explain("札も消える", window.Viewport().ToolRoleLabels().empty())) {
        return false;
    }
    // Esc でやめると、文書は始める前とまったく同じ。
    if (!Explain("Escでやめられる", window.HandleToolKey(Qt::Key_Escape, nullptr))) {
        return false;
    }
    return Explain("取消後も近似を維持する", window.ApproxShelfShown())
        && Explain("文書には面が1枚あるだけ",
            CountOfKind(window, EntityKind::GuideSurface) == 1
                && window.FabricationModelCount() == 0)
        && Explain("一行も消える", window.ToolFooterTextJa().isEmpty());
}

//! HP-AP-02。候補を押すと下見が変わり、Enter で下見と同じ作り方が文書へ入る。1回で戻せる。
[[nodiscard]] bool CaseHumanPathApproxCandidateThenConfirm(V2MainWindow& window)
{
    if (!ArmApproxOnFreshSurface(window)
        || !Explain("構えてから面を画面で拾える", ClickOnAnyGuideSurface(window))) {
        return false;
    }
    auto& dock = window.FabricationDock();
    if (!Explain("既定の候補は棚の方式どおり(帯 = B)", dock.SelectedCandidateShown() == 1)) {
        return false;
    }
    // 人が候補 A(面ごとに展開)を押す。棚のボタンで、見えているもの。
    if (!Explain("候補 A のボタンを押せる", dock.ClickCandidate(0))
        || !Explain("押した候補が選ばれて見える", dock.SelectedCandidateShown() == 0)
        || !Explain("一番下の一行も CANDIDATE=A",
            window.ToolFooterTextJa().contains(QStringLiteral("CANDIDATE=A")))) {
        return false;
    }
    const auto& outcomes = window.ApproxOutcomes();
    if (!Explain("平らな面は候補 A で作れる", outcomes.size() == 3 && outcomes[0].available)) {
        return false;
    }
    const std::size_t partsShown = outcomes[0].partCount;
    if (!Explain("Enterで確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("近似モデルが1つできる", window.FabricationModelCount() == 1)
        || !Explain("確定すると構えが解ける", !window.ApproxShelfShown())
        || !Explain("確定した部材数は下見と同じ",
            window.FabricationPanelCount() == partsShown)) {
        return false;
    }
    // 作り方は選んだ候補(面ごと = 方式 0)そのもの。下見と別の作り方で作り直していない。
    bool methodMatches = false;
    for (const auto& feature : window.Session().GetDocument().Snapshot().features) {
        if (const auto* made =
                std::get_if<kachakacha::v2::domain::CreateFabricationModelDefinition>(
                    &feature.definition)) {
            methodMatches = made->method == 0;
        }
    }
    if (!Explain("文書の作り方は押した候補のまま(方式 0)", methodMatches)) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1回の取り消しで近似モデルが消える", window.FabricationModelCount() == 0)
        && Explain("面は残る", CountOfKind(window, EntityKind::GuideSurface) == 1);
}

//! HP-AP-03。作り方のカード(標準/少部品優先/精度優先/手動条件)を押すと、既定の候補が作り方に合う
//! ものへ替わる。候補を人が押した後は、作り方を押し直すまで勝手に替わらない。
[[nodiscard]] bool CaseHumanPathApproxPolicyChoosesCandidate(V2MainWindow& window)
{
    using kachakacha::v2::app::ApproxPolicy;
    using kachakacha::v2::app::CandidateForPolicy;
    if (!ArmApproxOnFreshSurface(window)
        || !Explain("構えてから面を画面で拾える", ClickOnAnyGuideSurface(window))) {
        return false;
    }
    auto& dock = window.FabricationDock();
    const auto labels = dock.PolicyLabels();
    if (!Explain((std::string("作り方は 4 枚(実際 ") + std::to_string(labels.size()) + ")").c_str(),
            labels.size() == 4)
        || !Explain("最初は標準", dock.SelectedPolicyShown() == 0)) {
        return false;
    }
    const auto& outcomes = window.ApproxOutcomes();
    const int method = static_cast<int>(window.FabricationChoice().method);   // 棚の方式
    if (!Explain("「少部品優先」を押せる", dock.ClickPolicy(1))
        || !Explain("押された形が少部品優先", dock.SelectedPolicyShown() == 1)
        || !Explain("既定の候補が少部品優先に合う",
            window.ApproxInput().selectedCandidate
                == CandidateForPolicy(ApproxPolicy::FewerParts, outcomes, method))
        || !Explain("状態行が作り方を言う", window.StatusText().contains(QStringLiteral("少部品優先")))) {
        return false;
    }
    if (!Explain("「精度優先」を押せる", dock.ClickPolicy(2))
        || !Explain("既定の候補が精度優先に合う",
            window.ApproxInput().selectedCandidate
                == CandidateForPolicy(ApproxPolicy::Precision, outcomes, method))) {
        return false;
    }
    // 候補を自分で押したら、それが勝つ。
    if (!Explain("候補 B を押せる", dock.ClickCandidate(1))
        || !Explain("押した候補が選ばれる", window.ApproxInput().selectedCandidate == 1
            && window.ApproxInput().candidateChosenByUser)) {
        return false;
    }
    return Explain("Esc でやめられる", window.HandleToolKey(Qt::Key_Escape, nullptr))
        && Explain("近似モデルは増えていない", window.FabricationModelCount() == 0);
}

//! HP-AP-04。曲げ状態の基準値(0/25/50/75/100)を押すと、組立率が打たれて当たる(同じ道)。
[[nodiscard]] bool CaseHumanPathBendPresetsApplyAssembly(V2MainWindow& window)
{
    auto& dock = window.FabricationDock();
    const auto presets = dock.BendPresets();
    if (!Explain((std::string("基準値は 5 つ(実際 ") + std::to_string(presets.size()) + ")").c_str(),
            presets.size() == 5 && presets[0] == 0 && presets[4] == 100)) {
        return false;
    }
    // 近似モデルが無いときは、押しても文書は変わらない(理由が出る)。押せる形ではある。
    window.RunCommand("file.new");
    window.SetMode(kachakacha::v2::app::UiMode::Fabrication);
    window.RunCommand("fabrication.set_assembly");
    const auto revision = window.Session().GetDocument().Revision();
    if (!Explain("50 を押せる", dock.ClickBendPreset(50))
        || !Explain("組立率の欄が 50 になる", std::abs(dock.AssemblyPercent() - 50.0) < 1.0e-9)
        || !Explain("近似モデルが無ければ文書は変わらない",
            window.Session().GetDocument().Revision() == revision)) {
        return false;
    }
    return Explain("0 を押すと 0", dock.ClickBendPreset(0) && std::abs(dock.AssemblyPercent()) < 1.0e-9);
}

//! いま選んでいるもののうち、その種類のものの数。
[[nodiscard]] int CountSelectedOfKind(V2MainWindow& window, EntityKind kind)
{
    int count = 0;
    for (const auto& id : window.Viewport().Selection().entityIds) {
        const auto* entity = window.Session().GetDocument().FindEntity(id);
        count += entity != nullptr && entity->kind == kind ? 1 : 0;
    }
    return count;
}

//! 構えたまま線を押しても選べない(近似は面と立体しか受けない)ことを確かめてから、
//! 面を消す → 近似が解けて、線を押せば選べて Del で消える。
//! wireFree: その線を使う面が無く、Del で実際に消えることまで見る。面が残っていれば
//! 「使っている」と断る(HP-LF-09)ので、削除の道へ届いたことだけ見る。
[[nodiscard]] bool WiresBecomeSelectableAfterApproxEnds(V2MainWindow& window, const char* how,
    bool wireFree)
{
    if (!Explain((std::string(how) + ": 近似が解けている").c_str(), !window.ApproxShelfShown())
        || !Explain((std::string(how) + ": 一番下の一行から 近似 が消える").c_str(),
            !window.ToolFooterTextJa().contains(QStringLiteral("近似")))
        || !Explain((std::string(how) + ": 線を押すと選べる").c_str(),
            PressActualWire(window))) {
        return false;
    }
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    const auto selectedEdges = window.Viewport().Selection().ordered;
    const auto curvesBefore = window.Session().Scene().curves.size();
    QString reason;
    if (!Explain((std::string(how) + ": 削除が押せる(" + reason.toStdString() + ")").c_str(),
            window.CommandEnabled("edit.delete", &reason))) {
        return false;
    }
    if(wireFree)window.RunCommand("edit.delete");else if(!DeleteWithConfirmation(window,false))return false;
    if (!wireFree) {
        return Explain((std::string(how) + ": 面に使われている線は「使っている」と断られる(削除の道へ届く)").c_str(),
            CountOfKind(window, EntityKind::Wire) == wiresBefore
                && window.StatusText().contains(QStringLiteral("使っている")));
    }
    // 3D のクリックはワイヤ全体ではなく辺を選ぶ。多辺ワイヤは残りの辺を保持する。
    bool removed = !selectedEdges.empty();
    for (const auto& ref : selectedEdges) {
        for (const auto& curve : window.Session().Scene().curves) {
            if (curve.entityId == ref.entityId && curve.segmentId == ref.segmentId) removed = false;
        }
    }
    return Explain((std::string(how) + ": 選んだ辺だけ消え、残りの線は保持される").c_str(),
        removed && window.Session().Scene().curves.size() + selectedEdges.size() == curvesBefore
            && CountOfKind(window, EntityKind::Wire) >= wiresBefore - 1);
}

//! HP-AP-06。オーナー報告(2026-09-26)「面を削除した後などに線を消したり選択したりできない」。
//! 近似を構えて面を対象にしたまま、その面を消す → 近似は解けて線が選べる。
//! 構えたままモードを替えても解ける。構えている間は線を押しても選ばれない(それが元の症状)。
[[nodiscard]] bool CaseHumanPathApproxEndsWhenSourceVanishes(V2MainWindow& window)
{
    if (!ArmApproxOnFreshSurface(window)
        || !Explain("構えてから面を画面で拾える", ClickOnAnyGuideSurface(window))) {
        return false;
    }
    // 構えている間に線を押しても、線は選択に残らない(面と立体しか受けない。対象の面は残る)。
    if (!Explain("重なった面境界から候補切替で元のワイヤを押せる", PressActualWire(window))) return false;
    if (!Explain("構えている間は線を押しても線は選ばれない(面と立体しか受けない)",
            CountSelectedOfKind(window, EntityKind::Wire) == 0 && window.ApproxShelfShown())
        || !Explain((std::string("線を押したら、なぜ受けないかを棚が言う(")
                        + window.FabricationDock().MessageText().toStdString() + ")").c_str(),
            window.FabricationDock().MessageText().contains(QStringLiteral("線は対象にできません")))) {
        return false;
    }
    window.RefreshFabricationDock();
    if(!Explain("対象を拒否した理由は通常の棚更新でも残る",window.FabricationDock().MessageText().contains(QStringLiteral("線は対象にできません"))))return false;
    window.RunCommand("fabrication.preview_update");
    if(!Explain("明示的な再計算で入力拒否の案内を解除する",!window.FabricationDock().MessageText().contains(QStringLiteral("線は対象にできません"))&&window.ApproxShelfShown()))return false;
    // 面を左の一覧などから選んで Del(3D で押すと対象から外れるだけなので、選択を直接入れる)。
    kachakacha::v2::app::SelectionSet one;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::GuideSurface) {
            one.entityIds = {entity.id};
        }
    }
    window.Viewport().SetSelection(one);
    if (!Explain("面を選んでも近似は構えたまま", window.ApproxShelfShown())) {
        return false;
    }
    window.RunCommand("edit.delete");
    if (!Explain("面が消える", CountOfKind(window, EntityKind::GuideSurface) == 0)
        || !WiresBecomeSelectableAfterApproxEnds(window, "面を消した後", true)) {
        return false;
    }
    // 取り消しで面が戻っても、近似は構え直さない(線はそのまま選べる)。
    window.RunCommand("edit.undo");
    window.RunCommand("edit.undo");
    if (!Explain("取り消しで面と線が戻る", CountOfKind(window, EntityKind::GuideSurface) == 1)
        || !Explain("戻っても近似は構えない", !window.ApproxShelfShown())) {
        return false;
    }
    // モードを替えると解ける。
    if (!ArmApproxOnFreshSurface(window)
        || !Explain("構えてから面を画面で拾える(2度目)", ClickOnAnyGuideSurface(window))) {
        return false;
    }
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    return WiresBecomeSelectableAfterApproxEnds(window, "作図へ替えた後", false);
}


// These transitions deliberately avoid the old tests' deselect/reselect reset.
[[nodiscard]] bool CaseQuickSettingsOutsideApprox(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!MakeCurvedGuideSurface(window)) return false;
    window.SetMode(kachakacha::v2::app::UiMode::Fabrication);
    window.RunCommand("fabrication.set_assembly");
    auto& dock = window.FabricationDock();
    dock.FocusCommand("fabrication.create");
    QPushButton* start = nullptr;
    for (auto* button : window.findChildren<QPushButton*>(QStringLiteral("panelConfirm")))
        if (button->text().contains(QStringLiteral("始める"))) { start = button; break; }
    auto* one = window.findChild<QPushButton*>(QStringLiteral("fabricationOnePiece"));
    if (!Explain("approximation start and one-piece controls exist", start && one)) return false;
    for (int pass = 0; pass < 2; ++pass) {
        const auto revision = window.Session().GetDocument().Revision();
        if (pass == 0) one->click(); else dock.SetEqualPartCount(3);
        if (!Explain("idle settings retain an enabled start action", start->isEnabled()
            && start->text().contains(QStringLiteral("始める")) && !window.ApproxShelfShown()
            && window.Session().GetDocument().Revision() == revision)) return false;
        start->click();
        if (!Explain("one click restarts a valid approximation preview", window.ApproxShelfShown()
            && window.ApproxOutcomes().size() == 3 && !window.Viewport().ToolPreview().empty())) return false;
        start->click();
        if (!Explain("preview confirms and releases the tool", !window.ApproxShelfShown()
            && window.FabricationModelCount() == static_cast<std::size_t>(pass + 1))) return false;
    }
    return true;
}

[[nodiscard]] bool CaseCancelledTargetsRemainVisible(V2MainWindow& window)
{
    if (!ArmApproxOnFreshSurface(window) || !ClickOnAnyGuideSurface(window)) return false;
    const auto sources = window.ApproxInput().sources;
    const auto revision = window.Session().GetDocument().Revision();
    if (!window.FabricationDock().ClickCancelApprox()) return false;
    if (!Explain("cancelled retained targets are still named and labelled", window.ApproxShelfShown()
        && window.ApproxInput().sources == sources && !window.FabricationDock().SourcesTextJa().isEmpty()
        && window.Viewport().ToolRoleLabels().size() == sources.size()
        && window.Viewport().ToolPreview().empty()
        && window.Session().GetDocument().Revision() == revision)) return false;
    // A single subsequent click removes this visible target; no hidden target survives.
    if (!PressAnyGuideSurface(window)) return false;
    if (!Explain("one click after cancel toggles exactly the visible target",
        window.ApproxInput().sources.empty() && window.FabricationDock().SourcesTextJa().isEmpty())) return false;
    if (!PressAnyGuideSurface(window)) return false;
    return Explain("next click restores a usable preview", window.ApproxInput().sources == sources
        && !window.Viewport().ToolPreview().empty());
}

[[nodiscard]] bool CasePatternInvalidatesAfterEdit(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!MakeCurvedGuideSurface(window)) return false;
    auto& dock = window.FabricationDock();
    dock.SetEqualPartCount(3);
    window.RunCommand("fabrication.create"); window.RunCommand("fabrication.create");
    if (window.FabricationModelCount() != 1) return false;
    const auto modelSelection = window.Viewport().Selection();
    QTemporaryDir folder;
    if (!folder.isValid()) return false;
    for (int pass = 0; pass < 3; ++pass) {
        window.Viewport().SetSelection(modelSelection);
        window.RunCommand("fabrication.create_pattern");
        if (!Explain("fresh pattern exists before editing", window.PatternDock().PageCount() > 0)) return false;
        if (pass == 0) {
            dock.SetPartNumbersText(QStringLiteral("1")); dock.SetSplitPieces(2);
            window.RunCommand("fabrication.split_part"); dock.HandleKey(Qt::Key_Return);
            if (!Explain("split was committed", window.FabricationPanelCount() == 4)) return false;
        } else if (pass == 1) window.RunCommand("edit.undo");
        else window.RunCommand("file.new");
        if (!Explain("changed document clears both pattern preview and export counts",
            window.PatternDock().PageCount() == 0 && window.PatternDock().CurrentPage() == -1)) return false;
        auto& output = window.ExportDock();
        output.ChooseFormat(kachakacha::v2::app::ExportFormat::Svg);
        const bool chosePattern = output.ChooseTarget(kachakacha::v2::app::ExportTarget::CurrentPattern);
        const auto path = folder.filePath(QStringLiteral("stale-%1.svg").arg(pass));
        output.ChoosePath(path); output.SetOverwrite(true);
        if (!Explain("stale pattern cannot be exported", (!chosePattern || !output.RunNow()) && output.Counts().patternPages == 0 && !QFile::exists(path))) return false;
    }
    return true;
}


[[nodiscard]] bool PreparePatternAudit(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!MakeCurvedGuideSurface(window)) return false;
    window.FabricationDock().SetEqualPartCount(3);
    window.RunCommand("fabrication.create"); window.RunCommand("fabrication.create");
    if (window.FabricationModelCount() != 1) return false;
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::FabricationModel));
    window.RunCommand("fabrication.create_pattern");
    return window.PatternDock().PageCount() > 0;
}

[[nodiscard]] bool CaseCancelEnterRestarts(V2MainWindow& window)
{
    if (!ArmApproxOnFreshSurface(window) || !ClickOnAnyGuideSurface(window)) return false;
    if (!window.HandleToolKey(Qt::Key_Escape, nullptr)
        || !window.HandleToolKey(Qt::Key_Return, nullptr)) return false;
    if (!Explain("Enter after cancel recomputes without committing", window.FabricationModelCount() == 0
        && !window.Viewport().ToolPreview().empty() && window.ApproxOutcomes().size() == 3)) return false;
    if (!window.HandleToolKey(Qt::Key_Return, nullptr)) return false;
    return Explain("second Enter confirms a previewed approximation", window.FabricationModelCount() == 1);
}

[[nodiscard]] bool CasePatternOpenLifecycle(V2MainWindow& window)
{
    if (!PreparePatternAudit(window)) return false;
    QTemporaryDir folder;
    if (!folder.isValid()) return false;
    const auto saved = folder.filePath(QStringLiteral("same-document.kcd2"));
    window.SetPathChooser([saved](bool) { return saved; });
    window.RunCommand("file.save_as");
    if (!QFile::exists(saved)) return false;
    const int pages = window.PatternDock().PageCount();
    window.SetPathChooser([](bool) { return QString(); }); window.RunCommand("file.open");
    if (!Explain("cancelled Open preserves the current pattern", window.PatternDock().PageCount() == pages)) return false;
    if (!Explain("failed Open preserves the current pattern", !window.OpenDocumentFile(folder.filePath("missing.kcd2"))
        && window.PatternDock().PageCount() == pages)) return false;
    if (!window.OpenDocumentFile(saved)) return false;
    return Explain("successful Open clears old pages even for the same saved document",
        window.PatternDock().PageCount() == 0 && window.ExportDock().Counts().patternPages == 0);
}

[[nodiscard]] bool CasePatternSurvivesOutputCancel(V2MainWindow& window)
{
    if (!PreparePatternAudit(window)) return false;
    const int pages = window.PatternDock().PageCount();
    auto& dock = window.FabricationDock();
    dock.SetPartNumbersText(QStringLiteral("すべて"));
    dock.TypeAssemblyPercent(37); dock.PressApplyAssembly();
    if (!Explain("assembly pose leaves the cutting pattern valid", window.PatternDock().PageCount() == pages)) return false;
    dock.SetShowSource(false); dock.SetShowSource(true);
    window.RunCommand("view.hide_selected"); window.RunCommand("view.show_all");
    if (!Explain("entity visibility changes keep the pattern valid", window.PatternDock().PageCount() == pages)) return false;
    const auto revision = window.Session().GetDocument().Revision();
    auto* output = V2OutputTool::Open(window, 2);
    if (!Explain("model output captures current geometry", output && output->AssetCount() > 0)) return false;
    output->close();
    V2OutputTool::Generate(window, "fabrication.freeze_state");
    bool captured = false;
    for (auto* dialog : window.findChildren<QDialog*>()) {
        if (dialog->isVisible() && dialog->objectName() == "selectionOutput") {
            auto* generated = dynamic_cast<V2OutputTool*>(dialog);
            captured = generated && generated->AssetCount() > 0;
            dialog->close();
        }
    }
    if (!Explain("generation cancellation preserves geometry and the old valid pattern", captured
        && window.Session().GetDocument().Revision() == revision
        && window.PatternDock().PageCount() == pages)) return false;
    QTemporaryDir folder;
    if (!folder.isValid()) return false;
    auto& exportDock = window.ExportDock();
    if (!exportDock.ChooseTarget(kachakacha::v2::app::ExportTarget::CurrentPattern)) return false;
    for (const auto format : {kachakacha::v2::app::ExportFormat::Svg,
        kachakacha::v2::app::ExportFormat::Dxf, kachakacha::v2::app::ExportFormat::Pdf}) {
        if (!exportDock.ChooseFormat(format)) return false;
        const auto path = folder.filePath(QStringLiteral("pattern%1").arg(static_cast<int>(format))
            + QString::fromUtf8(kachakacha::v2::app::ExportFormatExtension(format).data()));
        exportDock.ChoosePath(path); exportDock.SetOverwrite(true);
        if (!Explain("unchanged pattern still exports every 2D format", exportDock.RunNow() && QFile(path).size() > 100)) return false;
    }
    return true;
}


[[nodiscard]] bool MakeLegacyOutputFixture(V2MainWindow& window)
{
    if (qEnvironmentVariableIsSet("KACHACAD_APPROX_UI_AUDIT_STRESS")) return MakeCurvedGuideSurface(window);
    // A compact native curved sheet keeps the format matrix inexpensive. The
    // separate stress route retains the large UI-drawn 270-degree source.
    kachakacha::v2::app::SelectionSet selected;
    for (int row = 0; row < 2; ++row) {
        const auto arc = kachakacha::v2::geometry::CurveSegment::MakeCircularArc(
            {0,0,row*10.0}, {0,0,1}, {1,0,0}, 10.0+row*.01, 0, 1.5707963267948966);
        if (!arc.HasValue()) return false;
        const auto wire = window.Session().AddWire({arc.Value()}, false, "legacy native output source");
        if (!wire.committed || wire.createdEntityIds.size() != 1) return false;
        selected.entityIds.push_back(wire.createdEntityIds.front());
    }
    window.Viewport().SetSelection(selected);
    window.CreateGuideSurfaceFromSelection();
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(window.Session().GetDocument().Snapshot(), EntityKind::GuideSurface));
    return CountOfKind(window, EntityKind::GuideSurface) == 1;
}

[[nodiscard]] bool CaseLegacyOnePieceAllOutputs(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!MakeLegacyOutputFixture(window)) return false;
    auto& dock = window.FabricationDock();
    dock.SetEqualPartCount(1); dock.SetSplitAxisIndex(2); dock.SetSplitAtCorners(false);
    window.RunCommand("fabrication.create");
    if (!dock.ClickCandidate(2)) return false;
    window.RunCommand("fabrication.create");
    if (!Explain("legacy untrimmed auto one-piece remains available", window.FabricationModelCount() == 1
        && window.FabricationPanelCount() == 1)) return false;
    const auto models = kachakacha::v2::app::SelectAllOfKind(window.Session().GetDocument().Snapshot(), EntityKind::FabricationModel);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    QString directory = temporary.path();
    const auto evidence = qEnvironmentVariable("KACHACAD_APPROX_UI_AUDIT_DIR");
    if (!evidence.isEmpty()) { QDir().mkpath(evidence); directory = evidence; }
    const auto input = directory + QStringLiteral("/legacy-auto-one-piece.kcd2");
    window.SetPathChooser([input](bool) { return input; }); window.RunCommand("file.save_as");
    if (!window.OpenDocumentFile(input) || !window.RebuildProblems().isEmpty()) return false;
    auto* kind = window.findChild<QComboBox*>(QStringLiteral("fabricationGenerationOutput"));
    if (!kind) return false;
    for (int percent : {0, 37, 100}) {
        window.Viewport().SetSelection(models);
        dock.SetPartNumbersText(QStringLiteral("1")); dock.TypeAssemblyPercent(percent); dock.PressApplyAssembly();
        for (int format : {0, 3, 1, 2}) {
            window.Viewport().SetSelection(models); kind->setCurrentIndex(format);
            const auto before = window.Session().GetDocument().Snapshot();
            const int wires = CountOfKind(window, EntityKind::Wire), parts = CountOfKind(window, EntityKind::Part);
            const int guides = CountOfKind(window, EntityKind::GuideSurface);
            window.RunCommand("fabrication.freeze_state");
            Note(QStringLiteral("legacy percent=%1 output=%2: %3").arg(percent).arg(format).arg(window.StatusText()).toUtf8().constData());
            const bool hasWires = format != 1, hasPart = format == 1 || format == 2;
            if (!Explain("all legacy wire face and part outputs generate", CountOfKind(window, EntityKind::Wire) > wires || !hasWires)) return false;
            if (!Explain("legacy output creates its requested native shape", CountOfKind(window, EntityKind::Part) == parts + (hasPart ? 1 : 0)
                && CountOfKind(window, EntityKind::GuideSurface) == guides + (format == 0 ? 0 : 1))) return false;
            if (format != 0) {
                kachakacha::v2::app::SelectionSet generated;
                for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
                    if (entity.kind != (hasPart ? EntityKind::Part : EntityKind::GuideSurface)) continue;
                    if (std::none_of(before.entities.begin(), before.entities.end(), [&](const auto& old) { return old.id == entity.id; }))
                        generated.entityIds.push_back(entity.id);
                }
                window.Viewport().SetSelection(generated);
                auto* output = V2OutputTool::Open(window, 1);
                if (!output || output->AssetCount() != 1) return false;
                for (int exportFormat : {1, 0, 2}) {
                    output->ChooseFormat(exportFormat);
                    const auto path = directory + QStringLiteral("/legacy-%1-%2-%3").arg(percent).arg(format).arg(exportFormat)
                        + (exportFormat == 1 ? QStringLiteral(".step") : exportFormat == 0 ? QStringLiteral(".stl") : QStringLiteral(".kcd2"));
                    if (!Explain("legacy native output exports STEP STL and independent KCD2", output->SaveTo(path) && QFile(path).size() > 100)) return false;
                }
                output->close();
            }
            window.RunCommand("edit.undo");
            if (!Explain("one undo removes only that generated output", CountOfKind(window, EntityKind::Wire) == wires
                && CountOfKind(window, EntityKind::Part) == parts && CountOfKind(window, EntityKind::GuideSurface) == guides)) return false;
        }
    }
    return true;
}


[[nodiscard]] bool CasePreparedMaterialRoundTrip(V2MainWindow&)
{
    QTemporaryDir folder;
    if (!folder.isValid()) return false;
    V2PreparedDocument payload;
    auto& expected = payload.fabrication["material"];
    kachakacha::v2::app::FabricationEvaluation::MaterialPanel material;
    material.panelIndex = 4; material.bandIndex = 7;
    material.axis = kachakacha::v2::fabrication::BandSplitAxis::V;
    material.low = .2; material.high = .8; material.longitudinal = {0, .3, 1};
    material.loopsUv = {{{0,0},{1,0},{1,1},{0,1}},{{.2,.2},{.2,.4},{.4,.4},{.4,.2}}};
    expected.materialPanels = {material}; expected.materialBands = {false,true,false};
    expected.bandPanels = {{0},{1,2},{}}; expected.bandPanelCount = 3;
    const auto path = folder.filePath("prepared.bin");
    if (!WritePreparedDocument(path, "round-trip", payload)) return false;
    const auto read = ReadPreparedDocument(path, "round-trip", [] { return true; });
    if (!read.HasValue() || !read.Value()->fabrication.contains("material")) return false;
    const auto& actual = read.Value()->fabrication.at("material");
    if (!Explain("worker transfer retains material flags panel grouping and count", actual.materialBands == expected.materialBands
        && actual.bandPanels == expected.bandPanels && actual.bandPanelCount == expected.bandPanelCount
        && actual.materialPanels.size() == 1)) return false;
    const auto& restored = actual.materialPanels.front();
    bool sameLoops = restored.loopsUv.size() == material.loopsUv.size();
    for (std::size_t loop = 0; sameLoops && loop < material.loopsUv.size(); ++loop) {
        sameLoops = restored.loopsUv[loop].size() == material.loopsUv[loop].size();
        for (std::size_t point = 0; sameLoops && point < material.loopsUv[loop].size(); ++point)
            sameLoops = restored.loopsUv[loop][point].u == material.loopsUv[loop][point].u
                && restored.loopsUv[loop][point].v == material.loopsUv[loop][point].v;
    }
    if (!Explain("worker transfer retains exact holes UV axis and interval mapping", restored.panelIndex == material.panelIndex
        && restored.bandIndex == material.bandIndex && restored.axis == material.axis && restored.low == material.low
        && restored.high == material.high && restored.longitudinal == material.longitudinal && sameLoops)) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto original = file.readAll(); file.close();
    auto oldSchema = original;
    QDataStream oldStream(&oldSchema, QIODevice::ReadWrite);
    oldStream.skipRawData(8 + static_cast<int>(std::string("kachakacha-prepared-document").size()));
    oldStream << qint64(2);
    if (!file.open(QIODevice::WriteOnly) || file.write(oldSchema) != oldSchema.size()) return false;
    file.close();
    if (!Explain("old transfer schemas are rejected instead of zero-filling new fields",
        !ReadPreparedDocument(path, "round-trip", [] { return true; }).HasValue()
        && !CopyPreparedDocument(path, "round-trip", folder.filePath("old-copy.bin"), "copy"))) return false;
    QByteArray marker;
    QDataStream markerStream(&marker, QIODevice::WriteOnly);
    markerStream << quint64(3) << qint64(0) << qint64(1) << qint64(0);
    auto badFlags = original;
    const auto at = badFlags.indexOf(marker);
    if (at < 0 || badFlags.indexOf(marker, at + 1) >= 0) return false;
    QDataStream flagStream(&badFlags, QIODevice::ReadWrite);
    flagStream.skipRawData(at + 8); flagStream << qint64(2);
    if (!file.open(QIODevice::WriteOnly) || file.write(badFlags) != badFlags.size()) return false;
    file.close();
    return Explain("invalid material flag is rejected without adopting a partial result",
        !ReadPreparedDocument(path, "round-trip", [] { return true; }).HasValue());
}

} // namespace

std::vector<SelfTestCase> HumanPathApproxCases()
{
    return {
        {"HP-AP-AUDIT worker round trip retains all material fields", CasePreparedMaterialRoundTrip},
        {"HP-AP-AUDIT legacy auto one-piece all poses and output formats", CaseLegacyOnePieceAllOutputs},
        {"HP-AP-AUDIT Escape Enter restarts preview before confirming", CaseCancelEnterRestarts},
        {"HP-AP-AUDIT Open success failure and cancellation preserve pattern ownership", CasePatternOpenLifecycle},
        {"HP-AP-AUDIT generation cancellation preserves pattern and all 2D exports", CasePatternSurvivesOutputCancel},
        {"HP-AP-AUDIT idle and post-confirm settings keep start usable", CaseQuickSettingsOutsideApprox},
        {"HP-AP-AUDIT cancelled targets stay visible and toggle once", CaseCancelledTargetsRemainVisible},
        {"HP-AP-AUDIT partition undo and new document reject stale patterns", CasePatternInvalidatesAfterEdit},
        {"HP-AP-01 近似は 3D で押した対象が候補になり、押し直すと外れる",
            CaseHumanPathApproxSourcesFollowClicks},
        {"HP-AP-02 候補を押すと下見が変わり、Enter で同じ作り方が文書へ入る",
            CaseHumanPathApproxCandidateThenConfirm},
        {"HP-AP-03 作り方のカードが既定の候補を決め、押した候補が勝つ",
            CaseHumanPathApproxPolicyChoosesCandidate},
        {"HP-AP-04 曲げ状態の基準値は組立率を打って当てる道を通る",
            CaseHumanPathBendPresetsApplyAssembly},
        {"HP-AP-06 対象の面が消えるかモードを替えると近似は解け、線が選べて消せる",
            CaseHumanPathApproxEndsWhenSourceVanishes},
    };
}

} // namespace kachakacha::v2::selftest
