//! 線の編集コマンド(V2MainWindow の一部)。
//!
//! 分割・結合・端点一致・接線接続・曲率接続・面取り・丸め・オフセットを、
//! すべて同じ道で通す。ここでやるのは3つだけである。
//!   1. 選んでいる線を、選んだ順のまま core へ渡す
//!   2. core が計算した結果で Feature を1つ作る
//!   3. 使い切った線を消す
//! 幾何の判断は1つもしない。判断は core にある。

#include "V2MainWindow.h"

#include "kachakacha/app/CommandParameters.h"
#include "kachakacha/app/DirectWireEntry.h"
#include "kachakacha/app/IntersectionPoints.h"
#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/fabrication/SurfaceProjection.h"
#include "kachakacha/document/FeatureReevaluation.h"
#include "kachakacha/geometry/CurveProjection.h"
#include "kachakacha/geometry/Measurement.h"

#include <optional>
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
    // オフセットは元の線を残す(V1 と同じ)。距離は数の棚「オフセット距離」。
    {"wire.offset", WireTransformMethod::Offset, "オフセット", false, false, true},
    {"wire.meet_lines", WireTransformMethod::MeetLines, "2線を交点まで", true, false, false},
    {"wire.corner_chamfer", WireTransformMethod::CornerChamfer, "角の加工(落とす)", true, false, true},
    {"wire.corner_fillet", WireTransformMethod::CornerFillet, "角の加工(丸める)", true, false, true},
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
    return FindWireEdit(id) != nullptr || id == "wire.project"
        || id == "wire.project_surface" || id == "wire.trim" || id == "wire.extend"
        || id == "wire.intersection_points" || id == "wire.set_datum"
        || id == "wire.clear_datum";
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

void V2MainWindow::ProjectSelectedWiresOntoSurface()
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;
    using kachakacha::v2::domain::FeatureType;

    // 曲がった面に窓を開けるための道。平らに描いた線を、作業平面の向きに沿って
    // 形状ガイドの面へ落とす。落ちた線は折れ線で、元の線は残す。
    const auto& selection = viewport_->Selection();
    const auto inputs = kachakacha::v2::app::SelectedCurves(selection, session_->Scene());
    kachakacha::v2::base::EntityId surfaceId;
    for (const auto& id : selection.entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity != nullptr && entity->kind == EntityKind::GuideSurface) {
            surfaceId = id;
        }
    }
    const auto samples = guideSamples_.find(surfaceId.ToString());
    if (inputs.empty() || surfaceId.IsNil() || samples == guideSamples_.end()) {
        SetStatus(QStringLiteral(
            "曲面へ投影: 線を1つ以上と、落とす先の形状ガイドの面を1つ選んでください。"));
        return;
    }
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    const auto projected = kachakacha::v2::fabrication::ProjectCurvesOntoSampledSurface(
        samples->second, inputs, viewport_->WorkPlane().normal, tolerance.interactiveJoinMm);
    if (!projected.HasValue()) {
        ReportDiagnostics(projected.Diagnostics());
        return;
    }

    Feature feature;
    feature.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
    feature.type = FeatureType::ProjectWire;
    feature.displayName = "曲面へ投影";
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
    entity.displayName = "曲面へ投影";
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});

    const auto added = session_->GetDocument().Run(
        AddFeatureCommand(feature, {entity}, "曲面へ投影"));
    if (!added.committed) {
        ReportDiagnostics(added.diagnostics);
        return;
    }
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("曲面へ投影: %1本を面へ落とし、%2本の折れ線にしました。"
                             "元の線は残しています。")
            .arg(static_cast<int>(inputs.size()))
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
    if (id == "wire.project_surface") {
        ProjectSelectedWiresOntoSurface();
        return;
    }
    if (id == "wire.trim" || id == "wire.extend") {
        BeginTrimOrExtend(id == "wire.trim");
        return;
    }
    if (id == "wire.intersection_points") {
        MakeIntersectionPoints();
        return;
    }
    if (id == "wire.set_datum" || id == "wire.clear_datum") {
        SetSelectedDatum(id == "wire.set_datum");
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
        // 面取り・丸めは「面取り量」、オフセットは「オフセット距離」。どちらも数の棚。
        const double size = binding->method == WireTransformMethod::Offset
            ? kachakacha::v2::app::ParameterValueOf(parameterDock_->Values(),
                  kachakacha::v2::app::ParameterId::OffsetDistanceMm)
            : CornerSizeMm();
        definition.scalarArgument.value = size;
        definition.scalarArgument.expression = std::to_string(size);
        definition.scalarArgument.kind = kachakacha::v2::geometry::QuantityKind::Length;
    }
    if (binding->method == WireTransformMethod::Offset) {
        // 作業平面の中で平行に写す。面の法線は、いま作業中の平面から取る。
        definition.vectorArgument = viewport_->WorkPlane().normal;
    }
    if (binding->method == WireTransformMethod::Chamfer
        || binding->method == WireTransformMethod::Fillet) {
        // 面取りの棚の欄(B の切戻し・残す側)。棚を触っていなければ対称・自動で前と同じ。
        const V2CornerChoice choice = cornerDock_->Choice();
        definition.secondScalarMm = choice.secondSetbackMm;
        definition.firstKeepSide = choice.firstKeepSide;
        definition.secondKeepSide = choice.secondKeepSide;
    }
    if (binding->method == WireTransformMethod::CornerChamfer
        || binding->method == WireTransformMethod::CornerFillet) {
        const V2CornerChoice choice = cornerDock_->Choice();
        definition.cornerIndex = choice.onlyVertex ? choice.vertexIndex : -1;
    }
    RunWireTransform(definition, QString::fromUtf8(binding->labelJa),
        binding->consumesFirstOnly, binding->consumesInputs);
}

