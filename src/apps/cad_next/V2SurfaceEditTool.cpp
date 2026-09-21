//! 「面の編集」の道具。V2SurfaceEditTool.h の頭の注記を見よ。

#include "V2SurfaceEditTool.h"

#include "V2MainWindow.h"
#include "V2SurfaceAnalysisTool.h"
#include "V2Viewport.h"

#include "kachakacha/app/ExplorerModel.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/SurfacePreview.h"
#include "kachakacha/app/ToolRoleLabels.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/kernel/OcctSurfaceEdit.h"

#include <QKeyEvent>
#include <QString>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using kachakacha::v2::app::SurfaceEditOperation;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;

namespace {

[[nodiscard]] QString Text(const std::string& text)
{
    return QString::fromUtf8(text.c_str());
}

[[nodiscard]] QString Text(std::string_view text)
{
    return Text(std::string(text));
}

} // namespace

V2SurfaceEditTool::V2SurfaceEditTool(V2MainWindow& window)
    : window_(window)
{
    dock_ = new V2SurfaceEditDock(&window);
    dock_->SetOperationHandler([this](SurfaceEditOperation operation) { Choose(operation); });
    dock_->SetRemoveHandler([this](const EntityId& id) {
        input_ = kachakacha::v2::app::WithoutSurfaceEditEntry(input_, id);
        MirrorToSelection();
        Refresh();
    });
    dock_->SetClearHandler([this] {
        input_.edges.clear();
        input_.surfaces.clear();
        input_.wires.clear();
        MirrorToSelection();
        Refresh();
    });
    dock_->SetOptionsHandler([this](const kachakacha::v2::app::SurfaceEditInputState& next) {
        input_.continuityA = next.continuityA;
        input_.continuityB = next.continuityB;
        input_.toleranceMm = next.toleranceMm;
        input_.tension = next.tension;
        input_.isoDirection = next.isoDirection;
        input_.isoCount = next.isoCount;
        input_.mirrorPlane = next.mirrorPlane;
        Refresh();
    });
    dock_->SetActionHandlers([this] { Confirm(); },
        [this] {
            End();
            window_.SetStatus(QStringLiteral("面の編集: やめました。何も作っていません。"));
        });
}

bool V2SurfaceEditTool::Handles(std::string_view commandId) const
{
    SurfaceEditOperation operation = SurfaceEditOperation::Match;
    // 既存の「面へ投影」(wire.project_surface)は、選んでから押す道をそのまま残す。
    // 棚の「面へ投影」のカードから、下見つきで同じ厳密な投影を使える。
    return commandId != "wire.project_surface"
        && kachakacha::v2::app::SurfaceEditOperationForCommand(commandId, operation);
}

void V2SurfaceEditTool::Begin(std::string_view commandId)
{
    SurfaceEditOperation operation = SurfaceEditOperation::Match;
    if (!kachakacha::v2::app::SurfaceEditOperationForCommand(commandId, operation)) {
        return;
    }
    if (active_) {
        Choose(operation);
        return;
    }
    input_ = kachakacha::v2::app::SurfaceEditInputState{};
    input_.operation = operation;
    active_ = true;
    // 選んであった形状ガイドの面・線は、そのまま欄へ入れる(選んでから押す道も残す)。
    const auto& selection = window_.viewport_->Selection();
    for (const auto& ref : selection.ordered) {
        const auto* entity = window_.session_->GetDocument().FindEntity(ref.entityId);
        if (entity == nullptr) {
            continue;
        }
        if (entity->kind == EntityKind::GuideSurface) {
            int edge = -1;
            if (kachakacha::v2::app::SurfaceEditSlotsFor(operation).edges > 0) {
                const auto found = window_.guideShapes_.find(ref.entityId.ToString());
                if (found != window_.guideShapes_.end()) {
                    const auto nearest = kachakacha::v2::kernel::NearestSurfaceEdge(found->second,
                        ref.hitPoint);
                    edge = nearest.HasValue() ? nearest.Value().index : -1;
                }
            }
            input_ = kachakacha::v2::app::WithSurfaceEditSurfacePick(input_, ref.entityId,
                ref.hitPoint, edge);
        } else if (entity->kind == EntityKind::Wire) {
            input_ = kachakacha::v2::app::WithSurfaceEditWirePick(input_, ref.entityId);
        }
    }
    window_.RefreshRightShelves();
    window_.viewport_->SetToolPickActive(true);
    window_.viewport_->SetToolPickToggle(true);
    MirrorToSelection();
    Refresh();
    window_.SetStatus(Text(std::string(kachakacha::v2::app::SurfaceEditLabelJa(operation))
        + ": 3D で面を押してください(縁は面の縁の近くを押す。押し直すと外れます)。"
          "Enter で確定、Esc でやめます。元の面はそのまま残ります。"));
}

