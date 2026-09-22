//! 部品の配置(P-18、部品モードの「配置」: 移動・回転・ミラー・コピー・パターン)。
//!
//! 線の移動・複製・鏡映・回転・配列と **同じ道具・同じ点の置き方** を使い、選んだものが部品なら
//! ここで部品の形に同じ変換を掛ける(core の TransformPlan / 配列の段取りはそのまま)。
//! 形は元の部品の形に変換を掛けて作り(kernel/OcctTransform)、文書には変換だけを残す
//! (TransformPartDefinition)。開き直したら元を作り直してから同じ変換を掛ける。
//! 動かす・回すは元を隠し(消すと作り方をたどれない)、写す・鏡・並べるは元を残す。

#include "V2MainWindow.h"

#include "kachakacha/app/ExplorerModel.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/document/FeatureReevaluation.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/kernel/OcctTransform.h"

#include <QString>

#include <string>
#include <utility>
#include <vector>

namespace {

using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::TransformWireDefinition;
using kachakacha::v2::domain::WireTransformMethod;

//! 線の変換の定義から、核の変換へ。移動・複製は平行移動、回転は軸と角度、鏡映は面。
[[nodiscard]] kachakacha::v2::kernel::ShapeTransform KernelTransformFor(
    const TransformWireDefinition& definition)
{
    using kachakacha::v2::kernel::ShapeTransformKind;
    kachakacha::v2::kernel::ShapeTransform transform;
    transform.vector = definition.vectorArgument;
    transform.point = definition.pointArgument;
    transform.angleRad = definition.scalarArgument.value;
    transform.kind = definition.method == WireTransformMethod::Rotate ? ShapeTransformKind::Rotate
        : definition.method == WireTransformMethod::Mirror            ? ShapeTransformKind::Mirror
                                                                      : ShapeTransformKind::Translate;
    return transform;
}

//! 文書の TransformPartDefinition::method(modeling::TransformKind の並び)。
[[nodiscard]] int PartMethodFor(WireTransformMethod method)
{
    switch (method) {
    case WireTransformMethod::Copy:   return 1;
    case WireTransformMethod::Mirror: return 2;
    case WireTransformMethod::Rotate: return 3;
    default:                          return 0;
    }
}

//! 開き直しで使う逆向き: 文書の method から、同じ変換を掛けるための線の定義。
[[nodiscard]] TransformWireDefinition WireDefinitionFor(
    const kachakacha::v2::domain::TransformPartDefinition& place)
{
    TransformWireDefinition definition;
    definition.method = place.method == 1 ? WireTransformMethod::Copy
        : place.method == 2               ? WireTransformMethod::Mirror
        : place.method == 3               ? WireTransformMethod::Rotate
                                          : WireTransformMethod::Move;
    definition.vectorArgument = place.vectorArgument;
    definition.pointArgument = place.pointArgument;
    definition.scalarArgument.value = place.angleRad;
    return definition;
}

} // namespace

