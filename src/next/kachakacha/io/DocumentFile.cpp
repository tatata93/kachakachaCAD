#include "kachakacha/io/DocumentFile.h"

#include "kachakacha/io/DocumentFileNames.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace kachakacha::v2::io {

using base::Diagnostic;
using base::DocumentId;
using base::DimensionId;
using base::EntityId;
using base::FeatureId;
using base::GroupId;
using base::MakeError;
using base::MakeWarning;
using base::Result;
using base::SegmentId;
using base::Uuid;
using document::DocumentSnapshot;
using document::Group;
using document::ReferenceDimension;
using domain::CreatePointDefinition;
using domain::CreateWireDefinition;
using domain::EditPolicy;
using domain::Entity;
using domain::EntityKind;
using domain::Feature;
using domain::FeatureOutput;
using domain::FeatureType;
using domain::FreezeDerivedDefinition;
using domain::ManufacturingProperties;
using domain::PartRole;
using domain::SegmentRef;
using domain::TransformWireDefinition;
using domain::Visibility;
using domain::WireTransformMethod;
using geometry::CurveKind;
using geometry::CurveSegment;
using geometry::EvaluatedValue;
using geometry::QuantityKind;
using geometry::Vector3;
using namespace detail;

namespace {

// ---------------------------------------------------------------- 書き出し

[[nodiscard]] JsonValue WriteVector(const Vector3& value)
{
    JsonObject object;
    object["x"] = JsonValue::Number(value.x);
    object["y"] = JsonValue::Number(value.y);
    object["z"] = JsonValue::Number(value.z);
    return JsonValue::Object(std::move(object));
}

[[nodiscard]] JsonValue WriteExpression(const EvaluatedValue& value)
{
    JsonObject object;
    object["expression"] = JsonValue::String(value.expression);
    object["value"] = JsonValue::Number(value.value);
    object["quantity"] = JsonValue::String(NameOf(kQuantityKinds, value.kind));
    return JsonValue::Object(std::move(object));
}

template<class Id>
[[nodiscard]] JsonValue WriteId(const Id& id)
{
    return JsonValue::String(id.ToString());
}

template<class Id>
[[nodiscard]] JsonValue WriteOptionalId(const std::optional<Id>& id)
{
    return id.has_value() ? WriteId(*id) : JsonValue::Null();
}

[[nodiscard]] JsonValue WriteSegment(const CurveSegment& segment, const SegmentId& id)
{
    JsonObject object;
    object["id"] = WriteId(id);
    object["type"] = JsonValue::String(NameOf(kCurveKinds, segment.Kind()));
    switch (segment.Kind()) {
    case CurveKind::Line:
        object["start"] = WriteVector(segment.StartPoint());
        object["end"] = WriteVector(segment.EndPoint());
        break;
    case CurveKind::CircularArc:
    case CurveKind::Circle:
        object["center"] = WriteVector(segment.Center());
        object["normal"] = WriteVector(segment.Normal());
        object["xDirection"] = WriteVector(segment.ReferenceDirection());
        object["radiusMm"] = JsonValue::Number(segment.Radius());
        if (segment.Kind() == CurveKind::CircularArc) {
            object["startAngleRad"] = JsonValue::Number(segment.StartAngleRad());
            object["sweepAngleRad"] = JsonValue::Number(segment.SweepAngleRad());
        }
        break;
    case CurveKind::CubicBezier:
    case CurveKind::CubicBSpline: {
        JsonArray points;
        for (const Vector3& point : segment.ControlPoints()) {
            points.push_back(WriteVector(point));
        }
        object["controlPoints"] = JsonValue::Array(std::move(points));
        if (segment.Kind() == CurveKind::CubicBSpline) {
            object["degree"] = JsonValue::Number(3.0);
            object["periodic"] = JsonValue::Bool(false);
        }
        break;
    }
    }
    return JsonValue::Object(std::move(object));
}

[[nodiscard]] JsonValue WriteSegmentRef(const SegmentRef& reference)
{
    JsonObject object;
    object["wireEntityId"] = WriteId(reference.entityId);
    object["segmentId"] = WriteId(reference.segmentId);
    JsonObject range;
    range["first"] = JsonValue::Number(reference.startParameter);
    range["last"] = JsonValue::Number(reference.endParameter);
    object["range"] = JsonValue::Object(std::move(range));
    return JsonValue::Object(std::move(object));
}

//! IDの並びを書く。新しい定義がどれもこの形なので、1か所にまとめる。
[[nodiscard]] JsonValue WriteNumberArray(const std::vector<double>& values)
{
    JsonArray array;
    for (const double value : values) {
        array.push_back(JsonValue::Number(value));
    }
    return JsonValue::Array(std::move(array));
}

[[nodiscard]] JsonValue WriteIdArray(const std::vector<EntityId>& ids)
{
    JsonArray array;
    for (const EntityId& id : ids) {
        array.push_back(WriteId(id));
    }
    return JsonValue::Array(std::move(array));
}

//! 役割1つぶんの鎖。線の参照と、向きを反転するかどうか。
[[nodiscard]] JsonValue WriteChainRef(const domain::WireChainRef& chain)
{
    JsonObject object;
    JsonArray segments;
    for (const SegmentRef& reference : chain.segments) {
        segments.push_back(WriteSegmentRef(reference));
    }
    object["segments"] = JsonValue::Array(std::move(segments));
    JsonArray reversed;
    for (const bool flipped : chain.reversed) {
        reversed.push_back(JsonValue::Bool(flipped));
    }
    object["reversed"] = JsonValue::Array(std::move(reversed));
    return JsonValue::Object(std::move(object));
}

[[nodiscard]] JsonValue WriteDefinition(const Feature& feature)
{
    JsonObject definition;
    if (const auto* point = std::get_if<CreatePointDefinition>(&feature.definition)) {
        definition["position"] = WriteVector(point->positionMm);
        definition["sourcePlaneId"] = WriteOptionalId(point->sourcePlaneId);
        JsonObject expressions;
        expressions["x"] = WriteExpression(point->xExpression);
        expressions["y"] = WriteExpression(point->yExpression);
        expressions["z"] = WriteExpression(point->zExpression);
        definition["expressions"] = JsonValue::Object(std::move(expressions));
    } else if (const auto* wire = std::get_if<CreateWireDefinition>(&feature.definition)) {
        JsonArray segments;
        for (std::size_t index = 0; index < wire->segments.size(); ++index) {
            const SegmentId id = index < wire->segmentIds.size() ? wire->segmentIds[index]
                                                                 : SegmentId{};
            segments.push_back(WriteSegment(wire->segments[index], id));
        }
        JsonObject shape;
        shape["segments"] = JsonValue::Array(std::move(segments));
        shape["closed"] = JsonValue::Bool(false);
        definition["wire"] = JsonValue::Object(std::move(shape));
        definition["sourcePlaneId"] = WriteOptionalId(wire->sourcePlaneId);
        definition["construction"] = JsonValue::Bool(wire->construction);
    } else if (const auto* transform =
                   std::get_if<TransformWireDefinition>(&feature.definition)) {
        definition["method"] = JsonValue::String(
            NameOf(kTransformMethods, transform->method));
        JsonArray inputs;
        for (const SegmentRef& reference : transform->inputs) {
            inputs.push_back(WriteSegmentRef(reference));
        }
        definition["inputs"] = JsonValue::Array(std::move(inputs));
        JsonObject parameters;
        parameters["vector"] = WriteVector(transform->vectorArgument);
        parameters["point"] = WriteVector(transform->pointArgument);
        parameters["scalar"] = WriteExpression(transform->scalarArgument);
        // 面取りの欄(V1 と同じ): B の切戻し・残す側・角番号。無ければ読むときに既定になる。
        parameters["scalar2"] = JsonValue::Number(transform->secondScalarMm);
        parameters["keepFirst"] = JsonValue::Number(
            static_cast<double>(transform->firstKeepSide));
        parameters["keepSecond"] = JsonValue::Number(
            static_cast<double>(transform->secondKeepSide));
        parameters["corner"] = JsonValue::Number(static_cast<double>(transform->cornerIndex));
        definition["parameters"] = JsonValue::Object(std::move(parameters));
    } else if (const auto* freeze =
                   std::get_if<FreezeDerivedDefinition>(&feature.definition)) {
        JsonArray sources;
        for (const EntityId& id : freeze->sources) {
            sources.push_back(WriteId(id));
        }
        definition["sources"] = JsonValue::Array(std::move(sources));
    } else if (const auto* plane =
                   std::get_if<domain::CreateWorkPlaneDefinition>(&feature.definition)) {
        definition["method"] = JsonValue::Number(static_cast<double>(plane->method));
        definition["inputs"] = WriteIdArray(plane->inputs);
        definition["origin"] = WriteVector(plane->origin);
        definition["normal"] = WriteVector(plane->normal);
        definition["uDirection"] = WriteVector(plane->uDirection);
        definition["offset"] = WriteExpression(plane->offset);
        definition["isOriginPlane"] = JsonValue::Bool(plane->isOriginPlane);
    } else if (const auto* project =
                   std::get_if<domain::ProjectWireDefinition>(&feature.definition)) {
        definition["inputs"] = WriteIdArray(project->inputs);
        definition["targetPlaneId"] = WriteId(project->targetPlaneId);
        definition["direction"] = WriteVector(project->direction);
    } else if (const auto* guide =
                   std::get_if<domain::CreateGuideSurfaceDefinition>(&feature.definition)) {
        definition["method"] = JsonValue::Number(static_cast<double>(guide->method));
        JsonArray chains;
        for (const domain::WireChainRef& chain : guide->chains) {
            chains.push_back(WriteChainRef(chain));
        }
        definition["chains"] = JsonValue::Array(std::move(chains));
        JsonArray roles;
        for (const int role : guide->roles) {
            roles.push_back(JsonValue::Number(static_cast<double>(role)));
        }
        definition["roles"] = JsonValue::Array(std::move(roles));
        definition["offsetDistanceMm"] = JsonValue::Number(guide->offsetDistanceMm);
    } else if (const auto* extrude =
                   std::get_if<domain::ExtrudeDefinition>(&feature.definition)) {
        definition["profiles"] = WriteIdArray(extrude->profiles);
        definition["direction"] = WriteVector(extrude->direction);
        definition["distance"] = WriteExpression(extrude->distance);
        definition["extentMode"] = JsonValue::Number(static_cast<double>(extrude->extentMode));
        definition["booleanMode"] = JsonValue::Number(
            static_cast<double>(extrude->booleanMode));
        definition["targets"] = WriteIdArray(extrude->targets);
    } else if (const auto* cage = std::get_if<domain::CreatePartFromWireCageDefinition>(
                   &feature.definition)) {
        definition["wires"] = WriteIdArray(cage->wires);
        definition["thickness"] = WriteExpression(cage->thickness);
        definition["placement"] = JsonValue::Number(static_cast<double>(cage->placement));
    } else if (const auto* boolean =
                   std::get_if<domain::BooleanDefinition>(&feature.definition)) {
        definition["mode"] = JsonValue::Number(static_cast<double>(boolean->mode));
        definition["targets"] = WriteIdArray(boolean->targets);
        definition["tools"] = WriteIdArray(boolean->tools);
    } else if (const auto* fabrication = std::get_if<domain::CreateFabricationModelDefinition>(
                   &feature.definition)) {
        definition["parts"] = WriteIdArray(fabrication->parts);
        definition["materialThickness"] = WriteExpression(fabrication->materialThickness);
        definition["targetMaxDeviation"] = WriteExpression(fabrication->targetMaxDeviation);
        definition["fidelity"] = JsonValue::Number(static_cast<double>(fabrication->fidelity));
        definition["method"] = JsonValue::Number(static_cast<double>(fabrication->method));
        definition["splitAxis"] = JsonValue::Number(
            static_cast<double>(fabrication->splitAxis));
        definition["automaticBoundaries"] = JsonValue::Bool(fabrication->automaticBoundaries);
        definition["maximumPartCount"] = JsonValue::Number(
            static_cast<double>(fabrication->maximumPartCount));
        definition["minimumPartWidthMm"] = JsonValue::Number(fabrication->minimumPartWidthMm);
        definition["manualBoundaries"] = WriteNumberArray(fabrication->manualBoundaries);
        definition["openingWires"] = WriteIdArray(fabrication->openingWires);
        definition["foldWires"] = WriteIdArray(fabrication->foldWires);
        definition["connectionWires"] = WriteIdArray(fabrication->connectionWires);
        definition["masterPercent"] = JsonValue::Number(fabrication->masterPercent);
        // 面の範囲(V1 の plate_range)。無ければ読むときに 0〜1 全体。
        definition["rangeUMin"] = JsonValue::Number(fabrication->rangeUMin);
        definition["rangeUMax"] = JsonValue::Number(fabrication->rangeUMax);
        definition["rangeVMin"] = JsonValue::Number(fabrication->rangeVMin);
        definition["rangeVMax"] = JsonValue::Number(fabrication->rangeVMax);
        definition["creaseProgress"] = WriteNumberArray(fabrication->creaseProgress);
        definition["bandProgress"] = WriteNumberArray(fabrication->bandProgress);
    } else if (const auto* thicken =
                   std::get_if<domain::ThickenSurfaceDefinition>(&feature.definition)) {
        definition["surface"] = WriteId(thicken->surface);
        definition["thickness"] = WriteExpression(thicken->thickness);
        definition["placement"] = JsonValue::Number(static_cast<double>(thicken->placement));
        if (thicken->targetPlane.has_value()) {
            definition["targetPlane"] = WriteId(*thicken->targetPlane);
        }
    } else if (const auto* pattern =
                   std::get_if<domain::CreatePatternDefinition>(&feature.definition)) {
        definition["fabricationModels"] = WriteIdArray(pattern->fabricationModels);
        definition["pageWidth"] = WriteExpression(pattern->pageWidth);
        definition["pageHeight"] = WriteExpression(pattern->pageHeight);
        definition["margin"] = WriteExpression(pattern->marginMm);
    }
    return JsonValue::Object(std::move(definition));
}

[[nodiscard]] JsonValue WriteManufacturing(const ManufacturingProperties& properties)
{
    JsonObject object;
    object["materialName"] = JsonValue::String(properties.materialName);
    object["colorName"] = JsonValue::String(properties.colorName);
    object["processName"] = JsonValue::String(properties.processName);
    object["notes"] = JsonValue::String(properties.note);
    object["layerCount"] = JsonValue::Number(static_cast<double>(properties.layerCount));
    if (properties.nominalThicknessMm.has_value()) {
        object["nominalThicknessMm"] = JsonValue::Number(*properties.nominalThicknessMm);
    }
    if (properties.scaleDenominator.has_value()) {
        object["referenceScaleDenominator"] = JsonValue::Number(*properties.scaleDenominator);
    }
    return JsonValue::Object(std::move(object));
}

[[nodiscard]] JsonValue WriteEntity(const Entity& entity)
{
    JsonObject object;
    object["id"] = WriteId(entity.id);
    object["kind"] = JsonValue::String(NameOf(kEntityKinds, entity.kind));
    object["displayName"] = JsonValue::String(entity.displayName);
    object["groupId"] = WriteOptionalId(entity.groupId);
    object["visibility"] = JsonValue::String(NameOf(kVisibilities, entity.visibility));
    object["editPolicy"] = JsonValue::String(NameOf(kEditPolicies, entity.editPolicy));
    object["createdBy"] = WriteId(entity.createdBy);
    object["revision"] = JsonValue::Number(static_cast<double>(entity.revision));
    object["construction"] = JsonValue::Bool(entity.construction);
    object["datum"] = JsonValue::Bool(entity.datum);
    if (entity.kind == EntityKind::Part) {
        JsonObject part;
        part["purpose"] = JsonValue::String(NameOf(kPartRoles, entity.partRole));
        part["manufacturing"] = entity.manufacturing.has_value()
            ? WriteManufacturing(*entity.manufacturing)
            : JsonValue::Null();
        object["partProperties"] = JsonValue::Object(std::move(part));
    } else {
        object["partProperties"] = JsonValue::Null();
    }
    return JsonValue::Object(std::move(object));
}

[[nodiscard]] JsonValue WriteFeature(const Feature& feature)
{
    JsonObject object;
    object["id"] = WriteId(feature.id);
    object["type"] = JsonValue::String(NameOf(kFeatureTypes, feature.type));
    object["displayName"] = JsonValue::String(feature.displayName);
    object["enabled"] = JsonValue::Bool(feature.enabled);
    object["revision"] = JsonValue::Number(static_cast<double>(feature.revision));
    object["definition"] = WriteDefinition(feature);
    JsonArray outputs;
    for (const FeatureOutput& output : feature.outputs) {
        JsonObject item;
        item["key"] = JsonValue::String(output.key);
        item["entityId"] = WriteId(output.entityId);
        item["kind"] = JsonValue::String(NameOf(kEntityKinds, output.kind));
        outputs.push_back(JsonValue::Object(std::move(item)));
    }
    object["outputs"] = JsonValue::Array(std::move(outputs));
    JsonArray inputs;
    for (const EntityId& id : feature.inputEntityIds) {
        inputs.push_back(WriteId(id));
    }
    object["inputEntityIds"] = JsonValue::Array(std::move(inputs));
    // 派生物の置き場。作業中グループとは別に覚える(§11)。
    object["derivedGroupId"] = WriteOptionalId(feature.derivedGroupId);
    return JsonValue::Object(std::move(object));
}

//! Groupの子の並び。Group自身とEntityを、断面の並び順のまま入れる。
[[nodiscard]] JsonArray ChildOrderOf(const DocumentSnapshot& snapshot,
    const std::optional<GroupId>& parent)
{
    JsonArray order;
    for (const Group& group : snapshot.groups) {
        if (group.parentId == parent) {
            order.push_back(WriteId(group.id));
        }
    }
    for (const Entity& entity : snapshot.entities) {
        if (entity.groupId == parent) {
            order.push_back(WriteId(entity.id));
        }
    }
    return order;
}

} // namespace

