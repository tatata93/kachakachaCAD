#include "kachakacha/document/FeatureReevaluation.h"

#include "kachakacha/geometry/WireEdit.h"

#include <algorithm>
#include <map>
#include <set>

namespace kachakacha::v2::document {
namespace {

using base::MakeError;
using base::Result;
using domain::CreatePointDefinition;
using domain::CreateWireDefinition;
using domain::EntityKind;
using domain::TransformWireDefinition;
using domain::WireTransformMethod;
using geometry::CurveSegment;
using geometry::Vector3;

//! その Feature が出す Entity の一覧。
[[nodiscard]] std::vector<EntityId> OutputsOf(const Feature& feature)
{
    std::vector<EntityId> ids;
    ids.reserve(feature.outputs.size());
    for (const auto& output : feature.outputs) {
        ids.push_back(output.entityId);
    }
    return ids;
}

//! 定義の種類の番号。variant のどの型が入っているか。
[[nodiscard]] std::size_t DefinitionKind(const FeatureDefinition& definition)
{
    return definition.index();
}

} // namespace

Result<std::monostate> CheckDefinitionSwap(const Feature& feature,
    const FeatureDefinition& definition)
{
    if (DefinitionKind(feature.definition) != DefinitionKind(definition)) {
        return Result<std::monostate>::Failure(MakeError("DOC-C006",
            "操作の種類は変えられません。",
            std::string(domain::FeatureTypeName(feature.type))
                + " の中身を、別の種類の定義に差し替えようとしています。"
                + "作り直してください。"));
    }
    return Result<std::monostate>::Success(std::monostate{});
}

Result<ReevaluationPlan> PlanReevaluation(const DocumentSnapshot& snapshot,
    FeatureId changed)
{
    const std::vector<FeatureId> order = Document::TopologicalOrder(snapshot);
    if (order.empty() && !snapshot.features.empty()) {
        return Result<ReevaluationPlan>::Failure(MakeError("DOC-V004",
            "操作履歴が循環しています。", "計算し直す順番を決められません。"));
    }
    std::map<FeatureId, const Feature*> byId;
    for (const Feature& feature : snapshot.features) {
        byId[feature.id] = &feature;
    }
    if (byId.find(changed) == byId.end()) {
        return Result<ReevaluationPlan>::Failure(MakeError("DOC-C001",
            "入力に指定されたものが見つかりません。", changed.ToString()));
    }

    // 変えた Feature の出力から下流へ、順番に広げる。
    std::set<EntityId> dirtyEntities;
    std::set<FeatureId> dirtyFeatures{changed};
    ReevaluationPlan plan;
    for (const FeatureId& id : order) {
        const auto found = byId.find(id);
        if (found == byId.end()) {
            continue;
        }
        const Feature& feature = *found->second;
        const bool isChanged = id == changed;
        const bool touchesDirty = std::any_of(feature.inputEntityIds.begin(),
            feature.inputEntityIds.end(),
            [&](const EntityId& input) { return dirtyEntities.count(input) > 0; });
        if (!isChanged && !touchesDirty) {
            continue;
        }
        dirtyFeatures.insert(id);
        plan.order.push_back(id);
        for (const EntityId& output : OutputsOf(feature)) {
            dirtyEntities.insert(output);
            plan.affectedEntityIds.push_back(output);
        }
    }
    if (plan.order.empty()) {
        // 位相順に入っていない(孤立した)Feature でも、それ自身は計算し直す。
        plan.order.push_back(changed);
        for (const EntityId& output : OutputsOf(*byId[changed])) {
            plan.affectedEntityIds.push_back(output);
        }
    }
    return Result<ReevaluationPlan>::Success(std::move(plan));
}

namespace {

//! 作図点。式を持っているならその値を使う。
[[nodiscard]] ReevaluatedOutput EvaluatePoint(const domain::FeatureOutput& output,
    const CreatePointDefinition& definition)
{
    ReevaluatedOutput result;
    result.entityId = output.entityId;
    result.key = output.key;
    result.kind = EntityKind::Point;
    result.positionMm = definition.positionMm;
    return result;
}

//! ワイヤー。定義がそのまま形になる。
[[nodiscard]] ReevaluatedOutput EvaluateWire(const domain::FeatureOutput& output,
    const CreateWireDefinition& definition)
{
    ReevaluatedOutput result;
    result.entityId = output.entityId;
    result.key = output.key;
    result.kind = EntityKind::Wire;
    result.segments = definition.segments;
    return result;
}

//! ワイヤーの変形。移動・回転・鏡像だけは core で最後まで出せる。
[[nodiscard]] Result<std::vector<CurveSegment>> ApplyTransform(
    const TransformWireDefinition& definition, const std::vector<CurveSegment>& inputs)
{
    std::vector<CurveSegment> outputs;
    outputs.reserve(inputs.size());
    for (const CurveSegment& segment : inputs) {
        switch (definition.method) {
        case WireTransformMethod::Move:
        case WireTransformMethod::Copy:
            outputs.push_back(geometry::TranslateCurve(segment, definition.vectorArgument));
            break;
        case WireTransformMethod::Rotate: {
            const auto rotated = geometry::RotateCurve(segment, definition.pointArgument,
                definition.vectorArgument, definition.scalarArgument.value);
            if (!rotated.HasValue()) {
                return Result<std::vector<CurveSegment>>::Failure(rotated.Diagnostics());
            }
            outputs.push_back(rotated.Value());
            break;
        }
        case WireTransformMethod::Mirror: {
            const auto mirrored = geometry::MirrorCurve(segment, definition.pointArgument,
                definition.vectorArgument);
            if (!mirrored.HasValue()) {
                return Result<std::vector<CurveSegment>>::Failure(mirrored.Diagnostics());
            }
            outputs.push_back(mirrored.Value());
            break;
        }
        default:
            // トリムや結合は相手の線が要る。ここでは扱わず、断る。
            return Result<std::vector<CurveSegment>>::Failure(MakeError("DOC-C007",
                "この編集は、この場では計算し直せません。",
                "相手の線が要る編集です。"));
        }
    }
    return Result<std::vector<CurveSegment>>::Success(std::move(outputs));
}

//! その Entity の、いまの形。上流の Feature の出力から引く。
[[nodiscard]] std::vector<CurveSegment> SegmentsOfEntity(const DocumentSnapshot& snapshot,
    const EntityId& id)
{
    for (const Feature& feature : snapshot.features) {
        for (const auto& output : feature.outputs) {
            if (output.entityId != id) {
                continue;
            }
            if (const auto* wire = std::get_if<CreateWireDefinition>(&feature.definition)) {
                return wire->segments;
            }
        }
    }
    return {};
}

} // namespace

Result<ReevaluationResult> ReevaluateFeature(const DocumentSnapshot& snapshot,
    FeatureId featureId)
{
    const Feature* feature = nullptr;
    for (const Feature& candidate : snapshot.features) {
        if (candidate.id == featureId) {
            feature = &candidate;
            break;
        }
    }
    if (feature == nullptr) {
        return Result<ReevaluationResult>::Failure(MakeError("DOC-C001",
            "入力に指定されたものが見つかりません。", featureId.ToString()));
    }
    ReevaluationResult result;
    result.featureRevision = feature->revision;
    for (const auto& output : feature->outputs) {
        if (const auto* point = std::get_if<CreatePointDefinition>(&feature->definition)) {
            result.outputs.push_back(EvaluatePoint(output, *point));
            continue;
        }
        if (const auto* wire = std::get_if<CreateWireDefinition>(&feature->definition)) {
            result.outputs.push_back(EvaluateWire(output, *wire));
            continue;
        }
        if (const auto* transform =
                std::get_if<TransformWireDefinition>(&feature->definition)) {
            std::vector<CurveSegment> inputs;
            for (const EntityId& id : feature->inputEntityIds) {
                const std::vector<CurveSegment> found = SegmentsOfEntity(snapshot, id);
                inputs.insert(inputs.end(), found.begin(), found.end());
            }
            const auto moved = ApplyTransform(*transform, inputs);
            if (!moved.HasValue()) {
                return Result<ReevaluationResult>::Failure(moved.Diagnostics());
            }
            ReevaluatedOutput built;
            built.entityId = output.entityId;
            built.key = output.key;
            built.kind = EntityKind::Wire;
            built.segments = moved.Value();
            result.outputs.push_back(std::move(built));
            continue;
        }
        // core では形を持てない出力。OCCT 側が作る。ここで作ったふりをしない。
        ReevaluatedOutput deferred;
        deferred.entityId = output.entityId;
        deferred.key = output.key;
        deferred.kind = output.kind;
        deferred.needsKernel = true;
        result.outputs.push_back(std::move(deferred));
    }
    return Result<ReevaluationResult>::Success(std::move(result));
}

} // namespace kachakacha::v2::document
