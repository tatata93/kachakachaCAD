//! 「足す・引く」を道具から始める(引継ぎ 2026-09-17 の 4)。
//!
//! 足す/引くを押す → 土台待ち → 3D で部品を押すと土台に入り、自動で相手待ちへ →
//! 相手を押す → **実際に足し引きして** 出来上がりの稜線を下見 → Enter で確定。
//! 確定は下見に使った形をそのまま文書へ入れる。土台と相手を隠すのも含めて
//! 1つのまとまり(1回で戻せる)。
//!
//! 土台・相手は 3D の札(TARGET / TOOL)と右の棚の欄に別々に出て、
//! 「ここへ選ぶ」で選び直せ、「解除」で空にでき、3D で押し直すと外れる。
//! 何がどの欄に入るかは core(app/BooleanInputState)が決める。

#include "V2MainWindow.h"

#include "V2BooleanDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/BooleanInputState.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ToolRoleLabels.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/kernel/OcctBoolean.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/modeling/ToolController.h"

#include "V2CornerDock.h"

#include <QString>

#include <algorithm>
#include <string>
#include <vector>

using kachakacha::v2::app::BooleanSlot;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;

namespace {

[[nodiscard]] std::vector<EntityId> Missing(const std::vector<EntityId>& left,
    const std::vector<EntityId>& right)
{
    std::vector<EntityId> out;
    for (const EntityId& id : left) {
        if (std::find(right.begin(), right.end(), id) == right.end()) {
            out.push_back(id);
        }
    }
    return out;
}

} // namespace

//! 道具に結びついた命令のうち、棚を構えてから相手を選ぶもの。
//! ここで引き受けたら真。構えて待つ道(ArmCommand)は通さない。
bool V2MainWindow::BeginToolFirstCommand(std::string_view id)
{
    if (id == "surface.create" && !surfaceShelfShown_) {
        ClearPendingCommand();
        RunGuideCommand(id);
        return true;
    }
    // 近似も道具から始める。何も選んでいなくても棚が出て、3D で対象を押せる。
    // 構えている間の2度目は確定。
    if (id == "fabrication.create") {
        ClearPendingCommand();
        RunFabricationCreate();
        return true;
    }
    if (id == "part.boolean_add" || id == "part.boolean_cut") {
        ClearPendingCommand();
        RunBooleanTool(id == "part.boolean_cut");
        return true;
    }
    // 厚みも道具から始める(指示書 matrix P-10)。何も選んでいなくても棚が出て、
    // 3D で形状ガイドの面を押せる。選んでから押した道もそのまま通す(欄が先に埋まるだけ)。
    if (id == "part.thicken") {
        ClearPendingCommand();
        RunThickenTool();
        return true;
    }
    // 回転体。面を作るの道具を 作り方 = 回転体 で構える(断面 → 軸 → 下見 → Enter)。
    if (id == "guide.revolve") {
        ClearPendingCommand();
        RunRevolveTool();
        return true;
    }
    // C面取り / R丸め。線が2本そろっていなければ、面取りの道具を持って拾うのを待つ。
    // そろっていれば従来どおり作る(下見の Enter もここへ来る)。
    if ((id == "wire.chamfer" || id == "wire.fillet") && !CornerPairSelected(nullptr)) {
        ClearPendingCommand();
        if (cornerDock_ != nullptr) {
            cornerDock_->SetFillet(id == "wire.fillet");
        }
        SelectTool(kachakacha::v2::modeling::DrawingTool::ChamferOrFilletPair);
        SetStatus(QStringLiteral("%1: 線を2本、直す順に 3D で押してください(1本目が A、2本目が B)。"
                                 "2本そろうと下見が出ます。Enter で確定、Esc でやめます。")
                .arg(id == "wire.fillet" ? QStringLiteral("R丸め") : QStringLiteral("C面取り")));
        return true;
    }
    return false;
}

//! 「足す」「引く」を押した。構えていなければ構え、構えていれば操作を切り替える。
void V2MainWindow::RunBooleanTool(bool cut)
{
    if (booleanShelfShown_) {
        ChooseBooleanOperation(cut);
        return;
    }
    // 選んであった部品は、選んだ順に土台・相手へ入れる(選んでから押す道も残す)。
    booleanInput_ = kachakacha::v2::app::BooleanInputState{};
    booleanInput_.cut = cut;
    const auto& document = session_->GetDocument();
    for (const EntityId& id : viewport_->Selection().entityIds) {
        const auto* entity = document.FindEntity(id);
        if (entity != nullptr && entity->kind == EntityKind::Part) {
            booleanInput_ = kachakacha::v2::app::WithBooleanPick(booleanInput_, id);
        }
    }
    booleanShelfShown_ = true;
    RefreshRightShelves();
    viewport_->SetToolPickActive(true);
    viewport_->SetToolPickToggle(true);
    MirrorBooleanToSelection();
    RefreshBooleanAll();
    SetStatus(QStringLiteral("%1: 部品を2つ(土台と相手)、3D で順に押してください。"
                             "押し直すと外れます。Enter で確定、Esc でやめます。\n%2")
            .arg(QString::fromUtf8(
                     std::string(kachakacha::v2::app::BooleanOperationLabelJa(cut)).c_str()),
                QString::fromStdString(kachakacha::v2::app::BooleanHintJa(booleanInput_))));
}