void V2SurfaceEditTool::Choose(SurfaceEditOperation operation)
{
    input_ = kachakacha::v2::app::WithSurfaceEditOperation(input_, operation);
    MirrorToSelection();
    Refresh();
}

void V2SurfaceEditTool::End()
{
    active_ = false;
    surfaces_.clear();
    wires_.clear();
    outcome_ = kachakacha::v2::app::SurfaceEditOutcome{};
    if (window_.viewport_ != nullptr) {
        window_.viewport_->HideToolPreview();
        window_.viewport_->HideToolRoleLabels();
        window_.viewport_->SetToolPickActive(false);
        window_.viewport_->SetToolPickToggle(false);
        // 欄の印を選択に残さない。残すと、次に構えたときに勝手に欄へ入る。
        mirroring_ = true;
        window_.viewport_->SetSelection(kachakacha::v2::app::SelectionSet{});
        mirroring_ = false;
    }
    window_.ShowToolFooter(QString());
    window_.RefreshRightShelves();
    if (window_.surfaceAnalysis_ != nullptr) {
        window_.surfaceAnalysis_->Refresh();   // 下見の面の塗りを消す。
    }
}

void V2SurfaceEditTool::MirrorToSelection()
{
    kachakacha::v2::app::SelectionSet mirrored;
    const auto add = [&mirrored](const EntityId& id) {
        if (std::find(mirrored.entityIds.begin(), mirrored.entityIds.end(), id)
            != mirrored.entityIds.end()) {
            return;
        }
        mirrored.entityIds.push_back(id);
        kachakacha::v2::app::SelectionRef ref;
        ref.entityId = id;
        mirrored.ordered.push_back(ref);
    };
    for (const auto& pick : input_.edges) {
        add(pick.surface);
    }
    for (const auto& id : input_.surfaces) {
        add(id);
    }
    for (const auto& id : input_.wires) {
        add(id);
    }
    mirroring_ = true;
    window_.viewport_->SetSelection(std::move(mirrored));
    mirroring_ = false;
}

void V2SurfaceEditTool::HandleSelectionChanged()
{
    if (!active_ || mirroring_ || window_.viewport_ == nullptr) {
        return;
    }
    if (const auto pick = window_.viewport_->TakeLastToolPick(); pick.has_value()) {
        ApplyPick(*pick);
    }
    MirrorToSelection();   // 受けなかったもの(部品など)を選択に残さない。
    Refresh();
}

void V2SurfaceEditTool::ApplyPick(const EntityId& id)
{
    const auto* entity = window_.session_->GetDocument().FindEntity(id);
    if (entity == nullptr) {
        return;
    }
    if (entity->kind == EntityKind::Wire) {
        input_ = kachakacha::v2::app::WithSurfaceEditWirePick(input_, id);
        return;
    }
    if (entity->kind != EntityKind::GuideSurface) {
        return;
    }
    // 押した点(選択の最後に入ったその物の当たり)から、一番近い縁を決める。
    kachakacha::v2::geometry::Vector3 hit{};
    for (const auto& ref : window_.viewport_->Selection().ordered) {
        if (ref.entityId == id) {
            hit = ref.hitPoint;
        }
    }
    int edge = -1;
    if (kachakacha::v2::app::SurfaceEditSlotsFor(input_.operation).edges > 0) {
        const auto found = window_.guideShapes_.find(id.ToString());
        if (found != window_.guideShapes_.end()) {
            const auto nearest = kachakacha::v2::kernel::NearestSurfaceEdge(found->second, hit);
            if (nearest.HasValue()) {
                edge = nearest.Value().index;
            } else {
                window_.ReportDiagnostics(nearest.Diagnostics());
            }
        }
    }
    input_ = kachakacha::v2::app::WithSurfaceEditSurfacePick(input_, id, hit, edge);
}

