//! 「厚み」を道具から始める(指示書 matrix P-10)。
//!
//! 厚みを押す → 面待ち → 3D で形状ガイドの面を押すと入る(押し直すと外れる) →
//! 作り方(外側/中央/内側/平面まで)を選ぶ → **実際に厚みを付けて** 出来上がりの稜線を
//! 下見 → Enter で確定。確定は下見に使った形をそのまま文書へ入れる
//! (V2BooleanCommands.cpp と同じ役目分け)。
//!
//! 面は 3D の札(SURFACE)と右の棚の欄に出て、「選び直す」で外せ、
//! 3D で押し直しても外れる。何が欄に入るかは core(app/ThickenInputState)が決める。

#include "V2MainWindow.h"

#include "V2ThickenDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/CommandParameters.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ThickenInputState.h"
#include "kachakacha/app/ToolRoleLabels.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/kernel/OcctThicken.h"
#include "kachakacha/kernel/OcctTessellate.h"

#include <QString>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::fabrication::ThicknessPlacement;

//! 「厚み」を押す。構えていなければ構え、構えていれば何もしない
//! (足す・引くと違い、操作の切り替えが無いので2度目に意味が無い)。
void V2MainWindow::RunThickenTool()
{
    if (thickenShelfShown_) {
        // 構えている間の2度目は確定(回転体・足す引くと同じ文法)。
        ConfirmThicken();
        return;
    }
    // 選んであった形状ガイドの面は、そのまま欄へ入れる(選んでから押す道も残す)。
    thickenInput_ = kachakacha::v2::app::ThickenInputState{};
    thickenInput_.placement = thicknessPlacement_;
    thickenInput_.thicknessMm = ExtrudeDistanceMm();
    const auto& document = session_->GetDocument();
    for (const EntityId& id : viewport_->Selection().entityIds) {
        const auto* entity = document.FindEntity(id);
        if (entity != nullptr && entity->kind == EntityKind::GuideSurface) {
            thickenInput_ = kachakacha::v2::app::WithThickenPick(thickenInput_, id);
            break;   // 面は1つだけ。
        }
    }
    thickenShelfShown_ = true;
    RefreshRightShelves();
    viewport_->SetToolPickActive(true);
    viewport_->SetToolPickToggle(true);
    MirrorThickenToSelection();
    thickenDock_->SetTargets(ExtrudeTargets());
    RefreshThickenAll();
    SetStatus(QStringLiteral("厚み: 3D で形状ガイドの面を押してください(押し直すと外れます)。"
                             "作り方を選んで、Enter で確定、Esc でやめます。"));
}

//! 3D の選択の印を、面の欄に合わせる。欄が正本。
void V2MainWindow::MirrorThickenToSelection()
{
    thickenMirror_ = thickenInput_.surface;
    kachakacha::v2::app::SelectionSet mirrored;
    if (!thickenMirror_.IsNil()) {
        mirrored.entityIds.push_back(thickenMirror_);
        kachakacha::v2::app::SelectionRef ref;
        ref.entityId = thickenMirror_;
        mirrored.ordered.push_back(ref);
    }
    thickenMirroring_ = true;
    viewport_->SetSelection(std::move(mirrored));
    thickenMirroring_ = false;
}

//! 3D の選択が変わった。形状ガイドの面が足されれば欄へ、外れれば欄からも外す。
void V2MainWindow::RefreshThickenForSelectionChange()
{
    if (!thickenShelfShown_ || thickenMirroring_ || viewport_ == nullptr) {
        return;
    }
    const auto& now = viewport_->Selection().entityIds;
    bool changed = false;
    const bool stillThere = !thickenMirror_.IsNil()
        && std::find(now.begin(), now.end(), thickenMirror_) != now.end();
    if (!thickenMirror_.IsNil() && !stillThere) {
        thickenInput_ = kachakacha::v2::app::WithoutThickenSurface(thickenInput_, thickenMirror_);
        changed = true;
    }
    const auto& document = session_->GetDocument();
    for (const EntityId& id : now) {
        if (!thickenMirror_.IsNil() && id == thickenMirror_) {
            continue;   // すでに映しているもの。
        }
        const auto* entity = document.FindEntity(id);
        if (entity != nullptr && entity->kind == EntityKind::GuideSurface) {
            thickenInput_ = kachakacha::v2::app::WithThickenPick(thickenInput_, id);
            changed = true;
            break;   // 面は1つだけ。
        }
    }
    MirrorThickenToSelection();   // 受けなかったもの(部品・線など)を選択に残さない。
    if (changed) {
        RefreshThickenAll();
    }
}

