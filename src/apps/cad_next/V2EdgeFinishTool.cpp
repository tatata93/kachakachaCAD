//! 「辺の丸め・面取り」の道具。V2EdgeFinishTool.h の頭の注記を見よ。

#include "V2EdgeFinishTool.h"

#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ToolRoleLabels.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/kernel/OcctEdgeFinish.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/modeling/ToolController.h"

#include <QKeyEvent>
#include <QString>

#include <algorithm>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EdgeFinishDefinition;
using kachakacha::v2::domain::EntityKind;

namespace {

[[nodiscard]] QString Text(const std::string& text)
{
    return QString::fromUtf8(text.c_str());
}

[[nodiscard]] std::string LabelOf(int kind)
{
    return std::string(kachakacha::v2::app::EdgeFinishKindNameJa(kind));
}

[[nodiscard]] kachakacha::v2::kernel::EdgeFinishKind KernelKind(int kind)
{
    return kind == 1 ? kachakacha::v2::kernel::EdgeFinishKind::Chamfer
                     : kachakacha::v2::kernel::EdgeFinishKind::Fillet;
}

} // namespace

V2EdgeFinishTool::V2EdgeFinishTool(V2MainWindow& window)
    : window_(window)
{
    dock_ = new V2EdgeFinishDock(&window);
    dock_->SetKindHandler([this](int kind) {
        input_.kind = kind;
        Refresh();
    });
    dock_->SetSizeHandler([this](double value) {
        input_.sizeMm = value;
        Refresh();
    });
    dock_->SetClearHandlers(
        [this] {
            input_ = kachakacha::v2::app::WithoutEdgeFinishPart(input_);
            MirrorToSelection();
            Refresh();
        },
        [this] {
            input_ = kachakacha::v2::app::WithoutEdgeFinishEdges(input_);
            Refresh();
        });
    dock_->SetActionHandlers([this] { Confirm(); },
        [this] {
            const std::string label = LabelOf(input_.kind);
            End();
            window_.SetStatus(Text(label + ": やめました。何も作っていません。"));
        });
}

bool V2EdgeFinishTool::Handles(std::string_view commandId)
{
    int kind = 0;
    return kachakacha::v2::app::EdgeFinishKindForCommand(commandId, kind);
}

double V2EdgeFinishTool::JoinMm() const
{
    const auto& tolerance = window_.session_->GetDocument().Snapshot().settings.tolerance;
    return std::max(tolerance.interactiveJoinMm, 1.0e-4);
}

void V2EdgeFinishTool::Begin(std::string_view commandId)
{
    int kind = 0;
    if (!kachakacha::v2::app::EdgeFinishKindForCommand(commandId, kind)) {
        return;
    }
    if (active_) {
        // 構えている間の2度目は確定。違う種類なら種類だけ替える(部品と辺はそのまま)。
        if (kind == input_.kind) {
            Confirm();
            return;
        }
        input_.kind = kind;
        Refresh();
        return;
    }
    if (window_.session_->CurrentTool() != kachakacha::v2::modeling::DrawingTool::Select) {
        window_.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    }
    const double previousSize = input_.sizeMm;
    input_ = kachakacha::v2::app::EdgeFinishInputState{};
    input_.kind = kind;
    input_.sizeMm = previousSize > 0.0 ? previousSize : 1.0;
    // 選んであった部品は相手に入れる(辺は 3D で押して選ぶ)。
    for (const EntityId& id : window_.viewport_->Selection().entityIds) {
        const auto* entity = window_.session_->GetDocument().FindEntity(id);
        if (entity != nullptr && entity->kind == EntityKind::Part) {
            input_ = kachakacha::v2::app::WithEdgeFinishPick(input_, id, std::nullopt, JoinMm());
            break;
        }
    }
    active_ = true;
    window_.RefreshRightShelves();
    window_.viewport_->SetToolPickActive(true);
    window_.viewport_->SetToolPickToggle(true);
    MirrorToSelection();
    Refresh();
    window_.SetStatus(Text(LabelOf(kind) + ": 部品の" + (kind == 1 ? "落としたい" : "丸めたい")
        + "辺の近くを 3D で押してください(何本でも。入っている辺の近くを押すと外れます)。"
          "大きさは右の棚で決めます。Enter で確定、Esc でやめます。"));
}

void V2EdgeFinishTool::End()
{
    active_ = false;
    built_.reset();
    outcome_ = kachakacha::v2::app::EdgeFinishOutcome{};
    if (window_.viewport_ != nullptr) {
        window_.viewport_->HideToolPreview();
        window_.viewport_->HideToolRoleLabels();
        window_.viewport_->SetToolPickActive(false);
        window_.viewport_->SetToolPickToggle(false);
        mirroring_ = true;
        window_.viewport_->SetSelection(kachakacha::v2::app::SelectionSet{});
        mirroring_ = false;
    }
    window_.ShowToolFooter(QString());
    window_.RefreshRightShelves();
}

