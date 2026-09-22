//! 線の上に置いて押す編集(トリム・延長・分割)。V2HoverEditTool.h の頭の注記を見よ。

#include "V2HoverEditTool.h"

#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/ExplorerModel.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/domain/Feature.h"

#include <QPointF>
#include <QString>

#include <string>
#include <utility>
#include <vector>

using kachakacha::v2::base::EntityId;
using kachakacha::v2::modeling::DrawingTool;

namespace {

[[nodiscard]] QString Text(const std::string& text)
{
    return QString::fromUtf8(text.c_str());
}

} // namespace

V2HoverEditTool::V2HoverEditTool(V2MainWindow& window)
    : window_(window)
{
}

bool V2HoverEditTool::Handles(DrawingTool tool) noexcept
{
    return tool == DrawingTool::Trim || tool == DrawingTool::Extend || tool == DrawingTool::Split;
}

bool V2HoverEditTool::Active() const
{
    return Handles(window_.session_->CurrentTool());
}

QString V2HoverEditTool::LabelJa() const
{
    return Text(std::string(kachakacha::v2::modeling::DrawingToolNameJa(window_.session_->CurrentTool())));
}

//! ポインタの下(position を渡せばその場所)の線。線でなければ値なし。
std::optional<kachakacha::v2::app::HoverEditPick> V2HoverEditTool::PickAt(
    const std::optional<QPointF>& position) const
{
    auto* viewport = window_.viewport_;
    if (position.has_value()) {
        viewport->RefreshPickCycleAt(*position);
    }
    const auto candidate = viewport->CurrentCandidate();
    if (!candidate.has_value() || candidate->segmentId.IsNil() || !candidate->curveParameter.has_value()) {
        return std::nullopt;
    }
    const auto* entity = window_.session_->GetDocument().FindEntity(candidate->entityId);
    if (entity == nullptr || entity->kind != kachakacha::v2::domain::EntityKind::Wire) {
        return std::nullopt;
    }
    kachakacha::v2::app::HoverEditPick pick;
    pick.entityId = candidate->entityId;
    pick.segmentId = candidate->segmentId;
    pick.parameter = *candidate->curveParameter;
    pick.point = candidate->hitPoint;
    return pick;
}

kachakacha::v2::base::Result<kachakacha::v2::app::HoverEditOutcome> V2HoverEditTool::Plan(
    const kachakacha::v2::app::HoverEditPick& pick) const
{
    const auto& tolerance = window_.session_->GetDocument().Snapshot().settings.tolerance;
    const auto& scene = window_.session_->Scene();
    switch (window_.session_->CurrentTool()) {
    case DrawingTool::Extend:
        return kachakacha::v2::app::PlanExtend(scene, pick, tolerance);
    case DrawingTool::Split:
        return kachakacha::v2::app::PlanSplit(scene, pick, tolerance);
    default:
        return kachakacha::v2::app::PlanTrim(scene, pick, tolerance);
    }
}

void V2HoverEditTool::RefreshPreview()
{
    if (!Active()) {
        Clear();
        return;
    }
    const auto pick = PickAt(std::nullopt);
    outcome_.reset();
    refusalJa_.clear();
    if (!pick.has_value()) {
        window_.viewport_->HideEditPreview();
        window_.ShowToolFooter(LabelJa() + QStringLiteral(": NEXT=線の上に置く"));
        return;
    }
    const auto planned = Plan(*pick);
    if (!planned.HasValue()) {
        refusalJa_ = Text(planned.FirstSummaryJa());
        window_.viewport_->HideEditPreview();
        window_.ShowToolFooter(LabelJa() + QStringLiteral(": × ") + refusalJa_);
        window_.SetStatus(LabelJa() + QStringLiteral(": ") + refusalJa_);
        return;
    }
    outcome_ = planned.Value();
    V2Viewport::EditPreview preview;
    if (!outcome_->previewLine.empty()) {
        preview.lines.push_back(outcome_->previewLine);
    }
    if (outcome_->previewPoint.has_value()) {
        preview.markers.push_back(*outcome_->previewPoint);
    }
    preview.removing = window_.session_->CurrentTool() == DrawingTool::Trim;
    window_.viewport_->ShowEditPreview(std::move(preview));
    window_.ShowToolFooter(Text(outcome_->footerJa) + QStringLiteral(" / Preview only"));
    window_.SetStatus(LabelJa() + QStringLiteral(": ") + Text(outcome_->summaryJa)
        + QStringLiteral(" 押すとそうなります。Esc でやめます。"));
}

