//! 「近似」を道具から始める(引継ぎ 2026-09-17 の 3)。
//!
//! これまでの近似は「選んでから押す」しかなく、押した瞬間に文書へ入っていた。
//! 何枚になるのか、どれだけずれるのかは、作ってからしか分からなかった。
//!
//! ここでは 近似を押す → 3D で面や立体を押す(もう一度押すと外れる)→
//! 3通りを **実際に作って** 見比べる → 選んだ候補だけ下見 → Enter で確定。
//! 確定は下見に使った作り方と結果をそのまま文書へ入れる(§9 と同じ決まり)。
//! 1回の確定は 1つの AddFeatureCommand = 1つの取り消し単位。
//!
//! 製作の棚(FabricationDock)とその工程タブ、EvaluateFabrication はそのまま使う。
//! backend を画面のために複製しない。

#include "V2MainWindow.h"

#include "V2FabricationDock.h"
#include "V2ParameterDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/ApproxInput.h"
#include "kachakacha/app/CommandParameters.h"
#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/app/FabricationOptions.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/SurfacePreview.h"
#include "kachakacha/app/ToolRoleLabels.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/domain/Feature.h"

#include <QString>

#include <algorithm>
#include <string>
#include <vector>

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

//! 棚の欄と数の棚から、候補に共通の作り方を組み立てる。
kachakacha::v2::domain::CreateFabricationModelDefinition V2MainWindow::ApproxBaseDefinition()
    const
{
    kachakacha::v2::domain::CreateFabricationModelDefinition definition;
    definition.parts = approxInput_.sources;
    kachakacha::v2::app::ApplyFabricationChoice(definition, fabricationChoice_);
    definition.targetMaxDeviation.value = kachakacha::v2::app::ParameterValueOf(
        parameterDock_->Values(), kachakacha::v2::app::ParameterId::MaxDeviationMm);
    definition.targetMaxDeviation.kind = kachakacha::v2::geometry::QuantityKind::Length;
    definition.materialThickness.value = ExtrudeDistanceMm();
    definition.materialThickness.kind = kachakacha::v2::geometry::QuantityKind::Length;
    return definition;
}

//! 3通りを実際に作って比べる。**見積もりではない。**同じ backend を3回回す。
void V2MainWindow::EvaluateApproxCandidates()
{
    approxOutcomes_.clear();
    approxEvaluations_.clear();
    approxDefinitions_.clear();
    const auto& specs = kachakacha::v2::app::ApproxCandidateSpecs();
    approxOutcomes_.resize(specs.size());
    approxEvaluations_.resize(specs.size());
    approxDefinitions_.resize(specs.size());
    if (approxInput_.sources.empty()) {
        return;
    }
    const auto base = ApproxBaseDefinition();
    const double tolerance =
        session_->GetDocument().Snapshot().settings.tolerance.interactiveJoinMm;
    for (std::size_t index = 0; index < specs.size(); ++index) {
        const auto definition = kachakacha::v2::app::ApproxCandidateDefinition(base,
            static_cast<int>(index));
        approxDefinitions_[index] = definition;
        auto& outcome = approxOutcomes_[index];
        outcome.evaluated = true;
        const auto sources = FabricationSourcesFor(definition.parts, definition.splitSolidFaces);
        if (sources.empty()) {
            outcome.available = false;
            outcome.refusalJa = "元になる面が取れません";
            continue;
        }
        const auto evaluated = kachakacha::v2::app::EvaluateFabrication(definition, sources,
            FabricationMarkingsFor(definition), tolerance);
        if (!evaluated.HasValue()) {
            outcome.available = false;
            outcome.refusalJa = evaluated.FirstSummaryJa();
            continue;
        }
        outcome.available = true;
        outcome.partCount = evaluated.Value().panels.size();
        outcome.maximumDeviationMm = evaluated.Value().maximumDeviationMm;
        outcome.reachedTolerance = evaluated.Value().reachedTolerance;
        approxEvaluations_[index] = evaluated.Value();
    }
    if (!approxInput_.candidateChosenByUser) {
        approxInput_.selectedCandidate =
            kachakacha::v2::app::PreferredApproxCandidate(approxOutcomes_, base.method);
    }
}