//! 部品 1 つに線と同じ変換を掛けた新しい部品を作る。元の部品はここでは隠さない(呼ぶ側が決める)。
//! replacesSource: 動かす・回す(元を置き換える)なら真。
//! 名前: 動かす・回すは元の名前のまま、写す・鏡・並べるは「〜 コピー」「〜 鏡」。
//! 用途(完成品/治具など)・材料・グループは元から引き継ぐ。
bool V2MainWindow::TransformOnePart(const TransformWireDefinition& definition,
    const EntityId& partId, const QString& labelJa, bool replacesSource)
{
    using kachakacha::v2::document::AddFeatureCommand;
    using kachakacha::v2::domain::Entity;
    using kachakacha::v2::domain::EntityKind;
    using kachakacha::v2::domain::Feature;
    using kachakacha::v2::domain::FeatureOutput;
    using kachakacha::v2::domain::FeatureType;

    const auto* source = session_->GetDocument().FindEntity(partId);
    const auto shape = partShapes_.find(partId.ToString());
    if (source == nullptr || source->kind != EntityKind::Part || shape == partShapes_.end()) {
        return false;
    }
    const auto built = kachakacha::v2::kernel::TransformShape(shape->second,
        KernelTransformFor(definition));
    if (!built.HasValue()) {
        ReportDiagnostics(built.Diagnostics());
        return false;
    }
    kachakacha::v2::domain::TransformPartDefinition place;
    place.method = PartMethodFor(definition.method);
    place.source = partId;
    place.vectorArgument = definition.vectorArgument;
    place.pointArgument = definition.pointArgument;
    place.angleRad = definition.scalarArgument.value;
    // 見せる辺(吸着にも使う)は、元の部品の辺に同じ変換を掛けたもの。
    std::vector<kachakacha::v2::geometry::CurveSegment> edges;
    if (const auto found = partEdges_.find(partId.ToString()); found != partEdges_.end()) {
        const auto moved = kachakacha::v2::document::EvaluateWireTransform(definition, found->second);
        if (moved.HasValue()) {
            edges = moved.Value();
        }
    }
    const bool keepsName = replacesSource;   // 動かす・回す(元を隠す)は同じ名前のまま
    const std::string base = source->displayName.empty() ? std::string("部品") : source->displayName;
    Feature feature;
    feature.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Feature>();
    feature.type = FeatureType::TransformPart;
    feature.displayName = labelJa.toStdString();
    feature.inputEntityIds = {partId};
    feature.definition = place;
    Entity entity;
    entity.id = ids_->NextTyped<kachakacha::v2::base::IdKind::Entity>();
    entity.kind = EntityKind::Part;
    entity.displayName = keepsName
        ? base
        : kachakacha::v2::app::UniqueDisplayName(session_->GetDocument().Snapshot(), EntityKind::Part,
              base + (definition.method == WireTransformMethod::Mirror ? " 鏡" : " コピー"));
    entity.partRole = source->partRole;
    entity.manufacturing = source->manufacturing;
    entity.groupId = source->groupId;
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{"part", entity.id, EntityKind::Part});
    const auto added = session_->GetDocument().Run(
        AddFeatureCommand(feature, {entity}, labelJa.toStdString()));
    if (!added.committed) {
        ReportDiagnostics(added.diagnostics);
        return false;
    }
    partShapes_[entity.id.ToString()] = built.Value().handle;
    partEdges_[entity.id.ToString()] = std::move(edges);
    return true;
}

//! 開き直したときの作り直し: 元の部品(先に作り直してある)に同じ変換を掛ける。
bool V2MainWindow::RebuildTransformPartShape(const kachakacha::v2::domain::Feature& feature,
    const EntityId& output)
{
    const auto* place =
        std::get_if<kachakacha::v2::domain::TransformPartDefinition>(&feature.definition);
    if (place == nullptr) {
        return false;
    }
    const auto source = partShapes_.find(place->source.ToString());
    if (source == partShapes_.end()) {
        return false;   // 元がまだ出来ていない。黙って作らない。
    }
    const auto definition = WireDefinitionFor(*place);
    const auto built = kachakacha::v2::kernel::TransformShape(source->second,
        KernelTransformFor(definition));
    if (!built.HasValue()) {
        return false;
    }
    partShapes_[output.ToString()] = built.Value().handle;
    if (const auto edges = partEdges_.find(place->source.ToString()); edges != partEdges_.end()) {
        const auto moved = kachakacha::v2::document::EvaluateWireTransform(definition, edges->second);
        if (moved.HasValue()) {
            partEdges_[output.ToString()] = moved.Value();
        }
    }
    return true;
}

//! 動かした・回した元の部品を隠す(消さない。作り方をたどれなくなる)。
bool V2MainWindow::HideConsumedParts(const std::vector<EntityId>& parts)
{
    if (parts.empty()) {
        return true;
    }
    const auto hidden = session_->GetDocument().Run(kachakacha::v2::document::SetVisibilityCommand(
        parts, kachakacha::v2::domain::Visibility::Hidden));
    if (!hidden.committed) {
        ReportDiagnostics(hidden.diagnostics);
        return false;
    }
    return true;
}