//! 面と作り方がそろっていれば **実際に厚みを付けて** 稜線を下見に出す。文書へは書かない。
void V2MainWindow::RefreshThickenPreview()
{
    thickenOutcome_ = kachakacha::v2::app::ThickenPreviewOutcome{};
    thickenBuilt_.reset();
    thickenBuiltEdges_.clear();
    viewport_->HideToolPreview();
    if (!kachakacha::v2::app::ThickenReadyToBuild(thickenInput_)) {
        return;
    }
    const auto found = guideShapes_.find(thickenInput_.surface.ToString());
    if (found == guideShapes_.end()) {
        thickenOutcome_.evaluated = true;
        thickenOutcome_.refusalJa = "選んだ面の形がまだありません";
        return;
    }
    thickenOutcome_.evaluated = true;
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    if (thickenInput_.toPlane) {
        const auto frame = WorkPlaneFrameOf(thickenInput_.targetPlane);
        if (!frame.has_value()) {
            thickenOutcome_.refusalJa = "相手の作業平面の形がまだありません";
            return;
        }
        const auto built = kachakacha::v2::kernel::ThickenSurfaceToPlane(found->second,
            frame->origin, frame->normal, tolerance);
        if (!built.HasValue()) {
            thickenOutcome_.refusalJa = built.FirstSummaryJa();
            return;
        }
        thickenOutcome_.available = true;
        thickenOutcome_.volumeMm3 = built.Value().volumeMm3;
        thickenOutcome_.thicknessMm = built.Value().thicknessMm;
        thickenBuilt_ = built.Value().handle;
        thickenBuiltEdges_ = built.Value().edges;
    } else {
        const auto built = kachakacha::v2::kernel::ThickenSurface(found->second,
            thickenInput_.thicknessMm, thickenInput_.placement, tolerance);
        if (!built.HasValue()) {
            thickenOutcome_.refusalJa = built.FirstSummaryJa();
            return;
        }
        thickenOutcome_.available = true;
        thickenOutcome_.volumeMm3 = built.Value().volumeMm3;
        thickenOutcome_.thicknessMm = built.Value().thicknessMm;
        thickenBuilt_ = built.Value().handle;
        thickenBuiltEdges_ = built.Value().edges;
    }
    // 出来上がりの稜線。画面に出すのは、確定で文書へ入るその形。
    const auto mesh = kachakacha::v2::kernel::BuildShapeMesh(*thickenBuilt_);
    if (mesh.HasValue()) {
        viewport_->ShowToolPreview(mesh.Value().edges);
    }
}