bool V2HoverEditTool::Click(const QPointF& position)
{
    if (!Active()) {
        return false;
    }
    const auto pick = PickAt(position);
    if (!pick.has_value()) {
        window_.SetStatus(LabelJa() + QStringLiteral(": 線の上を押してください。"));
        return true;
    }
    const auto planned = Plan(*pick);
    if (!planned.HasValue()) {
        window_.ReportDiagnostics(planned.Diagnostics());
        return true;
    }
    Apply(planned.Value());
    return true;
}

void V2HoverEditTool::Clear()
{
    outcome_.reset();
    refusalJa_.clear();
    if (window_.viewport_ != nullptr) {
        window_.viewport_->HideEditPreview();
    }
}

//! 残った鎖を新しいワイヤーとして入れ、元の線を消す。1 回で戻る。
void V2HoverEditTool::Apply(const kachakacha::v2::app::HoverEditOutcome& outcome)
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;
    using kachakacha::v2::domain::FeatureType;

    auto& document = window_.session_->GetDocument();
    const auto* source = document.FindEntity(outcome.source.entityId);
    if (source == nullptr) {
        return;
    }
    const std::string label = LabelJa().toStdString();
    const std::string baseName = source->displayName;
    const auto groupId = source->groupId;
    const bool datum = source->datum;
    kachakacha::v2::document::Document::Transaction transaction(document, label);
    for (std::size_t index = 0; index < outcome.chains.size(); ++index) {
        Feature feature;
        feature.id = window_.ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
        feature.type = FeatureType::TransformWire;
        feature.displayName = label;
        feature.inputEntityIds = {outcome.source.entityId};
        kachakacha::v2::domain::CreateWireDefinition wire;
        wire.segments = outcome.chains[index];
        wire.construction = outcome.source.construction;
        for (std::size_t k = 0; k < wire.segments.size(); ++k) {
            wire.segmentIds.push_back(window_.ids_->NextTyped<kachakacha::v2::base::IdKind::Segment>());
        }
        feature.definition = std::move(wire);
        Entity entity;
        entity.id = window_.ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
        entity.kind = EntityKind::Wire;
        // 名前は元の線のまま(2 本に分かれたら 2 本目に番号)。一覧に「トリム」が並ばないように。
        entity.displayName = index == 0 ? baseName
            : kachakacha::v2::app::UniqueDisplayName(document.Snapshot(), EntityKind::Wire, baseName);
        entity.groupId = groupId;
        entity.datum = datum;
        entity.construction = outcome.source.construction;
        entity.createdBy = feature.id;
        feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});
        const auto added = document.Run(AddFeatureCommand(feature, {entity}, label));
        if (!added.committed) {
            window_.ReportDiagnostics(added.diagnostics);
            return;   // Transaction が捨てる
        }
    }
    window_.RemoveConsumedWires({outcome.source.entityId});
    if (!transaction.Commit()) {
        window_.SetStatus(LabelJa() + QStringLiteral(": 途中で失敗したので、何も変えていません。"));
        window_.AdoptCurrentDocument();
        return;
    }
    Clear();
    window_.AdoptCurrentDocument();
    window_.SetStatus(LabelJa() + QStringLiteral(": ") + Text(outcome.summaryJa)
        + QStringLiteral(" 続けて押せます。Esc でやめます。"));
}