std::string V2SurfaceEditTool::NameOf(const EntityId& id) const
{
    const auto* entity = id.IsNil() ? nullptr : window_.session_->GetDocument().FindEntity(id);
    return entity != nullptr && !entity->displayName.empty() ? entity->displayName
                                                              : std::string("名前のないもの");
}

void V2SurfaceEditTool::Refresh()
{
    RefreshPreview();
    RefreshDock();
    // 面の解析(出していれば)も下見の面を塗り直す。
    if (window_.surfaceAnalysis_ != nullptr) {
        window_.surfaceAnalysis_->Refresh();
    }
}

bool V2SurfaceEditTool::HandleKey(int key)
{
    if (!active_) {
        return false;
    }
    if (key == Qt::Key_Escape) {
        End();
        window_.SetStatus(QStringLiteral("面の編集: やめました。何も作っていません。"));
        return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        Confirm();
        return true;
    }
    return false;
}

namespace {

//! 窓が覚えている形(形状ガイドの面)の番号。無ければ無効な番号。
[[nodiscard]] kachakacha::v2::modeling::KernelShapeHandle ShapeOf(
    const std::map<std::string, kachakacha::v2::modeling::KernelShapeHandle>& shapes,
    const EntityId& id)
{
    const auto found = shapes.find(id.ToString());
    return found == shapes.end() ? kachakacha::v2::modeling::KernelShapeHandle{} : found->second;
}

} // namespace

//! 面ができる作り方(合わせる・つなぐ・整える・対称)。**実際に核で作る。**
void V2SurfaceEditTool::BuildSurfaces()
{
    using kachakacha::v2::kernel::SurfaceEditResult;
    const auto& tolerance = window_.session_->GetDocument().Snapshot().settings.tolerance;
    const auto accept = [this](const kachakacha::v2::base::Result<SurfaceEditResult>& made,
                            kachakacha::v2::domain::EditSurfaceDefinition definition,
                            std::vector<EntityId> inputs, const std::string& label,
                            const std::string& prefix) {
        if (!made.HasValue()) {
            outcome_.refusalJa = prefix + made.FirstSummaryJa();
            if (!made.Diagnostics().empty() && !made.Diagnostics().front().detailsJa.empty()) {
                outcome_.refusalJa += "(" + made.Diagnostics().front().detailsJa + ")";
            }
            return false;
        }
        surfaces_.push_back(BuiltSurface{made.Value().surface, std::move(definition),
            std::move(inputs), label});
        outcome_.noteJa += (outcome_.noteJa.empty() ? "" : " ") + made.Value().noteJa;
        return true;
    };
    kachakacha::v2::domain::EditSurfaceDefinition definition;
    definition.operation = static_cast<int>(input_.operation);
    switch (input_.operation) {
    case SurfaceEditOperation::Match:
    case SurfaceEditOperation::Bridge: {
        const auto& a = input_.edges[0];
        const auto& b = input_.edges[1];
        definition.surfaces = {a.surface, b.surface};
        definition.edgeIndices = {a.edgeIndex, b.edgeIndex};
        const bool match = input_.operation == SurfaceEditOperation::Match;
        definition.continuity = {static_cast<int>(input_.continuityA)};
        if (!match) {
            definition.continuity.push_back(static_cast<int>(input_.continuityB));
            definition.tension = input_.tension;
        }
        const auto made = match
            ? kachakacha::v2::kernel::MatchSurfaceEdge(ShapeOf(window_.guideShapes_, a.surface),
                  a.edgeIndex, ShapeOf(window_.guideShapes_, b.surface), b.edgeIndex,
                  input_.continuityA, tolerance)
            : kachakacha::v2::kernel::BridgeSurfaceEdges(ShapeOf(window_.guideShapes_, a.surface),
                  a.edgeIndex, input_.continuityA, ShapeOf(window_.guideShapes_, b.surface),
                  b.edgeIndex, input_.continuityB, input_.tension, tolerance);
        (void)accept(made, definition, {a.surface, b.surface},
            match ? "合わせた面" : "つないだ面", "");
        return;
    }
    case SurfaceEditOperation::Refit:
    case SurfaceEditOperation::Mirror: {
        const bool refit = input_.operation == SurfaceEditOperation::Refit;
        const auto plane = kachakacha::v2::app::MirrorPlaneFor(input_.mirrorPlane,
            window_.viewport_->WorkPlane().origin, window_.viewport_->WorkPlane().normal);
        for (const EntityId& id : input_.surfaces) {
            auto one = definition;
            one.surfaces = {id};
            one.toleranceMm = refit ? input_.toleranceMm : 0.0;
            one.planePoint = plane.point;
            one.planeNormal = plane.normal;
            const auto shape = ShapeOf(window_.guideShapes_, id);
            const auto made = refit
                ? kachakacha::v2::kernel::RefitSurface(shape, input_.toleranceMm, tolerance)
                : kachakacha::v2::kernel::MirrorSurface(shape, plane.point, plane.normal, tolerance);
            // 1 枚でも作れなければ、全部作らない(半分だけ作れたことにしない)。
            if (!accept(made, one, {id}, NameOf(id) + (refit ? "(整えた)" : "(対称)"),
                    NameOf(id) + ": ")) {
                surfaces_.clear();
                return;
            }
        }
        return;
    }
    case SurfaceEditOperation::IsoCurve:
    case SurfaceEditOperation::CurveOnSurface:
        return;
    }
}