void V2MainWindow::RefreshCornerDock()
{
    if (cornerDock_ == nullptr || viewport_ == nullptr || parameterDock_ == nullptr) {
        return;
    }
    // 量は数の棚が正。直線 A / B は選んだ順の 1 本目と 2 本目。
    cornerDock_->SetSizeMm(CornerSizeMm());
    QString names[2];
    int found = 0;
    for (const auto& id : viewport_->Selection().entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity == nullptr || entity->kind != kachakacha::v2::domain::EntityKind::Wire) {
            continue;
        }
        if (found < 2) {
            names[found] = QString::fromStdString(entity->displayName);
        }
        ++found;
    }
    cornerDock_->SetPairText(names[0], names[1]);
}

void V2MainWindow::MakeIntersectionPoints()
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::CreatePointDefinition;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;
    using kachakacha::v2::domain::FeatureType;

    // 交点は core が求める。無ければ理由(UI-X001)を出して、何も作らない。
    const auto& selection = viewport_->Selection();
    const auto inputs = kachakacha::v2::app::SelectedCurves(selection, session_->Scene());
    const auto points = kachakacha::v2::app::IntersectionPointsOf(inputs,
        session_->GetDocument().Snapshot().settings.tolerance.interactiveJoinMm);
    if (!points.HasValue()) {
        ReportDiagnostics(points.Diagnostics());
        return;
    }
    // 点はひとまとまりで入れる。元に戻すのは一度で済む。線は変えない。
    session_->GetDocument().BeginCompound("交点に点");
    int made = 0;
    for (std::size_t index = 0; index < points.Value().size(); ++index) {
        const auto& point = points.Value()[index];
        Feature feature;
        feature.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
        feature.type = FeatureType::CreatePoint;
        feature.displayName = "交点" + std::to_string(index + 1);
        feature.inputEntityIds = selection.entityIds;
        CreatePointDefinition definition;
        definition.positionMm = point;
        definition.xExpression = {"", point.x, kachakacha::v2::geometry::QuantityKind::Length};
        definition.yExpression = {"", point.y, kachakacha::v2::geometry::QuantityKind::Length};
        definition.zExpression = {"", point.z, kachakacha::v2::geometry::QuantityKind::Length};
        feature.definition = std::move(definition);
        Entity entity;
        entity.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
        entity.kind = EntityKind::Point;
        entity.displayName = feature.displayName;
        entity.createdBy = feature.id;
        feature.outputs.push_back(FeatureOutput{"point", entity.id, EntityKind::Point});
        const auto added = session_->GetDocument().Run(
            AddFeatureCommand(feature, {entity}, feature.displayName));
        if (added.committed) {
            ++made;
        } else {
            ReportDiagnostics(added.diagnostics);
        }
    }
    session_->GetDocument().EndCompound();
    AdoptCurrentDocument();
    SetStatus(QStringLiteral("交点に点: %1 か所に作図点を作りました。線は変わっていません。")
            .arg(made));
}

