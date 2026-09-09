//! 形をつくるコマンド(V2MainWindow の一部)。押し出し・かご・足す引く・固定。
//!
//! 思想。**面が張れるかどうかを、面を張る前に判定する。**
//! だからここは必ず2段になる。まず core に調べさせ、通ったら kernel に作らせる。
//! 調べずに作らせると、失敗したときに「何が悪いのか」を言えない。
//!
//! もうひとつ。**出来た立体の形は文書に持たない。**
//! 持つと、入力を変えたのに形が古いまま、という食い違いが起きる。
//! 文書は「どう作ったか」だけを持ち、形は作り直す。

#include "V2MainWindow.h"

#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/kernel/OcctExtrude.h"
#include "kachakacha/kernel/OcctWireCage.h"
#include "kachakacha/modeling/ExtrudeInput.h"
#include "kachakacha/modeling/WireCage.h"

#include <string>
#include <utility>
#include <vector>

namespace {

using kachakacha::v2::base::EntityId;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::modeling::SnapCurve;

//! 選んでいるワイヤーを、押し出しの輪郭にまとめる。
//! Entity ごとに1つの輪郭にする。選んだ順は保つ。
[[nodiscard]] std::vector<kachakacha::v2::modeling::ExtrudeProfile> ProfilesOf(
    const std::vector<EntityId>& entityIds,
    const kachakacha::v2::modeling::SnapScene& scene,
    const kachakacha::v2::geometry::GeometryTolerance& tolerance)
{
    std::vector<kachakacha::v2::modeling::ExtrudeProfile> profiles;
    for (const EntityId& id : entityIds) {
        kachakacha::v2::modeling::ExtrudeProfile profile;
        profile.sourceEntityId = id;
        for (const SnapCurve& curve : scene.curves) {
            if (curve.entityId != id) {
                continue;
            }
            profile.segments.push_back(curve.segment);
            profile.segmentIds.push_back(curve.segmentId);
        }
        if (!profile.segments.empty()) {
            // 閉じているかを立てておく。立てないと、押し出しはいつまでも
            // 「開いた輪郭からは部品を作れません」と断る。実際にそうなった。
            profile.closed = kachakacha::v2::geometry::SegmentsFormClosedLoop(
                profile.segments, tolerance);
            profiles.push_back(std::move(profile));
        }
    }
    return profiles;
}

} // namespace

bool V2MainWindow::IsPartCommand(std::string_view id)
{
    return id == "part.extrude" || id == "part.from_wire_cage"
        || id == "part.boolean_add" || id == "part.boolean_cut";
}

void V2MainWindow::RunPartCommand(std::string_view id)
{
    if (id == "part.extrude") {
        RunExtrude();
        return;
    }
    if (id == "part.from_wire_cage") {
        RunWireCage();
        return;
    }
    RunBoolean(id == "part.boolean_cut");
}

void V2MainWindow::RunExtrude()
{
    using kachakacha::v2::modeling::AnalyzeExtrudeRequest;
    using kachakacha::v2::modeling::ExtrudeDirectionMode;
    using kachakacha::v2::modeling::ExtrudeExtentMode;
    using kachakacha::v2::modeling::ExtrudeRequest;

    const auto& selection = viewport_->Selection();
    ExtrudeRequest request;
    request.profiles = ProfilesOf(selection.entityIds, session_->Scene(),
        session_->GetDocument().Snapshot().settings.tolerance);
    if (request.profiles.empty()) {
        SetStatus(QStringLiteral("押し出し: 先に閉じたワイヤーを選んでください。"));
        return;
    }
    request.directionMode = ExtrudeDirectionMode::WorkPlaneNormal;
    request.workPlane = viewport_->WorkPlane();
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = extrudeDistanceMm_;
    request.outputs.part = true;
    request.outputs.endProfileWire = true;
    request.outputs.sideBoundaryWires = true;

    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    // まず調べる。通らないものは作らせない。作らせてから断ると理由を言えない。
    const auto analysis = AnalyzeExtrudeRequest(request, tolerance);
    if (!analysis.HasValue()) {
        ReportDiagnostics(analysis.Diagnostics());
        return;
    }
    const auto built = kachakacha::v2::kernel::BuildExtrude(request, analysis.Value(),
        tolerance);
    if (!built.HasValue()) {
        ReportDiagnostics(built.Diagnostics());
        return;
    }
    if (built.Value().parts.empty()) {
        SetStatus(QStringLiteral("押し出し: 立体になりませんでした。"));
        return;
    }
    kachakacha::v2::domain::ExtrudeDefinition definition;
    definition.profiles = selection.entityIds;
    definition.direction = request.workPlane.normal;
    definition.distance.value = extrudeDistanceMm_;
    definition.distance.kind = kachakacha::v2::geometry::QuantityKind::Length;
    std::vector<CurveSegment> edges;
    for (const auto& wire : built.Value().endProfileWires) {
        edges.insert(edges.end(), wire.begin(), wire.end());
    }
    for (const auto& wire : built.Value().sideBoundaryWires) {
        edges.insert(edges.end(), wire.begin(), wire.end());
    }
    AddPartFeature(kachakacha::v2::domain::FeatureType::Extrude, std::move(definition),
        built.Value().parts.front().handle, edges, "押し出し");
    SetStatus(QStringLiteral("押し出し: 厚み %1 mm の部品を作りました(体積 %2 mm3)。")
            .arg(extrudeDistanceMm_)
            .arg(built.Value().totalVolumeMm3));
}