//! 線ができる作り方(U/V 線・面へ投影)。
void V2SurfaceEditTool::BuildWires()
{
    const auto& tolerance = window_.session_->GetDocument().Snapshot().settings.tolerance;
    if (input_.operation == SurfaceEditOperation::IsoCurve) {
        for (const EntityId& id : input_.surfaces) {
            const auto made = kachakacha::v2::kernel::ExtractIsoCurves(
                ShapeOf(window_.guideShapes_, id), input_.isoDirection, input_.isoCount, tolerance);
            if (!made.HasValue()) {
                outcome_.refusalJa = NameOf(id) + ": " + made.FirstSummaryJa();
                wires_.clear();
                return;
            }
            wires_.insert(wires_.end(), made.Value().begin(), made.Value().end());
        }
        outcome_.noteJa = "線 " + std::to_string(wires_.size()) + " 本を取り出します。";
        return;
    }
    std::vector<std::vector<kachakacha::v2::geometry::CurveSegment>> inputs;
    for (const EntityId& id : input_.wires) {
        kachakacha::v2::app::SelectionSet one;
        one.entityIds.push_back(id);
        inputs.push_back(kachakacha::v2::app::SelectedCurves(one, window_.session_->Scene()));
    }
    const auto made = kachakacha::v2::kernel::ProjectWiresOntoSurface(
        ShapeOf(window_.guideShapes_, input_.surfaces.front()), inputs,
        window_.viewport_->WorkPlane().normal * -1.0, tolerance);
    if (!made.HasValue()) {
        outcome_.refusalJa = made.FirstSummaryJa();
        return;
    }
    wires_ = made.Value();
    outcome_.noteJa = "線 " + std::to_string(input_.wires.size()) + " 本を面へ落とし、面の上の曲線 "
        + std::to_string(wires_.size()) + " 本にします(折れ線にしません)。";
}

//! そろっていれば **実際に作って** 下見に出す。文書へは書かない。
void V2SurfaceEditTool::RefreshPreview()
{
    outcome_ = kachakacha::v2::app::SurfaceEditOutcome{};
    surfaces_.clear();
    wires_.clear();
    window_.viewport_->HideToolPreview();
    if (!active_ || !kachakacha::v2::app::SurfaceEditReadyToBuild(input_)) {
        return;
    }
    outcome_.evaluated = true;
    const bool wireOutput = input_.operation == SurfaceEditOperation::IsoCurve
        || input_.operation == SurfaceEditOperation::CurveOnSurface;
    if (wireOutput) {
        BuildWires();
    } else {
        BuildSurfaces();
    }
    outcome_.outputs = wireOutput ? wires_.size() : surfaces_.size();
    outcome_.available = outcome_.outputs > 0 && outcome_.refusalJa.empty();
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> lines;
    for (const BuiltSurface& built : surfaces_) {
        const auto loops = kachakacha::v2::app::SurfacePreviewLines(built.surface.samples,
            built.surface.boundary);
        lines.insert(lines.end(), loops.begin(), loops.end());
    }
    for (const auto& wire : wires_) {
        lines.push_back(kachakacha::v2::geometry::SampleChain(wire, 0.01));
    }
    // 指した縁も線で出す(どの縁を合わせる・つなぐのかを 3D で見せる)。
    for (const auto& pick : input_.edges) {
        const auto edge = kachakacha::v2::kernel::SurfaceEdgeAt(
            ShapeOf(window_.guideShapes_, pick.surface), pick.edgeIndex);
        if (edge.HasValue()) {
            lines.push_back(edge.Value().polyline);
        }
    }
    if (!lines.empty()) {
        window_.viewport_->ShowToolPreview(lines);
    }
}