void V2MainWindow::SetSelectedDatum(bool datum)
{
    // 基準線は印だけ。形も向きも変わらない(V1 と同じ)。
    const auto& selection = viewport_->Selection();
    std::vector<kachakacha::v2::base::EntityId> wires;
    for (const auto& id : selection.entityIds) {
        const auto* entity = session_->GetDocument().FindEntity(id);
        if (entity != nullptr && entity->kind == kachakacha::v2::domain::EntityKind::Wire) {
            wires.push_back(id);
        }
    }
    const auto changed = session_->GetDocument().Run(
        kachakacha::v2::document::SetDatumCommand(wires, datum));
    if (!changed.committed) {
        ReportDiagnostics(changed.diagnostics);
        return;
    }
    AdoptCurrentDocument();
    SetStatus(datum ? QStringLiteral("%1 本を基準線にしました(一点鎖線で出ます)。")
                          .arg(static_cast<int>(wires.size()))
                    : QStringLiteral("%1 本の基準線をやめました。")
                          .arg(static_cast<int>(wires.size())));
}

void V2MainWindow::RunWireTransform(
    const kachakacha::v2::domain::TransformWireDefinition& definition,
    const QString& labelJa, bool consumesFirstOnly, bool consumesInputs)
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;
    using kachakacha::v2::domain::FeatureType;

    const auto& selection = viewport_->Selection();
    const auto inputs = kachakacha::v2::app::SelectedCurves(selection, session_->Scene());
    if (inputs.empty()) {
        SetStatus(QStringLiteral("%1: 先に線を選んでください。").arg(labelJa));
        return;
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
    feature.displayName = labelJa.toStdString();
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
    entity.displayName = labelJa.toStdString();
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});

    const auto added = session_->GetDocument().Run(
        AddFeatureCommand(feature, {entity}, labelJa.toStdString()));
    if (!added.committed) {
        ReportDiagnostics(added.diagnostics);
        return;
    }
    std::vector<kachakacha::v2::base::EntityId> consumed = selection.entityIds;
    if (consumesFirstOnly && !consumed.empty()) {
        // 分割やトリムは1本目を直すだけ。刃や境界にした線は残す。
        consumed.resize(1);
    }
    if (!consumesInputs) {
        consumed.clear();   // オフセットは元の線を残す。
    }
    RemoveConsumedWires(consumed);
    AdoptCurrentDocument();
    if (!consumesInputs) {
        SetStatus(QStringLiteral("%1: %2本の線を写しました。元の線は残っています。")
                .arg(labelJa)
                .arg(static_cast<int>(computed.Value().size())));
        return;
    }
    if (consumesFirstOnly) {
        SetStatus(QStringLiteral("%1: 1本目を%2本にしました。相手の線は残っています。")
                .arg(labelJa)
                .arg(static_cast<int>(computed.Value().size())));
        return;
    }
    SetStatus(QStringLiteral("%1: %2本の線から%3本にしました。")
            .arg(labelJa)
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

void V2MainWindow::BeginTrimOrExtend(bool trim)
{
    using kachakacha::v2::domain::WireTransformMethod;

    // どこを切るか、どちら側へ延ばすかは「押した場所」で決まる。
    // 選択だけでは決まらないので、1回だけ押す場所を聞く。
    const auto& selection = viewport_->Selection();
    const auto inputs = kachakacha::v2::app::SelectedCurves(selection, session_->Scene());
    const QString label = trim ? QStringLiteral("トリム") : QStringLiteral("延長");
    if (inputs.size() < 2) {
        SetStatus(QStringLiteral("%1: 線を2本選んでください"
                                 "(1本目が直す線、2本目が境界です)。")
                .arg(label));
        return;
    }
    const kachakacha::v2::geometry::CurveSegment target = inputs.front();
    viewport_->BeginPointPick(
        [this, trim, target, label](const V2Viewport::PickedPoint& picked) {
            // 押した場所を、直す線の上の位置(0..1)へ直す。
            const auto closest = kachakacha::v2::geometry::MeasurePointToCurve(
                picked.point, target);
            kachakacha::v2::domain::TransformWireDefinition definition;
            definition.method = trim ? WireTransformMethod::Trim
                                     : WireTransformMethod::Extend;
            // トリムは「捨てる側」を、延長は「延ばす端」を、この数で伝える。
            definition.scalarArgument.value = trim
                ? closest.secondParameter
                : (closest.secondParameter >= 0.5 ? 1.0 : 0.0);
            definition.scalarArgument.kind =
                kachakacha::v2::geometry::QuantityKind::Scalar;
            RunWireTransform(definition, label, true);
        },
        trim ? "トリム: 捨てる側を1回押してください(Esc でやめます)。"
             : "延長: 延ばしたい端の近くを1回押してください(Esc でやめます)。");
}

namespace {

//! 変換の中身を、文書へ入れられる形へ写す。判断は core が済ませてある。
[[nodiscard]] kachakacha::v2::domain::TransformWireDefinition DefinitionFor(
    const kachakacha::v2::modeling::TransformPlan& plan)
{
    using kachakacha::v2::domain::TransformWireDefinition;
    using kachakacha::v2::domain::WireTransformMethod;
    using kachakacha::v2::modeling::TransformKind;

    TransformWireDefinition definition;
    switch (plan.kind) {
    case TransformKind::Move:
        definition.method = WireTransformMethod::Move;
        break;
    case TransformKind::Copy:
        definition.method = WireTransformMethod::Copy;
        break;
    case TransformKind::Mirror:
        definition.method = WireTransformMethod::Mirror;
        break;
    case TransformKind::Rotate:
        definition.method = WireTransformMethod::Rotate;
        break;
    }
    definition.vectorArgument = plan.vectorArgument;
    definition.pointArgument = plan.pointArgument;
    definition.scalarArgument.value = plan.angleRad;
    definition.scalarArgument.expression = std::to_string(plan.angleRad);
    definition.scalarArgument.kind = plan.kind == TransformKind::Rotate
        ? kachakacha::v2::geometry::QuantityKind::Angle
        : kachakacha::v2::geometry::QuantityKind::Length;
    return definition;
}

} // namespace

bool V2MainWindow::TransformOneWire(
    const kachakacha::v2::domain::TransformWireDefinition& definition,
    kachakacha::v2::base::EntityId entityId, const QString& labelJa)
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;
    using kachakacha::v2::domain::FeatureType;

    kachakacha::v2::app::SelectionSet one;
    one.entityIds.push_back(entityId);
    const auto inputs = kachakacha::v2::app::SelectedCurves(one, session_->Scene());
    if (inputs.empty()) {
        return false;
    }
    const auto computed =
        kachakacha::v2::document::EvaluateWireTransform(definition, inputs);
    if (!computed.HasValue()) {
        ReportDiagnostics(computed.Diagnostics());
        return false;
    }
    const auto* source = session_->GetDocument().FindEntity(entityId);
    Feature feature;
    feature.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
    feature.type = FeatureType::TransformWire;
    feature.displayName = labelJa.toStdString();
    feature.inputEntityIds = one.entityIds;
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
    // 名前を引き継ぐ。動かしただけで名前が変わると、一覧で見失う。
    entity.displayName = source != nullptr && !source->displayName.empty()
        ? source->displayName
        : labelJa.toStdString();
    entity.construction = source != nullptr && source->construction;
    entity.groupId = source != nullptr ? source->groupId : std::nullopt;
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});

    const auto added = session_->GetDocument().Run(
        AddFeatureCommand(feature, {entity}, labelJa.toStdString()));
    if (!added.committed) {
        ReportDiagnostics(added.diagnostics);
        return false;
    }
    return true;
}