//! 3D の選択の印を、土台・相手に合わせる。欄が正本。
void V2MainWindow::MirrorBooleanToSelection()
{
    booleanMirror_ = kachakacha::v2::app::BooleanEntries(booleanInput_);
    kachakacha::v2::app::SelectionSet mirrored;
    mirrored.entityIds = booleanMirror_;
    for (const EntityId& id : booleanMirror_) {
        kachakacha::v2::app::SelectionRef ref;
        ref.entityId = id;
        mirrored.ordered.push_back(ref);
    }
    booleanMirroring_ = true;
    viewport_->SetSelection(std::move(mirrored));
    booleanMirroring_ = false;
}

//! 3D の選択が変わった。足された部品は次の欄へ、外れた部品は欄からも外す。
void V2MainWindow::RefreshBooleanForSelectionChange()
{
    if (!booleanShelfShown_ || booleanMirroring_ || viewport_ == nullptr) {
        return;
    }
    const auto& now = viewport_->Selection().entityIds;
    const auto added = Missing(now, booleanMirror_);
    const auto removed = Missing(booleanMirror_, now);
    const auto& document = session_->GetDocument();
    bool changed = false;
    for (const EntityId& id : added) {
        const auto* entity = document.FindEntity(id);
        if (entity != nullptr && entity->kind == EntityKind::Part) {
            booleanInput_ = kachakacha::v2::app::WithBooleanPick(booleanInput_, id);
            changed = true;
        }
    }
    if (!removed.empty()) {
        booleanInput_ = kachakacha::v2::app::WithoutBooleanEntries(booleanInput_, removed);
        changed = true;
    }
    MirrorBooleanToSelection();   // 受けなかったもの(線など)を選択に残さない。
    if (changed) {
        RefreshBooleanAll();
    }
}

//! 土台と相手がそろっていれば **実際に足し引きして** 稜線を下見に出す。文書へは書かない。
void V2MainWindow::RefreshBooleanPreview()
{
    booleanOutcome_ = kachakacha::v2::app::BooleanPreviewOutcome{};
    booleanBuilt_.reset();
    viewport_->HideToolPreview();
    if (!kachakacha::v2::app::BooleanReady(booleanInput_)) {
        return;
    }
    booleanOutcome_.evaluated = true;
    const auto base = partShapes_.find(booleanInput_.target.ToString());
    const auto other = partShapes_.find(booleanInput_.tool.ToString());
    if (base == partShapes_.end() || other == partShapes_.end()) {
        booleanOutcome_.refusalJa = "選んだ部品の立体がまだありません";
        return;
    }
    const double tolerance =
        session_->GetDocument().Snapshot().settings.tolerance.interactiveJoinMm;
    const auto built = kachakacha::v2::kernel::BuildBoolean(
        booleanInput_.cut ? kachakacha::v2::kernel::BooleanOperation::Difference
                          : kachakacha::v2::kernel::BooleanOperation::Union,
        base->second, other->second, tolerance);
    if (!built.HasValue()) {
        booleanOutcome_.refusalJa = built.FirstSummaryJa();
        return;
    }
    booleanOutcome_.available = true;
    booleanOutcome_.previousVolumeMm3 = built.Value().previousVolumeMm3;
    booleanOutcome_.volumeMm3 = built.Value().volumeMm3;
    booleanBuilt_ = built.Value().handle;
    // 出来上がりの稜線。画面に出すのは、確定で文書へ入るその形。
    const auto mesh = kachakacha::v2::kernel::BuildShapeMesh(built.Value().handle);
    if (mesh.HasValue()) {
        viewport_->ShowToolPreview(mesh.Value().edges);
    }
}

//! 棚・札・一番下の一行を、いまの入力に合わせる。
void V2MainWindow::RefreshBooleanDock()
{
    if (booleanDock_ == nullptr) {
        return;
    }
    const auto& document = session_->GetDocument();
    const auto nameOf = [&document](const EntityId& id) {
        const auto* entity = id.IsNil() ? nullptr : document.FindEntity(id);
        return entity != nullptr && !entity->displayName.empty() ? entity->displayName
                                                                 : std::string("名前のないもの");
    };
    const bool previewShown = !viewport_->ToolPreview().empty();
    std::vector<QString> lines;
    for (const std::string& line : kachakacha::v2::app::BooleanStatusLinesJa(booleanInput_,
             booleanOutcome_, previewShown)) {
        lines.push_back(QString::fromStdString(line));
    }
    booleanDock_->ShowInput(booleanInput_, QString::fromStdString(nameOf(booleanInput_.target)),
        QString::fromStdString(nameOf(booleanInput_.tool)), lines,
        booleanBuilt_.has_value());
    ShowToolFooter(booleanShelfShown_
            ? QString::fromStdString(kachakacha::v2::app::BooleanFooterLine(booleanInput_,
                  nameOf(booleanInput_.target), nameOf(booleanInput_.tool), booleanOutcome_,
                  previewShown))
            : QString());
    std::vector<kachakacha::v2::app::ToolRoleLabel> labels;
    if (!booleanInput_.target.IsNil()) {
        labels.push_back({booleanInput_.target,
            std::string(kachakacha::v2::app::BooleanSlotKey(BooleanSlot::Target))});
    }
    if (!booleanInput_.tool.IsNil()) {
        labels.push_back({booleanInput_.tool,
            std::string(kachakacha::v2::app::BooleanSlotKey(BooleanSlot::Tool))});
    }
    ShowRoleLabels(labels);
}