//! 棚・札・一番下の一行を、いまの入力に合わせる。
void V2MainWindow::RefreshThickenDock()
{
    if (thickenDock_ == nullptr) {
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
    for (const std::string& line : kachakacha::v2::app::ThickenStatusLinesJa(thickenInput_,
             thickenOutcome_, previewShown)) {
        lines.push_back(QString::fromStdString(line));
    }
    thickenDock_->SetTargets(ExtrudeTargets());
    thickenDock_->ShowInput(thickenInput_, QString::fromStdString(nameOf(thickenInput_.surface)),
        lines, thickenBuilt_.has_value());
    ShowToolFooter(thickenShelfShown_
            ? QString::fromStdString(kachakacha::v2::app::ThickenFooterLine(thickenInput_,
                  nameOf(thickenInput_.surface), nameOf(thickenInput_.targetPlane),
                  thickenOutcome_, previewShown))
            : QString());
    std::vector<kachakacha::v2::app::ToolRoleLabel> labels;
    if (!thickenInput_.surface.IsNil()) {
        labels.push_back({thickenInput_.surface, std::string("SURFACE")});
    }
    ShowRoleLabels(labels);
}

void V2MainWindow::RefreshThickenAll()
{
    RefreshThickenPreview();
    RefreshThickenDock();
}

//! 「選び直す」。面の欄を空にし、次のクリックを待つ。
void V2MainWindow::ReselectThicken()
{
    thickenInput_ = kachakacha::v2::app::WithoutThickenSurface(thickenInput_, thickenInput_.surface);
    MirrorThickenToSelection();
    RefreshThickenAll();
}

//! 作り方のカード(外側・中央・内側)。「平面まで」は解いて、指定の作り方にする。
void V2MainWindow::ChooseThickenPlacement(ThicknessPlacement value)
{
    thickenInput_.placement = value;
    thickenInput_.toPlane = false;
    thicknessPlacement_ = value;   // 部品の棚の組み合わせ欄とも値をそろえる(RefreshPartDock)。
    RefreshThickenAll();
}

//! 「平面まで」のカード。相手の作業平面は、既定でいちばん上のものにしておく。
void V2MainWindow::ChooseThickenToPlane()
{
    thickenInput_.toPlane = true;
    if (thickenInput_.targetPlane.IsNil()) {
        const auto targets = ExtrudeTargets();
        if (!targets.empty()) {
            thickenInput_.targetPlane = targets.front().entityId;
        }
    }
    RefreshThickenAll();
}

//! 相手の作業平面を選んだ。
void V2MainWindow::ChooseThickenTarget(const EntityId& planeId)
{
    thickenInput_.targetPlane = planeId;
    RefreshThickenAll();
}

//! 厚み(mm)の欄を打った。数の棚(板厚)とも値をそろえる。
void V2MainWindow::ApplyThickenThicknessMm(double value)
{
    thickenInput_.thicknessMm = value;
    (void)parameterDock_->Apply(kachakacha::v2::app::ParameterId::ExtrudeDistance,
        QString::number(value, 'f', 3));
    RefreshThickenAll();
}

//! やめる。文書は始める前とまったく同じ。
void V2MainWindow::EndThicken()
{
    thickenShelfShown_ = false;
    thickenBuilt_.reset();
    thickenBuiltEdges_.clear();
    thickenOutcome_ = kachakacha::v2::app::ThickenPreviewOutcome{};
    thickenMirror_ = EntityId{};
    if (viewport_ != nullptr) {
        viewport_->HideToolPreview();
        viewport_->HideToolRoleLabels();
        viewport_->SetToolPickActive(false);
        viewport_->SetToolPickToggle(false);
    }
    ShowToolFooter(QString());
    RefreshRightShelves();
}

//! 確定。**下見に使った形をそのまま**文書へ入れる。
void V2MainWindow::ConfirmThicken()
{
    if (!thickenBuilt_.has_value()) {
        SetStatus(QStringLiteral(
            "厚み: まだ作れません。面と作り方(平面までなら相手も)を入れてください。"));
        RefreshThickenDock();
        return;
    }
    const bool toPlane = thickenInput_.toPlane;
    const char* label = toPlane ? "面を平面まで" : "面に厚み";
    kachakacha::v2::domain::ThickenSurfaceDefinition definition;
    definition.surface = thickenInput_.surface;
    definition.thickness.value = thickenOutcome_.thicknessMm;
    definition.thickness.kind = kachakacha::v2::geometry::QuantityKind::Length;
    if (toPlane) {
        definition.targetPlane = thickenInput_.targetPlane;
    } else {
        definition.placement = static_cast<int>(thickenInput_.placement);
    }
    const auto outcome = thickenOutcome_;
    const auto handle = *thickenBuilt_;
    const auto edges = thickenBuiltEdges_;
    const auto madeId = AddPartFeature(kachakacha::v2::domain::FeatureType::ThickenSurface,
        std::move(definition), handle, edges, label, {thickenInput_.surface});
    if (madeId.IsNil()) {
        return;   // まとまりは戻る(AddPartFeature が診断を出している)。
    }
    EndThicken();
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("%1: 厚み %2 mm、体積 %3 mm3 の部品を作りました。")
            .arg(QString::fromUtf8(label))
            .arg(outcome.thicknessMm, 0, 'f', 3)
            .arg(outcome.volumeMm3, 0, 'f', 3));
}