//! 選んでいる候補だけを下見に出す。**文書へは書かない。**
void V2MainWindow::ShowApproxPreview()
{
    if (viewport_ == nullptr) {
        return;
    }
    viewport_->HideToolPreview();
    const std::size_t chosen =
        static_cast<std::size_t>(std::max(0, approxInput_.selectedCandidate));
    if (chosen >= approxEvaluations_.size() || !approxEvaluations_[chosen].has_value()) {
        return;
    }
    const auto& definition = approxDefinitions_[chosen];
    const auto& evaluation = *approxEvaluations_[chosen];
    // V1 方式(帯)なら、いまの曲げ状態での帯のレール。
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> lines =
        kachakacha::v2::app::FoldedRailsOf(definition, evaluation, 8.0);
    if (lines.empty()) {
        // V2 方式(面ごと)はレールが無い。元の面の格子と輪郭を出す。
        for (const auto& source :
            FabricationSourcesFor(definition.parts, definition.splitSolidFaces)) {
            if (source.samples.has_value()) {
                auto grid = kachakacha::v2::app::SurfaceGridLines(*source.samples, 7);
                lines.insert(lines.end(), grid.begin(), grid.end());
            }
            if (source.flatBoundary.has_value()) {
                auto edge = kachakacha::v2::app::SurfaceBoundaryLines(*source.flatBoundary, 6);
                lines.insert(lines.end(), edge.begin(), edge.end());
            }
        }
    }
    viewport_->ShowToolPreview(std::move(lines));
}

//! 棚・札・一番下の一行を、いまの入力に合わせる。
void V2MainWindow::RefreshApproxDock()
{
    if (fabricationDock_ == nullptr) {
        return;
    }
    const auto& document = session_->GetDocument();
    QString sources;
    for (const EntityId& id : approxInput_.sources) {
        const auto* entity = document.FindEntity(id);
        if (!sources.isEmpty()) {
            sources += QStringLiteral(", ");
        }
        sources += entity != nullptr && !entity->displayName.empty()
            ? QString::fromStdString(entity->displayName)
            : QStringLiteral("名前のないもの");
    }
    std::vector<QString> lines;
    const auto& specs = kachakacha::v2::app::ApproxCandidateSpecs();
    for (std::size_t index = 0; index < specs.size(); ++index) {
        lines.push_back(QString::fromStdString(kachakacha::v2::app::ApproxCandidateLineJa(
            specs[index], index < approxOutcomes_.size() ? approxOutcomes_[index]
                                                          : kachakacha::v2::app::ApproxCandidateOutcome{})));
    }
    const std::size_t chosen =
        static_cast<std::size_t>(std::max(0, approxInput_.selectedCandidate));
    const bool canConfirm = chosen < approxEvaluations_.size()
        && approxEvaluations_[chosen].has_value();
    fabricationDock_->ShowApproxInput(sources, lines,
        approxInput_.sources.empty() ? -1 : approxInput_.selectedCandidate, canConfirm);
    QString status;
    for (const std::string& line : kachakacha::v2::app::ApproxStatusLinesJa(approxInput_,
             approxOutcomes_, canConfirm)) {
        if (!status.isEmpty()) {
            status += QStringLiteral("\n");
        }
        status += QString::fromStdString(line);
    }
    fabricationDock_->SetMessage(status);
    ShowToolFooter(approxShelfShown_
            ? QString::fromStdString(kachakacha::v2::app::ApproxFooterLine(approxInput_,
                  approxOutcomes_, canConfirm))
            : QString());
    // 3D の札。何を対象にしているかを、名前ではなく画面で示す。
    std::vector<kachakacha::v2::app::ToolRoleLabel> labels;
    for (std::size_t index = 0; index < approxInput_.sources.size(); ++index) {
        labels.push_back(kachakacha::v2::app::ToolRoleLabel{approxInput_.sources[index],
            approxInput_.sources.size() == 1
                ? std::string("SOURCE")
                : "SOURCE " + std::to_string(index + 1)});
    }
    ShowRoleLabels(labels);
}

//! 3D の選択の印を、対象の合計に合わせる。対象が正本。
void V2MainWindow::MirrorApproxSourcesToSelection()
{
    approxMirror_ = approxInput_.sources;
    kachakacha::v2::app::SelectionSet mirrored;
    mirrored.entityIds = approxMirror_;
    for (const EntityId& id : approxMirror_) {
        kachakacha::v2::app::SelectionRef ref;
        ref.entityId = id;
        mirrored.ordered.push_back(ref);
    }
    approxMirroring_ = true;
    viewport_->SetSelection(std::move(mirrored));
    approxMirroring_ = false;
}

//! 入力が変わった。作り直し、下見と棚を出し直す。
void V2MainWindow::RefreshApproxAll()
{
    EvaluateApproxCandidates();
    ShowApproxPreview();
    RefreshApproxDock();
}

//! 3D の選択が変わった。差分を対象へ入れる/外す。面か立体だけを受ける。
void V2MainWindow::RefreshApproxForSelectionChange()
{
    if (!approxShelfShown_ || approxMirroring_ || viewport_ == nullptr) {
        return;
    }
    const auto& now = viewport_->Selection().entityIds;
    const auto added = Missing(now, approxMirror_);
    const auto removed = Missing(approxMirror_, now);
    const auto& document = session_->GetDocument();
    std::vector<EntityId> accepted;
    for (const EntityId& id : added) {
        const auto* entity = document.FindEntity(id);
        if (entity != nullptr
            && (entity->kind == EntityKind::Part || entity->kind == EntityKind::GuideSurface)) {
            accepted.push_back(id);
        }
    }
    if (accepted.empty() && removed.empty()) {
        MirrorApproxSourcesToSelection();   // 受けなかったもの(線など)を選択に残さない。
        return;
    }
    approxInput_ = kachakacha::v2::app::WithoutApproxSources(approxInput_, removed);
    approxInput_ = kachakacha::v2::app::WithApproxSourcesToggled(approxInput_, accepted);
    MirrorApproxSourcesToSelection();
    RefreshApproxAll();
}