void V2MainWindow::ApplyTransformPlan(
    const kachakacha::v2::modeling::TransformPlan& plan)
{
    const auto selected = viewport_->Selection().entityIds;
    const QString label = QString::fromStdString(plan.summaryJa);
    if (selected.empty()) {
        SetStatus(QStringLiteral("%1: 先に動かす線を選んでください。").arg(label));
        return;
    }
    // ここは線の編集(トリムなど)とは分ける。あちらは複数の線から1本を作るが、
    // 動かすのは1本を1本のまま動かすことである。まとめて1本にしてしまうと、
    // 3本を動かしたつもりが1本になり、名前も分け方も失われる。
    const auto definition = DefinitionFor(plan);
    int changed = 0;
    std::vector<kachakacha::v2::base::EntityId> consumed;
    for (const auto& entityId : selected) {
        if (!TransformOneWire(definition, entityId, label)) {
            continue;
        }
        ++changed;
        if (!plan.keepsSource) {
            consumed.push_back(entityId);
        }
    }
    if (changed == 0) {
        SetStatus(QStringLiteral("%1: 動かせる線がありませんでした。").arg(label));
        return;
    }
    RemoveConsumedWires(consumed);
    AdoptCurrentDocument();
    if (plan.keepsSource) {
        SetStatus(QStringLiteral("%1: %2本を写しました。元の線は残っています。")
                .arg(label)
                .arg(changed));
        return;
    }
    SetStatus(QStringLiteral("%1: %2本を動かしました。").arg(label).arg(changed));
}