bool V2EdgeFinishTool::HandleKey(int key)
{
    if (!active_) {
        return false;
    }
    if (key == Qt::Key_Escape) {
        const std::string label = LabelOf(input_.kind);
        End();
        window_.SetStatus(Text(label + ": やめました。何も作っていません。"));
        return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        Confirm();
        return true;
    }
    return false;
}

//! 3D の選択の印を、相手の部品に合わせる(辺は下見の線で見える)。
void V2EdgeFinishTool::MirrorToSelection()
{
    kachakacha::v2::app::SelectionSet mirrored;
    if (!input_.part.IsNil()) {
        mirrored.entityIds.push_back(input_.part);
        kachakacha::v2::app::SelectionRef ref;
        ref.entityId = input_.part;
        mirrored.ordered.push_back(ref);
    }
    mirroring_ = true;
    window_.viewport_->SetSelection(std::move(mirrored));
    mirroring_ = false;
}

void V2EdgeFinishTool::HandleSelectionChanged()
{
    if (!active_ || mirroring_ || window_.viewport_ == nullptr) {
        return;
    }
    if (const auto pick = window_.viewport_->TakeLastToolPick(); pick.has_value()) {
        ApplyPick(*pick);
    }
    MirrorToSelection();   // 受けなかったもの(線など)を選択に残さない。
    Refresh();
}

//! 部品を押した: 押した点に一番近い辺を核に尋ねて入れる(同じ辺なら外れる)。
void V2EdgeFinishTool::ApplyPick(const EntityId& id)
{
    const auto* entity = window_.session_->GetDocument().FindEntity(id);
    if (entity == nullptr || entity->kind != EntityKind::Part) {
        window_.SetStatus(Text(LabelOf(input_.kind) + ": 部品の辺の近くを押してください。"));
        return;
    }
    const auto shape = window_.partShapes_.find(id.ToString());
    const auto hit = window_.viewport_->LastToolPickPoint();
    std::optional<kachakacha::v2::geometry::Vector3> midpoint;
    if (shape != window_.partShapes_.end() && hit.has_value()) {
        const auto nearest = kachakacha::v2::kernel::NearestSolidEdge(shape->second, *hit);
        if (nearest.HasValue()) {
            midpoint = nearest.Value().midpoint;
        } else {
            window_.ReportDiagnostics(nearest.Diagnostics());
        }
    }
    input_ = kachakacha::v2::app::WithEdgeFinishPick(input_, id, midpoint, JoinMm());
}

std::string V2EdgeFinishTool::NameOf(const EntityId& id) const
{
    const auto* entity = id.IsNil() ? nullptr : window_.session_->GetDocument().FindEntity(id);
    return entity != nullptr && !entity->displayName.empty() ? entity->displayName
                                                              : std::string("名前のないもの");
}

void V2EdgeFinishTool::Refresh()
{
    RefreshPreview();
    RefreshDock();
}

//! そろっていれば **実際に核で丸めて** 稜線を下見に出す。作れなければ選んだ辺だけを出す。
void V2EdgeFinishTool::RefreshPreview()
{
    outcome_ = kachakacha::v2::app::EdgeFinishOutcome{};
    built_.reset();
    window_.viewport_->HideToolPreview();
    const auto shape = input_.part.IsNil() ? window_.partShapes_.end()
                                           : window_.partShapes_.find(input_.part.ToString());
    if (shape == window_.partShapes_.end()) {
        return;
    }
    const auto& tolerance = window_.session_->GetDocument().Snapshot().settings.tolerance;
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> chosen;
    for (const auto& midpoint : input_.edges) {
        const auto edge = kachakacha::v2::kernel::SolidEdgeAt(shape->second, midpoint, tolerance);
        if (edge.HasValue()) {
            chosen.push_back(edge.Value().polyline);
        }
    }
    if (!kachakacha::v2::app::EdgeFinishReady(input_)) {
        if (!chosen.empty()) {
            window_.viewport_->ShowToolPreview(std::move(chosen));
        }
        return;
    }
    outcome_.evaluated = true;
    const auto made = kachakacha::v2::kernel::FinishSolidEdges(shape->second,
        KernelKind(input_.kind), input_.sizeMm, input_.edges, tolerance);
    if (!made.HasValue()) {
        outcome_.refusalJa = made.FirstSummaryJa();
        const auto first = made.FirstDiagnostic();
        if (!first.detailsJa.empty()) {
            outcome_.refusalJa += "(" + first.detailsJa + ")";
        }
        window_.viewport_->ShowToolPreview(std::move(chosen));   // どの辺かは見せる
        return;
    }
    outcome_.available = true;
    outcome_.volumeMm3 = made.Value().volumeMm3;
    outcome_.previousVolumeMm3 = made.Value().previousVolumeMm3;
    built_ = made.Value().handle;
    const auto mesh = kachakacha::v2::kernel::BuildShapeMesh(made.Value().handle);
    if (mesh.HasValue()) {
        window_.viewport_->ShowToolPreview(mesh.Value().edges);
    }
}