std::vector<kachakacha::v2::modeling::KernelShapeHandle> V2SurfaceEditTool::PreviewSurfaces() const
{
    std::vector<kachakacha::v2::modeling::KernelShapeHandle> handles;
    for (const BuiltSurface& built : surfaces_) {
        handles.push_back(built.surface.handle);
    }
    return handles;
}

void V2SurfaceEditTool::RefreshDock()
{
    std::vector<V2SurfaceEditDock::Entry> entries;
    std::vector<kachakacha::v2::app::ToolRoleLabel> labels;
    for (std::size_t index = 0; index < input_.edges.size(); ++index) {
        const auto& pick = input_.edges[index];
        const std::string slot = kachakacha::v2::app::SurfaceEdgeSlotNameJa(input_.operation, index);
        const std::string edge = pick.edgeIndex >= 0
            ? " の縁 " + std::to_string(pick.edgeIndex + 1) : " (縁が未定)";
        entries.push_back({Text(slot), Text(NameOf(pick.surface) + edge), pick.surface});
        labels.push_back({pick.surface, slot});
    }
    const bool onto = input_.operation == SurfaceEditOperation::CurveOnSurface;
    for (std::size_t index = 0; index < input_.surfaces.size(); ++index) {
        const std::string slot = onto ? std::string("落とす先の面")
                                      : "面 " + std::to_string(index + 1);
        entries.push_back({Text(slot), Text(NameOf(input_.surfaces[index])), input_.surfaces[index]});
        labels.push_back({input_.surfaces[index], slot});
    }
    for (std::size_t index = 0; index < input_.wires.size(); ++index) {
        const std::string slot = "線 " + std::to_string(index + 1);
        entries.push_back({Text(slot), Text(NameOf(input_.wires[index])), input_.wires[index]});
        labels.push_back({input_.wires[index], slot});
    }
    const bool previewShown = !window_.viewport_->ToolPreview().empty();
    std::vector<QString> lines;
    for (const std::string& line :
        kachakacha::v2::app::SurfaceEditStatusLinesJa(input_, outcome_, previewShown)) {
        lines.push_back(Text(line));
    }
    dock_->ShowInput(input_, entries, lines, outcome_.available);
    window_.ShowToolFooter(active_
            ? Text(kachakacha::v2::app::SurfaceEditFooterLine(input_, outcome_, previewShown))
            : QString());
    window_.ShowRoleLabels(labels);
}

//! 面を 1 枚、文書へ入れる(EditSurface の Feature + 形状ガイドの面)。形は窓が覚える。
bool V2SurfaceEditTool::AddSurfaceFeature(const BuiltSurface& built)
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;
    using kachakacha::v2::domain::FeatureType;
    Feature feature;
    feature.id = window_.ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
    feature.type = FeatureType::EditSurface;
    feature.displayName = built.label;
    feature.inputEntityIds = built.inputs;
    feature.definition = built.definition;
    Entity entity;
    entity.id = window_.ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
    entity.kind = EntityKind::GuideSurface;
    entity.displayName = kachakacha::v2::app::UniqueDisplayName(
        window_.session_->GetDocument().Snapshot(), EntityKind::GuideSurface, built.label);
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{"surface", entity.id, EntityKind::GuideSurface});
    const auto added = window_.session_->GetDocument().Run(
        AddFeatureCommand(feature, {entity}, built.label));
    if (!added.committed) {
        window_.ReportDiagnostics(added.diagnostics);
        return false;
    }
    window_.guideShapes_[entity.id.ToString()] = built.surface.handle;
    window_.guideEdges_[entity.id.ToString()] = built.surface.boundary;
    window_.guideSamples_[entity.id.ToString()] = built.surface.samples;
    return true;
}

