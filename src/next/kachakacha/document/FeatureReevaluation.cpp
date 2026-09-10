#include "kachakacha/document/FeatureReevaluation.h"

#include "kachakacha/geometry/PolylineCorners.h"

#include "kachakacha/geometry/WireConnect.h"
#include "kachakacha/geometry/WireEdit.h"

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
//! 相手の線が要る編集。入力を全部まとめて受け取って計算する。
//!
//! 1本目を「直す線」、残りを「相手」として扱う。選んだ順がそのまま意味になる。
//! 順を無視して当てにいくと、意図と違う線が消える。
[[nodiscard]] Result<std::vector<CurveSegment>> ApplyMultiInputTransform(
    const TransformWireDefinition& definition, const std::vector<CurveSegment>& inputs)
{
    using Out = Result<std::vector<CurveSegment>>;
    // 編集の許容差。文書の許容差を渡せる形にするまでは、対話用の既定値を使う。
    constexpr double tolerance = 0.01;
    if (definition.method == WireTransformMethod::Offset) {
        // オフセットは相手の線が要らない。1本ずつ動かす。
        std::vector<CurveSegment> offsets;
        offsets.reserve(inputs.size());
        for (const CurveSegment& segment : inputs) {
            const auto moved = geometry::OffsetCurveInPlane(segment,
                definition.vectorArgument, definition.scalarArgument.value);
            if (!moved.HasValue()) {
                return Out::Failure(moved.Diagnostics());
            }
            offsets.push_back(moved.Value());
        }
        return Out::Success(std::move(offsets));
    }
    if (definition.method == WireTransformMethod::CornerChamfer
        || definition.method == WireTransformMethod::CornerFillet) {
        // 角の加工は1本の並びの中で完結する。相手の線は要らない。
        return geometry::ProcessPolylineCorners(inputs,
            definition.method == WireTransformMethod::CornerChamfer
                ? geometry::CornerStyle::Chamfer
                : geometry::CornerStyle::Fillet,
            definition.scalarArgument.value, tolerance, definition.cornerIndex);
    }
    if (inputs.size() < 2) {
        return Out::Failure(MakeError("DOC-C007",
            "この編集は、この場では計算し直せません。",
            "相手の線が要る編集です。2本以上を選んでください。"));
    }
    const CurveSegment& first = inputs[0];
    const CurveSegment& second = inputs[1];
    const std::vector<CurveSegment> others(inputs.begin() + 1, inputs.end());
    switch (definition.method) {
    case WireTransformMethod::Split:
        return geometry::SplitCurveAtIntersections(first, others, tolerance);
    case WireTransformMethod::Join:
        return geometry::JoinCurves(inputs, tolerance);
    case WireTransformMethod::Coincident:
    case WireTransformMethod::Tangent:
    case WireTransformMethod::Curvature: {
        const geometry::ConnectContinuity continuity =
            definition.method == WireTransformMethod::Coincident
            ? geometry::ConnectContinuity::Position
            : (definition.method == WireTransformMethod::Tangent
                      ? geometry::ConnectContinuity::Tangent
                      : geometry::ConnectContinuity::Curvature);
        const auto joined = geometry::ConnectCurves(first, second, continuity, tolerance);
        if (!joined.HasValue()) {
            return Out::Failure(joined.Diagnostics());
        }
        return Out::Success({joined.Value().first, joined.Value().second});
    }
    case WireTransformMethod::MeetLines: {
        const auto met = geometry::MeetLines(first, second, tolerance);
        if (!met.HasValue()) {
            return Out::Failure(met.Diagnostics());
        }
        return Out::Success({met.Value().first, met.Value().second});
    }
    case WireTransformMethod::Chamfer:
    case WireTransformMethod::Fillet: {
        const double size = definition.scalarArgument.value;
        // 面取りの欄(B の切戻し・残す側)。無ければ対称・自動で、前と同じ。
        geometry::CornerOptions options;
        options.secondSetbackMm = definition.secondScalarMm;
        options.firstKeepSide = definition.firstKeepSide;
        options.secondKeepSide = definition.secondKeepSide;
        const auto corner = definition.method == WireTransformMethod::Chamfer
            ? geometry::ChamferLines(first, second, size, options, tolerance)
            : geometry::FilletLines(first, second, size, options, tolerance);
        if (!corner.HasValue()) {
            return Out::Failure(corner.Diagnostics());
        }
        return Out::Success(
            {corner.Value().first, corner.Value().corner, corner.Value().second});
    }
    case WireTransformMethod::Trim: {
        const auto trimmed = geometry::TrimCurve(first, second,
            definition.scalarArgument.value, tolerance);
        if (!trimmed.HasValue()) {
            return Out::Failure(trimmed.Diagnostics());
        }
        return Out::Success({trimmed.Value()});
    }
    case WireTransformMethod::Extend: {
        const int endpoint = definition.scalarArgument.value >= 0.5 ? 1 : 0;
        const auto extended = geometry::ExtendCurveToBoundary(first, endpoint, second,
            tolerance);
        if (!extended.HasValue()) {
            return Out::Failure(extended.Diagnostics());
        }
        return Out::Success({extended.Value()});
    }
    default:
        break;
    }
    return Out::Failure(MakeError("DOC-C007",
        "この編集は、この場では計算し直せません。", "この編集は扱えません。"));
}

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
            // 相手の線が要る編集は、1本ずつでは決まらない。下でまとめて扱う。
            break;
        }
    }
    if (!outputs.empty()) {
        return Result<std::vector<CurveSegment>>::Success(std::move(outputs));
    }
    return ApplyMultiInputTransform(definition, inputs);
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

Result<std::vector<CurveSegment>> EvaluateWireTransform(
    const TransformWireDefinition& definition, const std::vector<CurveSegment>& inputs)
{
    if (inputs.empty()) {
        return Result<std::vector<CurveSegment>>::Failure(MakeError("DOC-C001",
            "入力に指定されたものが見つかりません。", "線が1本も選ばれていません。"));
    }
    return ApplyTransform(definition, inputs);
}

} // namespace kachakacha::v2::document