std::string WriteDocumentJson(const DocumentFile& file)
{
    const DocumentSnapshot& snapshot = file.snapshot;
    JsonObject root;
    root["format"] = JsonValue::String(kFormatName);
    root["schemaVersion"] = JsonValue::Number(kSchemaVersion);
    root["documentId"] = WriteId(snapshot.id);
    root["revision"] = JsonValue::Number(static_cast<double>(snapshot.revision));

    JsonObject units;
    units["length"] = JsonValue::String("mm");
    units["storedAngle"] = JsonValue::String("rad");
    units["displayAngle"] = JsonValue::String("deg");
    root["units"] = JsonValue::Object(std::move(units));

    const geometry::GeometryTolerance& tolerance = snapshot.settings.tolerance;
    JsonObject tolerances;
    tolerances["numericEpsilon"] = JsonValue::Number(tolerance.numericEpsilon);
    tolerances["modelLinearMm"] = JsonValue::Number(tolerance.modelLinearMm);
    tolerances["modelAngularRad"] = JsonValue::Number(tolerance.modelAngularRad);
    tolerances["interactiveJoinMm"] = JsonValue::Number(tolerance.interactiveJoinMm);
    tolerances["displayPickPx"] = JsonValue::Number(tolerance.displayPickPx);
    tolerances["candidateMenuPx"] = JsonValue::Number(tolerance.candidateMenuPx);
    root["tolerances"] = JsonValue::Object(std::move(tolerances));

    JsonObject metadata;
    metadata["title"] = JsonValue::String(file.metadata.title);
    metadata["author"] = JsonValue::String(file.metadata.author);
    metadata["description"] = JsonValue::String(file.metadata.description);
    root["metadata"] = JsonValue::Object(std::move(metadata));

    root["activeGroupId"] = WriteOptionalId(snapshot.settings.activeGroupId);

    JsonArray groups;
    for (const Group& group : snapshot.groups) {
        JsonObject object;
        object["id"] = WriteId(group.id);
        object["displayName"] = JsonValue::String(group.displayName);
        object["parentGroupId"] = WriteOptionalId(group.parentId);
        object["state"] = JsonValue::String("visible");
        object["childOrder"] = JsonValue::Array(ChildOrderOf(snapshot, group.id));
        groups.push_back(JsonValue::Object(std::move(object)));
    }
    root["groups"] = JsonValue::Array(std::move(groups));

    JsonArray entities;
    for (const Entity& entity : snapshot.entities) {
        entities.push_back(WriteEntity(entity));
    }
    root["entities"] = JsonValue::Array(std::move(entities));

    JsonArray features;
    for (const Feature& feature : snapshot.features) {
        features.push_back(WriteFeature(feature));
    }
    root["features"] = JsonValue::Array(std::move(features));

    JsonArray dimensions;
    for (const ReferenceDimension& dimension : snapshot.referenceDimensions) {
        JsonObject object;
        object["id"] = WriteId(dimension.id);
        object["label"] = JsonValue::String(dimension.label);
        object["kind"] = JsonValue::String(dimension.kind);
        JsonArray targets;
        for (const EntityId& target : dimension.targets) {
            targets.push_back(WriteId(target));
        }
        object["targets"] = JsonValue::Array(std::move(targets));
        JsonArray parameters;
        for (const double value : dimension.parameters) {
            parameters.push_back(JsonValue::Number(value));
        }
        object["parameters"] = JsonValue::Array(std::move(parameters));
        object["recordedValue"] = JsonValue::Number(dimension.recordedValue);
        object["unit"] = JsonValue::String(dimension.unit);
        object["note"] = JsonValue::String(dimension.noteJa);
        dimensions.push_back(JsonValue::Object(std::move(object)));
    }
    root["referenceDimensions"] = JsonValue::Array(std::move(dimensions));

    root["rootOrder"] = JsonValue::Array(ChildOrderOf(snapshot, std::nullopt));
    root["uiState"] = file.uiState.IsObject() ? file.uiState : JsonValue::Object({});
    root["assets"] = JsonValue::Array({});

    return WriteJson(JsonValue::Object(std::move(root)));
}

} // namespace kachakacha::v2::io