//! 線を 1 本、文書へ入れる。U/V 線はふつうの線、面へ投影は既存と同じ「曲面へ投影」。
bool V2SurfaceEditTool::AddWireFeature(
    const std::vector<kachakacha::v2::geometry::CurveSegment>& wire,
    const std::vector<EntityId>& inputs, bool iso)
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;
    using kachakacha::v2::domain::FeatureType;
    const std::string label = iso ? "面の U/V 線" : "曲面へ投影";
    Feature feature;
    feature.id = window_.ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
    feature.type = iso ? FeatureType::CreateWire : FeatureType::ProjectWire;
    feature.displayName = label;
    feature.inputEntityIds = inputs;
    kachakacha::v2::domain::CreateWireDefinition definition;
    definition.segments = wire;
    for (std::size_t index = 0; index < wire.size(); ++index) {
        definition.segmentIds.push_back(
            window_.ids_->NextTyped<kachakacha::v2::base::IdKind::Segment>());
    }
    feature.definition = std::move(definition);
    Entity entity;
    entity.id = window_.ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
    entity.kind = EntityKind::Wire;
    entity.displayName = kachakacha::v2::app::UniqueDisplayName(
        window_.session_->GetDocument().Snapshot(), EntityKind::Wire, label);
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});
    const auto added = window_.session_->GetDocument().Run(
        AddFeatureCommand(feature, {entity}, label));
    if (!added.committed) {
        window_.ReportDiagnostics(added.diagnostics);
        return false;
    }
    return true;
}

//! 確定。**下見に使った形をそのまま**文書へ入れる。何枚でも 1 回の取り消しで戻る。
void V2SurfaceEditTool::Confirm()
{
    if (!outcome_.available) {
        window_.SetStatus(Text(std::string(kachakacha::v2::app::SurfaceEditLabelJa(input_.operation))
            + ": まだ作れません。右の棚の「4. 状態」を見てください。"));
        RefreshDock();
        return;
    }
    const std::string label(kachakacha::v2::app::SurfaceEditLabelJa(input_.operation));
    auto& document = window_.session_->GetDocument();
    document.BeginCompound(label);
    bool ok = true;
    for (const BuiltSurface& built : surfaces_) {
        ok = ok && AddSurfaceFeature(built);
    }
    std::vector<EntityId> inputs = input_.surfaces;
    inputs.insert(inputs.end(), input_.wires.begin(), input_.wires.end());
    const bool iso = input_.operation == SurfaceEditOperation::IsoCurve;
    for (const auto& wire : wires_) {
        ok = ok && AddWireFeature(wire, inputs, iso);
    }
    if (!ok) {
        // 1 つでも入らなければ全部取り消す。半分だけ作れたことにしない。
        document.AbortCompound();
        window_.RebuildKernelShapes();
        return;
    }
    document.EndCompound();
    const std::size_t made = outcome_.outputs;
    const std::string note = outcome_.noteJa;
    End();
    window_.AdoptCurrentDocument();
    window_.RefreshGuideTable();
    window_.SetStatus(Text(label + ": " + std::to_string(made) + " 個作りました。" + note));
}