void V2EdgeFinishTool::RefreshDock()
{
    const bool previewShown = !window_.viewport_->ToolPreview().empty() && outcome_.available;
    std::vector<QString> lines;
    for (const std::string& line :
        kachakacha::v2::app::EdgeFinishStatusLinesJa(input_, outcome_, previewShown)) {
        lines.push_back(Text(line));
    }
    const std::string part = NameOf(input_.part);
    dock_->ShowInput(input_, Text(part), lines, built_.has_value());
    window_.ShowToolFooter(active_
            ? Text(kachakacha::v2::app::EdgeFinishFooterLine(input_, part, outcome_, previewShown))
            : QString());
    if (!active_) {
        return;
    }
    std::vector<kachakacha::v2::app::ToolRoleLabel> labels;
    if (!input_.part.IsNil()) {
        labels.push_back({input_.part, "PART"});
    }
    window_.ShowRoleLabels(labels);
}

//! 確定。**下見に使った形をそのまま**文書へ入れる。元の部品を隠すのも含めて 1 回で戻せる。
void V2EdgeFinishTool::Confirm()
{
    const std::string label = LabelOf(input_.kind);
    if (!built_.has_value()) {
        window_.SetStatus(Text(label + ": まだ作れません。"
            + (outcome_.refusalJa.empty() ? kachakacha::v2::app::EdgeFinishHintJa(input_)
                                          : outcome_.refusalJa)));
        RefreshDock();
        return;
    }
    EdgeFinishDefinition definition;
    definition.kind = input_.kind;
    definition.source = input_.part;
    definition.size.value = input_.sizeMm;
    definition.size.expression = std::to_string(input_.sizeMm);
    definition.size.kind = kachakacha::v2::geometry::QuantityKind::Length;
    definition.edgeMidpoints = input_.edges;
    const auto handle = *built_;
    const auto outcome = outcome_;
    const EntityId source = input_.part;
    auto& document = window_.session_->GetDocument();
    kachakacha::v2::document::Document::Transaction transaction(document, label.c_str());
    const auto madeId = window_.AddPartFeature(kachakacha::v2::domain::FeatureType::EdgeFinish,
        definition, handle, {}, label.c_str(), {source});
    if (madeId.IsNil()) {
        return;
    }
    // 元の部品は隠す(消すと作り方をたどれない)。
    const auto hidden = document.Run(kachakacha::v2::document::SetVisibilityCommand(
        {source}, kachakacha::v2::domain::Visibility::Hidden));
    if (!hidden.committed) {
        window_.ReportDiagnostics(hidden.diagnostics);
        return;
    }
    if (!transaction.Commit()) {
        return;
    }
    End();
    window_.AdoptCurrentDocument();
    window_.SetStatus(QStringLiteral("%1: 辺を %2 本 %3、体積が %4 mm3 から %5 mm3 になりました。")
            .arg(Text(label))
            .arg(static_cast<int>(definition.edgeMidpoints.size()))
            .arg(definition.kind == 1 ? QStringLiteral("落として") : QStringLiteral("丸めて"))
            .arg(outcome.previousVolumeMm3, 0, 'f', 4)
            .arg(outcome.volumeMm3, 0, 'f', 4));
}

bool V2EdgeFinishTool::Rebuild(const kachakacha::v2::domain::Feature& feature, const EntityId& output)
{
    const auto* definition = std::get_if<EdgeFinishDefinition>(&feature.definition);
    if (definition == nullptr) {
        return false;
    }
    const auto source = window_.partShapes_.find(definition->source.ToString());
    if (source == window_.partShapes_.end()) {
        return false;   // 元がまだ出来ていない。黙って作らない。
    }
    const auto made = kachakacha::v2::kernel::FinishSolidEdges(source->second,
        KernelKind(definition->kind), definition->size.value, definition->edgeMidpoints,
        window_.session_->GetDocument().Snapshot().settings.tolerance);
    if (!made.HasValue()) {
        window_.ReportDiagnostics(made.Diagnostics());
        return false;
    }
    window_.partShapes_[output.ToString()] = made.Value().handle;
    window_.partEdges_[output.ToString()] = {};
    return true;
}