void V2MainWindow::RefreshBooleanAll()
{
    RefreshBooleanPreview();
    RefreshBooleanDock();
}

//! 「ここへ選ぶ」。次の 3D クリックがその欄へ入る。
void V2MainWindow::ActivateBooleanSlot(BooleanSlot slot)
{
    booleanInput_ = kachakacha::v2::app::WithActiveBooleanSlot(booleanInput_, slot);
    RefreshBooleanDock();
    SetStatus(QStringLiteral("%1: 次のクリックは「%2」へ入ります。")
            .arg(QString::fromUtf8(std::string(kachakacha::v2::app::BooleanOperationLabelJa(
                                                   booleanInput_.cut))
                                       .c_str()),
                QString::fromUtf8(
                    std::string(kachakacha::v2::app::BooleanSlotNameJa(slot)).c_str())));
}

//! 「解除」。欄を空にし、次のクリックをその欄へ。
void V2MainWindow::ClearBooleanSlot(BooleanSlot slot)
{
    booleanInput_ = kachakacha::v2::app::WithBooleanSlotCleared(booleanInput_, slot);
    MirrorBooleanToSelection();
    RefreshBooleanAll();
}

//! 足す ⇄ 引く。入力はそのまま、下見だけ作り直す。
void V2MainWindow::ChooseBooleanOperation(bool cut)
{
    booleanInput_.cut = cut;
    RefreshBooleanAll();
    SetStatus(QStringLiteral("%1: 部品を2つ(土台と相手)、3D で順に押してください。\n%2")
            .arg(QString::fromUtf8(
                     std::string(kachakacha::v2::app::BooleanOperationLabelJa(cut)).c_str()),
                QString::fromStdString(kachakacha::v2::app::BooleanHintJa(booleanInput_))));
}

//! やめる。文書は始める前とまったく同じ。
void V2MainWindow::EndBoolean()
{
    booleanShelfShown_ = false;
    booleanBuilt_.reset();
    booleanOutcome_ = kachakacha::v2::app::BooleanPreviewOutcome{};
    booleanMirror_.clear();
    if (viewport_ != nullptr) {
        viewport_->HideToolPreview();
        viewport_->HideToolRoleLabels();
        viewport_->SetToolPickActive(false);
        viewport_->SetToolPickToggle(false);
    }
    ShowToolFooter(QString());
    RefreshRightShelves();
}

//! 確定。**下見に使った形をそのまま**文書へ入れる。土台と相手を隠すのも含めて1回で戻せる。
void V2MainWindow::ConfirmBoolean()
{
    if (!booleanBuilt_.has_value()) {
        SetStatus(QStringLiteral("%1: まだ作れません。土台と相手を入れてください。")
                .arg(QString::fromUtf8(std::string(kachakacha::v2::app::BooleanOperationLabelJa(
                                                       booleanInput_.cut))
                                           .c_str())));
        RefreshBooleanDock();
        return;
    }
    const bool cut = booleanInput_.cut;
    const char* label = cut ? "引く" : "足す";
    kachakacha::v2::domain::BooleanDefinition definition;
    definition.mode = cut ? 1 : 0;
    definition.targets.push_back(booleanInput_.target);
    definition.tools.push_back(booleanInput_.tool);
    const std::vector<EntityId> used{booleanInput_.target, booleanInput_.tool};
    const auto outcome = booleanOutcome_;
    const auto handle = *booleanBuilt_;
    kachakacha::v2::document::Document::Transaction transaction(session_->GetDocument(), label);
    // 出来た形の辺は、いまは持たない。持てるようになるまで、元の辺を使い回さない。
    const auto madeId = AddPartFeature(kachakacha::v2::domain::FeatureType::Boolean,
        std::move(definition), handle, {}, label, used);
    if (madeId.IsNil()) {
        return;   // まとまりは戻る(Transaction が捨てる)。
    }
    // 使い切った2つは隠す。消すと、作り方をたどれなくなる。
    const auto hidden = session_->GetDocument().Run(kachakacha::v2::document::SetVisibilityCommand(
        used, kachakacha::v2::domain::Visibility::Hidden));
    if (!hidden.committed) {
        ReportDiagnostics(hidden.diagnostics);
        return;
    }
    if (!transaction.Commit()) {
        return;
    }
    EndBoolean();
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("%1: 体積が %2 mm3 から %3 mm3 になりました。")
            .arg(QString::fromUtf8(label))
            .arg(outcome.previousVolumeMm3, 0, 'f', 4)
            .arg(outcome.volumeMm3, 0, 'f', 4));
}