//! 開き直したときの作り直し。作ったときと同じ核の道を通す。
bool V2SurfaceEditTool::Rebuild(const kachakacha::v2::domain::Feature& feature,
    const EntityId& output)
{
    const auto* definition =
        std::get_if<kachakacha::v2::domain::EditSurfaceDefinition>(&feature.definition);
    if (definition == nullptr || definition->surfaces.empty()) {
        return false;
    }
    const auto& tolerance = window_.session_->GetDocument().Snapshot().settings.tolerance;
    const auto continuity = [definition](std::size_t index) {
        return index < definition->continuity.size()
            ? static_cast<kachakacha::v2::modeling::SurfaceContinuity>(definition->continuity[index])
            : kachakacha::v2::modeling::SurfaceContinuity::G0;
    };
    const auto edge = [definition](std::size_t index) {
        return index < definition->edgeIndices.size() ? definition->edgeIndices[index] : -1;
    };
    const auto first = ShapeOf(window_.guideShapes_, definition->surfaces.front());
    const auto second = definition->surfaces.size() > 1
        ? ShapeOf(window_.guideShapes_, definition->surfaces[1])
        : kachakacha::v2::modeling::KernelShapeHandle{};
    kachakacha::v2::base::Result<kachakacha::v2::kernel::SurfaceEditResult> made =
        kachakacha::v2::base::Result<kachakacha::v2::kernel::SurfaceEditResult>::Failure(
            kachakacha::v2::base::MakeError("KER-D001", "知らない面の編集です。", {}));
    switch (static_cast<SurfaceEditOperation>(definition->operation)) {
    case SurfaceEditOperation::Match:
        made = kachakacha::v2::kernel::MatchSurfaceEdge(first, edge(0), second, edge(1),
            continuity(0), tolerance);
        break;
    case SurfaceEditOperation::Bridge:
        made = kachakacha::v2::kernel::BridgeSurfaceEdges(first, edge(0), continuity(0), second,
            edge(1), continuity(1), definition->tension, tolerance);
        break;
    case SurfaceEditOperation::Refit:
        made = kachakacha::v2::kernel::RefitSurface(first, definition->toleranceMm, tolerance);
        break;
    case SurfaceEditOperation::Mirror:
        made = kachakacha::v2::kernel::MirrorSurface(first, definition->planePoint,
            definition->planeNormal, tolerance);
        break;
    case SurfaceEditOperation::IsoCurve:
    case SurfaceEditOperation::CurveOnSurface:
        break;
    }
    if (!made.HasValue()) {
        window_.ReportDiagnostics(made.Diagnostics());
        return false;
    }
    window_.guideShapes_[output.ToString()] = made.Value().surface.handle;
    window_.guideEdges_[output.ToString()] = made.Value().surface.boundary;
    window_.guideSamples_[output.ToString()] = made.Value().surface.samples;
    return true;
}

bool V2SurfaceEditTool::ProjectSelectionExactly(const EntityId& surfaceId)
{
    const auto shape = ShapeOf(window_.guideShapes_, surfaceId);
    if (!shape.Valid()) {
        return false;
    }
    // 線ごとに 1 本のワイヤーとして落とす(ばらばらの曲線にしない)。
    std::vector<std::vector<kachakacha::v2::geometry::CurveSegment>> groups;
    std::vector<EntityId> inputs;
    for (const EntityId& id : window_.viewport_->Selection().entityIds) {
        const auto* entity = window_.session_->GetDocument().FindEntity(id);
        if (entity == nullptr || entity->kind != EntityKind::Wire) {
            continue;
        }
        kachakacha::v2::app::SelectionSet one;
        one.entityIds.push_back(id);
        groups.push_back(kachakacha::v2::app::SelectedCurves(one, window_.session_->Scene()));
        inputs.push_back(id);
    }
    if (groups.empty()) {
        return false;
    }
    const auto& tolerance = window_.session_->GetDocument().Snapshot().settings.tolerance;
    const auto made = kachakacha::v2::kernel::ProjectWiresOntoSurface(shape, groups,
        window_.viewport_->WorkPlane().normal, tolerance);
    if (!made.HasValue()) {
        return false;   // 窓が標本から折れ線で落とす道へ戻す。
    }
    inputs.push_back(surfaceId);
    auto& document = window_.session_->GetDocument();
    document.BeginCompound("曲面へ投影");
    bool ok = true;
    for (const auto& wire : made.Value()) {
        ok = ok && AddWireFeature(wire, inputs, false);
    }
    if (!ok) {
        document.AbortCompound();
        return true;   // 入れられなかった理由は AddWireFeature が出している。
    }
    document.EndCompound();
    window_.AdoptCurrentDocument();
    window_.SetStatus(QStringLiteral("曲面へ投影: %1本を面へ落とし、面の上の曲線 %2 本にしました"
                                     "(折れ線にしていません)。元の線は残しています。")
            .arg(static_cast<int>(groups.size()))
            .arg(static_cast<int>(made.Value().size())));
    return true;
}