void V2MainWindow::ReplaceWireSegment(kachakacha::v2::base::EntityId entityId,
    kachakacha::v2::base::SegmentId segmentId,
    const kachakacha::v2::geometry::CurveSegment& replacement)
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;
    using kachakacha::v2::domain::FeatureType;

    // 動かした1本だけ差し替える。同じワイヤーの他の線はそのまま持ち越す。
    std::vector<kachakacha::v2::geometry::CurveSegment> segments;
    bool replaced = false;
    for (const auto& curve : session_->Scene().curves) {
        if (curve.entityId != entityId) {
            continue;
        }
        if (curve.segmentId == segmentId) {
            segments.push_back(replacement);
            replaced = true;
            continue;
        }
        segments.push_back(curve.segment);
    }
    if (!replaced) {
        SetStatus(QStringLiteral("動かした線が見つかりませんでした。"));
        return;
    }
    const auto* source = session_->GetDocument().FindEntity(entityId);
    const QString label = QStringLiteral("制御点を動かす");

    Feature feature;
    feature.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
    feature.type = FeatureType::CreateWire;
    feature.displayName = label.toStdString();
    kachakacha::v2::domain::CreateWireDefinition wire;
    wire.segments = std::move(segments);
    for (std::size_t index = 0; index < wire.segments.size(); ++index) {
        wire.segmentIds.push_back(
            ids_->NextTyped<kachakacha::v2::base::IdKind::Segment>());
    }
    feature.definition = std::move(wire);

    Entity entity;
    entity.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
    entity.kind = EntityKind::Wire;
    entity.displayName = source != nullptr && !source->displayName.empty()
        ? source->displayName
        : label.toStdString();
    entity.construction = source != nullptr && source->construction;
    entity.groupId = source != nullptr ? source->groupId : std::nullopt;
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});

    const auto added = session_->GetDocument().Run(
        AddFeatureCommand(feature, {entity}, label.toStdString()));
    if (!added.committed) {
        ReportDiagnostics(added.diagnostics);
        return;
    }
    RemoveConsumedWires({entityId});
    AdoptCurrentDocument();
    // 直した線をそのまま選んでおく。選び直さずに続けて直せる。
    kachakacha::v2::app::SelectionSet next;
    next.entityIds.push_back(entity.id);
    viewport_->SetSelection(next);
    SetStatus(label + QStringLiteral("ました。"));
}

void V2MainWindow::ApplyToolSettings(const kachakacha::v2::modeling::ToolSettings& settings)
{
    // 円弧の作り方・補助線・指定点を残す。作業平面の向きは置くときに場面から渡される。
    // 道具の途中の点は捨てる(V1 と同じ。作り方を変えたら最初から)。
    session_->SetToolSettings(settings);
    viewport_->OnToolChanged();
    RefreshGuide();
    viewport_->update();
}

void V2MainWindow::CreateWireFromDock()
{
    if (drawingDock_ == nullptr) {
        return;
    }
    // 欄の値を core へ。作れるかは core が決め、理由はそのまま棚と帯へ出す。
    const auto request = drawingDock_->DirectWire();
    const auto made = kachakacha::v2::app::BuildDirectWire(request, viewport_->WorkPlane());
    if (!made.HasValue()) {
        ReportDiagnostics(made.Diagnostics());
        drawingDock_->ShowMessage(QString::fromStdString(
            made.Diagnostics().front().summaryJa + " " + made.Diagnostics().front().detailsJa));
        return;
    }
    const QString name = drawingDock_->DirectWireName();
    const auto added = session_->AddWire(made.Value(), request.construction,
        name.isEmpty() ? std::string("数値の線") : name.toStdString());
    if (!added.committed) {
        ReportDiagnostics(added.diagnostics);
        return;
    }
    AdoptCurrentDocument();
    drawingDock_->ShowMessage(QString());
    SetStatus(QStringLiteral("%1: %2 を作りました。")
            .arg(QString::fromUtf8(std::string(
                kachakacha::v2::app::DirectWireKindNameJa(request.kind)).c_str()),
                name.isEmpty() ? QStringLiteral("数値の線") : name));
}