//! 「近似」を押した。1度目は道具を構えて棚を出し、2度目は確定。
void V2MainWindow::RunFabricationCreate()
{
    if (approxShelfShown_) {
        ConfirmApprox();
        return;
    }
    // 選んであるもののうち、面と立体を対象に取り込む(選んでから押す道も残す)。
    approxInput_ = kachakacha::v2::app::ApproxInputState{};
    const auto& document = session_->GetDocument();
    for (const EntityId& id : viewport_->Selection().entityIds) {
        const auto* entity = document.FindEntity(id);
        if (entity != nullptr
            && (entity->kind == EntityKind::Part || entity->kind == EntityKind::GuideSurface)) {
            approxInput_.sources.push_back(id);
        }
    }
    approxShelfShown_ = true;
    // 製作の棚を前へ。工程タブは「1 近似モデル」。
    SetMode(kachakacha::v2::app::UiMode::Fabrication);
    if (fabricationDock_ != nullptr) {
        fabricationDock_->SetStageIndex(0);
    }
    RefreshRightShelves();
    viewport_->SetToolPickActive(true);
    viewport_->SetToolPickToggle(true);
    MirrorApproxSourcesToSelection();
    RefreshApproxAll();
    SetStatus(QStringLiteral("近似\n3D で面か立体を押してください。押すたびに候補を作って比べます。"
                             "Enter で確定、Esc でやめます。"));
}

//! 候補のボタン。選んだ候補だけを下見に出す。
void V2MainWindow::ChooseApproxCandidate(int candidate)
{
    approxInput_.selectedCandidate = candidate;
    approxInput_.candidateChosenByUser = true;
    ShowApproxPreview();
    RefreshApproxDock();
}

//! 対象の「解除」。
void V2MainWindow::ClearApproxSources()
{
    approxInput_.sources.clear();
    approxInput_.candidateChosenByUser = false;
    MirrorApproxSourcesToSelection();
    RefreshApproxAll();
    SetStatus(QStringLiteral("近似: 対象を空にしました。"));
}

//! やめる。文書は始める前とまったく同じ。
void V2MainWindow::EndApprox()
{
    approxShelfShown_ = false;
    approxOutcomes_.clear();
    approxEvaluations_.clear();
    approxDefinitions_.clear();
    approxMirror_.clear();
    if (viewport_ != nullptr) {
        viewport_->HideToolPreview();
        viewport_->HideToolRoleLabels();
        viewport_->SetToolPickActive(false);
        viewport_->SetToolPickToggle(false);
    }
    if (fabricationDock_ != nullptr) {
        fabricationDock_->ShowApproxInput(QString(), {}, -1, false);
        fabricationDock_->SetMessage(QString());
    }
    ShowToolFooter(QString());
}

//! 確定。**下見に使った作り方と結果をそのまま**文書へ入れる。1つの取り消し単位。
void V2MainWindow::ConfirmApprox()
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;
    using kachakacha::v2::domain::FeatureType;
    const std::size_t chosen =
        static_cast<std::size_t>(std::max(0, approxInput_.selectedCandidate));
    if (chosen >= approxEvaluations_.size() || !approxEvaluations_[chosen].has_value()) {
        SetStatus(QStringLiteral("近似: まだ作れません。対象を選び、作れる候補を選んでください。"));
        RefreshApproxDock();
        return;
    }
    const auto definition = approxDefinitions_[chosen];
    const auto evaluation = *approxEvaluations_[chosen];
    Feature feature;
    feature.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
    feature.type = FeatureType::CreateFabricationModel;
    feature.displayName = "近似モデル";
    feature.inputEntityIds = definition.parts;
    feature.definition = definition;
    Entity entity;
    entity.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
    entity.kind = EntityKind::FabricationModel;
    entity.displayName = "近似モデル";
    entity.createdBy = feature.id;
    feature.outputs.push_back(
        FeatureOutput{"fabrication", entity.id, EntityKind::FabricationModel});
    const auto added = session_->GetDocument().Run(
        AddFeatureCommand(feature, {entity}, "製作モデルを作る"));
    if (!added.committed) {
        ReportDiagnostics(added.diagnostics);
        return;
    }
    fabricationModels_[entity.id.ToString()] = evaluation;
    const QString summary = QString::fromStdString(evaluation.summaryJa);
    EndApprox();
    AdoptCurrentDocument();
    RefreshFabricationView();
    SetStatus(QStringLiteral("製作モデルを作る: %1").arg(summary));
}
