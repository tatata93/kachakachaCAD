//! 線の編集コマンド(V2MainWindow の一部)。
//!
//! 分割・結合・端点一致・接線接続・曲率接続・面取り・丸め・オフセットを、
//! すべて同じ道で通す。ここでやるのは3つだけである。
//!   1. 選んでいる線を、選んだ順のまま core へ渡す
//!   2. core が計算した結果で Feature を1つ作る
//!   3. 使い切った線を消す
//! 幾何の判断は1つもしない。判断は core にある。

#include "V2MainWindow.h"

#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/document/FeatureReevaluation.h"
#include "kachakacha/geometry/CurveProjection.h"

#include <string>
#include <utility>
#include <vector>

namespace {

using kachakacha::v2::domain::WireTransformMethod;

//! 台帳のIDと編集の対応。ここに無いものは線の編集ではない。
struct WireEditBinding {
    std::string_view commandId;
    WireTransformMethod method;
    const char* labelJa;
    //! 使い切ったら元の線を消すか。結合は消す。オフセットは残す。
    bool consumesInputs;
    //! 1本目だけを使い切るか。分割の刃(2本目以降)は残す。
    //! 残さないと、切っただけで刃が消える。実際にそうなっていた。
    bool consumesFirstOnly;
    //! 大きさ(面取り量・丸め半径・オフセット距離)が要るか。
    bool needsSize;
};

constexpr WireEditBinding kWireEdits[] = {
    {"wire.split", WireTransformMethod::Split, "分割", true, true, false},
    {"wire.join", WireTransformMethod::Join, "結合", true, false, false},
    {"wire.coincident", WireTransformMethod::Coincident, "端点一致", true, false, false},
    {"wire.tangent", WireTransformMethod::Tangent, "接線接続", true, false, false},
    {"wire.curvature", WireTransformMethod::Curvature, "曲率接続", true, false, false},
    {"wire.chamfer", WireTransformMethod::Chamfer, "C面取り", true, false, true},
    {"wire.fillet", WireTransformMethod::Fillet, "R丸め", true, false, true},
};

[[nodiscard]] const WireEditBinding* FindWireEdit(std::string_view id)
{
    for (const WireEditBinding& binding : kWireEdits) {
        if (binding.commandId == id) {
            return &binding;
        }
    }
    return nullptr;
}

} // namespace

bool V2MainWindow::IsWireEditCommand(std::string_view id)
{
    return FindWireEdit(id) != nullptr || id == "wire.project";
}

void V2MainWindow::ProjectSelectedWires()
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;
    using kachakacha::v2::domain::FeatureType;
    using kachakacha::v2::geometry::ProjectCurvesOntoPlane;
    using kachakacha::v2::geometry::ProjectionPlane;

    const auto& selection = viewport_->Selection();
    const auto inputs = kachakacha::v2::app::SelectedCurves(selection, session_->Scene());
    if (inputs.empty()) {
        SetStatus(QStringLiteral("面へ投影: 先に線を選んでください。"));
        return;
    }
    // 落とす先は、いまの作業平面。どの面へ落としたのかが後から分かるように、
    // 元の線は消さずに残す。
    ProjectionPlane plane;
    plane.origin = viewport_->WorkPlane().origin;
    plane.normal = viewport_->WorkPlane().normal;
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    const auto projected = ProjectCurvesOntoPlane(inputs, plane, tolerance);
    if (!projected.HasValue()) {
        ReportDiagnostics(projected.Diagnostics());
        return;
    }

    Feature feature;
    feature.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
    feature.type = FeatureType::ProjectWire;
    feature.displayName = "面へ投影";
    feature.inputEntityIds = selection.entityIds;
    kachakacha::v2::domain::CreateWireDefinition wire;
    wire.segments = projected.Value();
    for (std::size_t index = 0; index < wire.segments.size(); ++index) {
        wire.segmentIds.push_back(ids_->NextTyped<kachakacha::v2::base::IdKind::Segment>());
    }
    feature.definition = std::move(wire);

    Entity entity;
    entity.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
    entity.kind = EntityKind::Wire;
    entity.displayName = "面へ投影";
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});

    const auto added = session_->GetDocument().Run(
        AddFeatureCommand(feature, {entity}, "面へ投影"));
    if (!added.committed) {
        ReportDiagnostics(added.diagnostics);
        return;
    }
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("面へ投影: %1本を作業平面へ落としました。元の線は残しています。")
            .arg(static_cast<int>(projected.Value().size())));
}