void V2MainWindow::RunWireCage()
{
    using kachakacha::v2::modeling::AnalyzeWireCage;
    using kachakacha::v2::modeling::CageEdgeInput;
    using kachakacha::v2::modeling::PlanWireCageParts;

    const auto& selection = viewport_->Selection();
    std::vector<CageEdgeInput> inputs;
    for (const auto& curve : session_->Scene().curves) {
        if (!kachakacha::v2::app::IsSelected(selection, curve.entityId)) {
            continue;
        }
        inputs.push_back(CageEdgeInput{curve.entityId, curve.segmentId, curve.segment});
    }
    if (inputs.size() < 3) {
        SetStatus(QStringLiteral(
            "ワイヤー群から部品: 閉じたかごになる線を3本以上選んでください。"));
        return;
    }
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    // まず調べる。閉じていないかごは、ここで断る。
    const auto analysis = AnalyzeWireCage(inputs, tolerance);
    if (!analysis.HasValue()) {
        ReportDiagnostics(analysis.Diagnostics());
        return;
    }
    // 出来たシェルを全部部品にする。1シェル=1部品。まとめない。
    std::vector<std::size_t> shells;
    for (std::size_t index = 0; index < analysis.Value().shells.size(); ++index) {
        shells.push_back(index);
    }
    const auto planned = PlanWireCageParts(analysis.Value(), shells);
    if (!planned.HasValue()) {
        ReportDiagnostics(planned.Diagnostics());
        return;
    }
    const auto built = kachakacha::v2::kernel::BuildWireCageParts(inputs, analysis.Value(),
        planned.Value(), tolerance);
    if (!built.HasValue()) {
        ReportDiagnostics(built.Diagnostics());
        return;
    }
    if (built.Value().empty()) {
        SetStatus(QStringLiteral("ワイヤー群から部品: 立体になりませんでした。"));
        return;
    }
    kachakacha::v2::domain::CreatePartFromWireCageDefinition definition;
    definition.wires = selection.entityIds;
    std::vector<CurveSegment> edges;
    for (const auto& input : inputs) {
        edges.push_back(input.segment);
    }
    AddPartFeature(kachakacha::v2::domain::FeatureType::CreatePartFromWireCage,
        std::move(definition), built.Value().front().handle, edges, "かごから部品");
    SetStatus(QStringLiteral("ワイヤー群から部品: %1個の部品を作りました。")
            .arg(static_cast<int>(built.Value().size())));
}

void V2MainWindow::RunBoolean(bool cut)
{
    // 足す・引くは相手を明示して選ぶ。近い部品を勝手に選ばない(§8.4)。
    const auto& selection = viewport_->Selection();
    std::vector<EntityId> parts;
    for (const auto& id : selection.entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity != nullptr
            && entity->kind == kachakacha::v2::domain::EntityKind::Part) {
            parts.push_back(id);
        }
    }
    if (parts.size() < 2) {
        SetStatus(QStringLiteral("%1: 相手を明示して、部品を2つ以上選んでください。")
                .arg(cut ? QStringLiteral("引く") : QStringLiteral("足す")));
        return;
    }
    SetStatus(QStringLiteral("%1: 部品どうしの足し引きは、押し出しの足す・引くで行います。"
                             "いまは押し出しから相手を選んでください。")
            .arg(cut ? QStringLiteral("引く") : QStringLiteral("足す")));
}

void V2MainWindow::AddPartFeature(kachakacha::v2::domain::FeatureType type,
    kachakacha::v2::domain::FeatureDefinition definition,
    kachakacha::v2::modeling::KernelShapeHandle handle,
    const std::vector<kachakacha::v2::geometry::CurveSegment>& edges,
    const char* labelJa)
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;

    Feature feature;
    feature.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
    feature.type = type;
    feature.displayName = labelJa;
    feature.inputEntityIds = viewport_->Selection().entityIds;
    feature.definition = std::move(definition);

    Entity entity;
    entity.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
    entity.kind = EntityKind::Part;
    entity.displayName = labelJa;
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{"part", entity.id, EntityKind::Part});

    const auto added = session_->GetDocument().Run(
        AddFeatureCommand(feature, {entity}, labelJa));
    if (!added.committed) {
        ReportDiagnostics(added.diagnostics);
        return;
    }
    // 形そのものは文書に持たない。持つと入力と食い違う。
    // 出来た形の handle と、見せるための辺だけを画面側で覚えておく。
    partShapes_[entity.id.ToString()] = handle;
    partEdges_[entity.id.ToString()] = edges;
    AdoptCurrentDocument();
    RefreshPartEdges();
}

void V2MainWindow::RefreshPartEdges()
{
    // 部品の辺を場面へ足す。立体そのものはまだ描かないので、輪郭で見せる。
    // 描いていないものを「描いた」と言わないため、辺は補助線として出す。
    auto scene = session_->Scene();
    for (const auto& entry : partEdges_) {
        const auto id = kachakacha::v2::base::EntityId::Parse(entry.first);
        if (!id.has_value()) {
            continue;
        }
        for (const auto& segment : entry.second) {
            scene.curves.push_back(kachakacha::v2::modeling::SnapCurve{*id,
                ids_->NextTyped<kachakacha::v2::base::IdKind::Segment>(), segment, false});
        }
    }
    session_->SetScene(std::move(scene));
    viewport_->update();
}