void V2MainWindow::RunWireEditCommand(std::string_view id)
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::document::RemoveFeatureCommand;
    using kachakacha::v2::document::RemovePolicy;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;
    using kachakacha::v2::domain::FeatureType;
    using kachakacha::v2::domain::TransformWireDefinition;

    if (id == "wire.project") {
        ProjectSelectedWires();
        return;
    }
    const WireEditBinding* binding = FindWireEdit(id);
    if (binding == nullptr) {
        return;
    }
    const auto& selection = viewport_->Selection();
    const auto inputs = kachakacha::v2::app::SelectedCurves(selection, session_->Scene());
    if (inputs.empty()) {
        SetStatus(QStringLiteral("%1: 先に線を選んでください。")
                .arg(QString::fromUtf8(binding->labelJa)));
        return;
    }
    TransformWireDefinition definition;
    definition.method = binding->method;
    if (binding->needsSize) {
        definition.scalarArgument.value = wireEditSizeMm_;
        definition.scalarArgument.expression = std::to_string(wireEditSizeMm_);
        definition.scalarArgument.kind = kachakacha::v2::geometry::QuantityKind::Length;
    }
    const auto computed =
        kachakacha::v2::document::EvaluateWireTransform(definition, inputs);
    if (!computed.HasValue()) {
        ReportDiagnostics(computed.Diagnostics());
        return;
    }

    Feature feature;
    feature.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
    feature.type = FeatureType::TransformWire;
    feature.displayName = binding->labelJa;
    feature.inputEntityIds = selection.entityIds;
    // 計算した形をそのまま持たせる。持たせないと、開き直したときに形が出ない。
    kachakacha::v2::domain::CreateWireDefinition wire;
    wire.segments = computed.Value();
    for (std::size_t index = 0; index < wire.segments.size(); ++index) {
        wire.segmentIds.push_back(
            ids_->NextTyped<kachakacha::v2::base::IdKind::Segment>());
    }
    feature.definition = std::move(wire);

    Entity entity;
    entity.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
    entity.kind = EntityKind::Wire;
    entity.displayName = binding->labelJa;
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});

    const auto added = session_->GetDocument().Run(
        AddFeatureCommand(feature, {entity}, binding->labelJa));
    if (!added.committed) {
        ReportDiagnostics(added.diagnostics);
        return;
    }
    if (binding->consumesInputs) {
        std::vector<kachakacha::v2::base::EntityId> consumed = selection.entityIds;
        if (binding->consumesFirstOnly && !consumed.empty()) {
            // 分割は1本目を切るだけ。刃にした線は残す。
            consumed.resize(1);
        }
        RemoveConsumedWires(consumed);
    }
    AdoptCurrentDocument();
    if (binding->consumesFirstOnly) {
        SetStatus(QStringLiteral("%1: 1本目を%2本にしました。刃にした線は残っています。")
                .arg(QString::fromUtf8(binding->labelJa))
                .arg(static_cast<int>(computed.Value().size())));
        return;
    }
    SetStatus(QStringLiteral("%1: %2本の線から%3本にしました。")
            .arg(QString::fromUtf8(binding->labelJa))
            .arg(static_cast<int>(inputs.size()))
            .arg(static_cast<int>(computed.Value().size())));
}

void V2MainWindow::RemoveConsumedWires(
    const std::vector<kachakacha::v2::base::EntityId>& entityIds)
{
    using kachakacha::v2::document::RemoveFeatureCommand;
    using kachakacha::v2::document::RemovePolicy;
    // 使い切った線を消す。作った Feature が下流にいるので、まとめて消す指定にする。
    // 断られたら残す。黙って消さない。
    for (const auto& id : entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity == nullptr) {
            continue;
        }
        const auto removed = session_->GetDocument().Run(RemoveFeatureCommand(
            entity->createdBy, RemovePolicy::RefuseIfUsed, "使い切った線を消す"));
        if (!removed.committed) {
            // 下流がいるので消せない。表示だけ消して、形は残す。
            (void)session_->GetDocument().Run(
                kachakacha::v2::document::SetVisibilityCommand({id},
                    kachakacha::v2::domain::Visibility::Hidden));
        }
    }
}

void V2MainWindow::AdoptCurrentDocument()
{
    // 文書が変わったので、場面を作り直して選択を掃除する。
    session_->SetScene(kachakacha::v2::app::RebuildSceneKeepingView(session_->Scene(),
        session_->GetDocument().Snapshot(), *ids_));
    viewport_->PruneSelection();
    RefreshEntityList();
    RefreshExportCounts();
    RefreshCommandVisibility();
    viewport_->update();
}
