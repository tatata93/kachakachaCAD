#include "kachakacha/io/DocumentFile.h"

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

namespace {

constexpr const char* kFormatName = "kachakachaCAD";
constexpr int kSchemaVersion = 2;
constexpr const char* kDocumentEntry = "document.json";

constexpr const char* kNotOurFile = "KCD2-D001";  //!< format / schemaVersion が違う
constexpr const char* kBadValue   = "KCD2-D002";  //!< 型違い、範囲外、UUIDでない
constexpr const char* kBadEnum    = "KCD2-D003";  //!< 知らない enum
constexpr const char* kBadShape   = "KCD2-D004";  //!< 参照切れ、重複ID、構造の矛盾
constexpr const char* kNoDocument = "KCD2-D005";  //!< document.json が無い

// ---------------------------------------------------------------- enum 表
// 名前は kcd2-format.md の lower snake_case。並びは enum 定義順。

template<class Enumeration>
struct NamedEnum {
    Enumeration value;
    const char* name;
};

constexpr NamedEnum<EntityKind> kEntityKinds[]{
    {EntityKind::Point, "point"},
    {EntityKind::WorkPlane, "work_plane"},
    {EntityKind::Wire, "wire"},
    {EntityKind::GuideSurface, "guide_surface"},
    {EntityKind::Part, "part"},
    {EntityKind::FabricationModel, "fabrication_model"},
    {EntityKind::Pattern, "pattern"},
};

constexpr NamedEnum<Visibility> kVisibilities[]{
    {Visibility::Visible, "visible"},
    {Visibility::Reference, "reference"},
    {Visibility::Hidden, "hidden"},
};

constexpr NamedEnum<EditPolicy> kEditPolicies[]{
    {EditPolicy::Source, "source"},
    {EditPolicy::Derived, "derived"},
    {EditPolicy::Frozen, "frozen"},
};

constexpr NamedEnum<PartRole> kPartRoles[]{
    {PartRole::FinishedModel, "finished_model"},
    {PartRole::FabricationPart, "fabrication_part"},
    {PartRole::Fixture, "fixture"},
    {PartRole::Imported, "imported"},
};

constexpr NamedEnum<FeatureType> kFeatureTypes[]{
    {FeatureType::CreatePoint, "create_point"},
    {FeatureType::CreateWorkPlane, "create_work_plane"},
    {FeatureType::CreateWire, "create_wire"},
    {FeatureType::TransformWire, "transform_wire"},
    {FeatureType::ProjectWire, "project_wire"},
    {FeatureType::CreateGuideSurface, "create_guide_surface"},
    {FeatureType::Extrude, "extrude"},
    {FeatureType::CreatePartFromWireCage, "create_part_from_wire_cage"},
    {FeatureType::Boolean, "boolean"},
    {FeatureType::CreateFabricationModel, "create_fabrication_model"},
    {FeatureType::CreatePattern, "create_pattern"},
    {FeatureType::FreezeDerived, "freeze_derived"},
};

constexpr NamedEnum<WireTransformMethod> kTransformMethods[]{
    {WireTransformMethod::Move, "move"},
    {WireTransformMethod::Copy, "copy"},
    {WireTransformMethod::Rotate, "rotate"},
    {WireTransformMethod::Mirror, "mirror"},
    {WireTransformMethod::Trim, "trim"},
    {WireTransformMethod::Extend, "extend"},
    {WireTransformMethod::Split, "split"},
    {WireTransformMethod::Join, "join"},
    {WireTransformMethod::Fillet, "fillet"},
    {WireTransformMethod::Chamfer, "chamfer"},
    {WireTransformMethod::Offset, "offset"},
    {WireTransformMethod::MeetLines, "meet_lines"},
    {WireTransformMethod::Coincident, "coincident"},
    {WireTransformMethod::Tangent, "tangent"},
    {WireTransformMethod::Curvature, "curvature"},
};

constexpr NamedEnum<CurveKind> kCurveKinds[]{
    {CurveKind::Line, "line"},
    {CurveKind::CircularArc, "circular_arc"},
    {CurveKind::Circle, "circle"},
    {CurveKind::CubicBezier, "cubic_bezier"},
    {CurveKind::CubicBSpline, "cubic_b_spline"},
};

constexpr NamedEnum<QuantityKind> kQuantityKinds[]{
    {QuantityKind::Length, "length"},
    {QuantityKind::Angle, "angle"},
    {QuantityKind::Scalar, "scalar"},
};

template<class Enumeration, std::size_t Count>
[[nodiscard]] const char* NameOf(const NamedEnum<Enumeration> (&table)[Count],
    Enumeration value)
{
    for (const NamedEnum<Enumeration>& item : table) {
        if (item.value == value) {
            return item.name;
        }
    }
    return "";
}

template<class Enumeration, std::size_t Count>
[[nodiscard]] std::optional<Enumeration> ValueOf(const NamedEnum<Enumeration> (&table)[Count],
    const std::string& name)
{
    for (const NamedEnum<Enumeration>& item : table) {
        if (name == item.name) {
            return item.value;
        }
    }
    return std::nullopt;
}

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
        definition["parameters"] = JsonValue::Object(std::move(parameters));
    } else if (const auto* freeze =
                   std::get_if<FreezeDerivedDefinition>(&feature.definition)) {
        JsonArray sources;
        for (const EntityId& id : freeze->sources) {
            sources.push_back(WriteId(id));
        }
        definition["sources"] = JsonValue::Array(std::move(sources));
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

namespace {

// ---------------------------------------------------------------- 読み込み

//! 読み取り中の失敗をまとめる。1件見つけたらそこで諦めず、集めて返す。
class Loader {
public:
    [[nodiscard]] bool Failed() const noexcept { return !diagnostics_.empty(); }
    [[nodiscard]] std::vector<Diagnostic> Take() { return std::move(diagnostics_); }

    void Fail(const char* code, std::string summary, std::string details = {})
    {
        diagnostics_.push_back(MakeError(code, std::move(summary), std::move(details)));
    }

    void Warn(std::string summary, std::string details = {})
    {
        diagnostics_.push_back(MakeWarning("KCD2-D100", std::move(summary), std::move(details)));
    }

    template<class Id>
    [[nodiscard]] Id ParseId(const std::string& text, const std::string& where)
    {
        const std::optional<Id> parsed = Id::Parse(text);
        if (!parsed.has_value()) {
            Fail(kBadValue, "IDの書き方が正しくありません。", where + ": " + text);
            return Id{};
        }
        return *parsed;
    }

    template<class Id>
    [[nodiscard]] std::optional<Id> ParseOptionalId(const JsonValue* value,
        const std::string& where)
    {
        if (value == nullptr || value->IsNull()) {
            return std::nullopt;
        }
        if (value->Type() != JsonType::String) {
            Fail(kBadValue, "IDは文字列かnullでなければなりません。", where);
            return std::nullopt;
        }
        return ParseId<Id>(value->AsString(), where);
    }

    template<class Enumeration, std::size_t Count>
    [[nodiscard]] Enumeration ParseEnum(const NamedEnum<Enumeration> (&table)[Count],
        const std::string& name, const std::string& where, Enumeration fallback)
    {
        const std::optional<Enumeration> value = ValueOf(table, name);
        if (!value.has_value()) {
            Fail(kBadEnum, "知らない種類が入っています。", where + ": " + name);
            return fallback;
        }
        return *value;
    }

    [[nodiscard]] double Number(const JsonValue& parent, const char* key,
        const std::string& where)
    {
        const JsonValue* found = parent.Find(key);
        if (found == nullptr) {
            Fail(kBadValue, "必要な項目がありません。", where + "." + key);
            return 0.0;
        }
        if (found->Type() != JsonType::Number) {
            Fail(kBadValue, "数であるべき項目が数ではありません。", where + "." + key);
            return 0.0;
        }
        return found->AsNumber();
    }

    [[nodiscard]] std::string String(const JsonValue& parent, const char* key,
        const std::string& where)
    {
        const JsonValue* found = parent.Find(key);
        if (found == nullptr) {
            Fail(kBadValue, "必要な項目がありません。", where + "." + key);
            return {};
        }
        if (found->Type() != JsonType::String) {
            Fail(kBadValue, "文字列であるべき項目が文字列ではありません。",
                where + "." + key);
            return {};
        }
        return found->AsString();
    }

    [[nodiscard]] bool Bool(const JsonValue& parent, const char* key,
        const std::string& where, bool fallback)
    {
        const JsonValue* found = parent.Find(key);
        if (found == nullptr) {
            return fallback;
        }
        if (found->Type() != JsonType::Bool) {
            Fail(kBadValue, "真偽であるべき項目が真偽ではありません。", where + "." + key);
            return fallback;
        }
        return found->AsBool();
    }

    [[nodiscard]] const JsonArray* ArrayAt(const JsonValue& parent, const char* key,
        const std::string& where)
    {
        const JsonValue* found = parent.Find(key);
        if (found == nullptr) {
            Fail(kBadValue, "必要な配列がありません。", where + "." + key);
            return nullptr;
        }
        if (!found->IsArray()) {
            Fail(kBadValue, "配列であるべき項目が配列ではありません。", where + "." + key);
            return nullptr;
        }
        return &found->AsArray();
    }

    [[nodiscard]] const JsonValue* ObjectAt(const JsonValue& parent, const char* key,
        const std::string& where)
    {
        const JsonValue* found = parent.Find(key);
        if (found == nullptr) {
            Fail(kBadValue, "必要な項目がありません。", where + "." + key);
            return nullptr;
        }
        if (!found->IsObject()) {
            Fail(kBadValue, "組であるべき項目が組ではありません。", where + "." + key);
            return nullptr;
        }
        return found;
    }

    [[nodiscard]] Vector3 ReadVector(const JsonValue& parent, const char* key,
        const std::string& where)
    {
        const JsonValue* object = ObjectAt(parent, key, where);
        if (object == nullptr) {
            return {};
        }
        const std::string place = where + "." + key;
        Vector3 value{Number(*object, "x", place), Number(*object, "y", place),
            Number(*object, "z", place)};
        if (!value.IsFinite()) {
            Fail(kBadValue, "座標に有限でない数が入っています。", place);
        }
        return value;
    }

    [[nodiscard]] EvaluatedValue ReadExpression(const JsonValue& parent, const char* key,
        const std::string& where)
    {
        const JsonValue* object = ObjectAt(parent, key, where);
        if (object == nullptr) {
            return {};
        }
        const std::string place = where + "." + key;
        EvaluatedValue value;
        value.expression = String(*object, "expression", place);
        value.value = Number(*object, "value", place);
        value.kind = ParseEnum(kQuantityKinds, String(*object, "quantity", place), place,
            QuantityKind::Scalar);
        if (!geometry::IsFinite(value.value)) {
            Fail(kBadValue, "式の値が有限ではありません。", place);
        }
        return value;
    }

private:
    std::vector<Diagnostic> diagnostics_;
};

[[nodiscard]] std::pair<CurveSegment, SegmentId> ReadSegment(Loader& loader,
    const JsonValue& value, const std::string& where)
{
    const CurveSegment fallback = CurveSegment::MakeLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0})
                                      .Value();
    if (!value.IsObject()) {
        loader.Fail(kBadValue, "線の記述が組ではありません。", where);
        return {fallback, SegmentId{}};
    }
    const SegmentId id = loader.ParseId<SegmentId>(loader.String(value, "id", where),
        where + ".id");
    const std::string typeName = loader.String(value, "type", where);
    const CurveKind kind = loader.ParseEnum(kCurveKinds, typeName, where + ".type",
        CurveKind::Line);

    base::Result<CurveSegment> made = base::Result<CurveSegment>::Failure(
        MakeError(kBadValue, "未処理", where));
    switch (kind) {
    case CurveKind::Line:
        made = CurveSegment::MakeLine(loader.ReadVector(value, "start", where),
            loader.ReadVector(value, "end", where));
        break;
    case CurveKind::CircularArc:
        made = CurveSegment::MakeCircularArc(loader.ReadVector(value, "center", where),
            loader.ReadVector(value, "normal", where),
            loader.ReadVector(value, "xDirection", where),
            loader.Number(value, "radiusMm", where),
            loader.Number(value, "startAngleRad", where),
            loader.Number(value, "sweepAngleRad", where));
        break;
    case CurveKind::Circle:
        made = CurveSegment::MakeCircle(loader.ReadVector(value, "center", where),
            loader.ReadVector(value, "normal", where),
            loader.ReadVector(value, "xDirection", where),
            loader.Number(value, "radiusMm", where));
        break;
    case CurveKind::CubicBezier:
    case CurveKind::CubicBSpline: {
        const JsonArray* points = loader.ArrayAt(value, "controlPoints", where);
        std::vector<Vector3> controlPoints;
        if (points != nullptr) {
            for (std::size_t index = 0; index < points->size(); ++index) {
                const JsonValue& item = (*points)[index];
                const std::string place = where + ".controlPoints[" + std::to_string(index) + "]";
                if (!item.IsObject()) {
                    loader.Fail(kBadValue, "制御点が組ではありません。", place);
                    continue;
                }
                Vector3 point{loader.Number(item, "x", place), loader.Number(item, "y", place),
                    loader.Number(item, "z", place)};
                controlPoints.push_back(point);
            }
        }
        made = kind == CurveKind::CubicBezier
            ? CurveSegment::MakeCubicBezier(std::move(controlPoints))
            : CurveSegment::MakeCubicBSpline(std::move(controlPoints));
        break;
    }
    }
    if (!made.HasValue()) {
        // 幾何として成り立たないものを、近い形へ黙って直さない。
        std::string reason = made.Diagnostics().empty() ? std::string()
                                                        : made.Diagnostics().front().summaryJa;
        loader.Fail(kBadValue, "線として成り立たない記述です。", where + ": " + reason);
        return {fallback, id};
    }
    return {made.Value(), id};
}

[[nodiscard]] SegmentRef ReadSegmentRef(Loader& loader, const JsonValue& value,
    const std::string& where)
{
    SegmentRef reference;
    if (!value.IsObject()) {
        loader.Fail(kBadValue, "参照が組ではありません。", where);
        return reference;
    }
    reference.entityId = loader.ParseId<EntityId>(
        loader.String(value, "wireEntityId", where), where + ".wireEntityId");
    reference.segmentId = loader.ParseId<SegmentId>(
        loader.String(value, "segmentId", where), where + ".segmentId");
    const JsonValue* range = loader.ObjectAt(value, "range", where);
    if (range != nullptr) {
        reference.startParameter = loader.Number(*range, "first", where + ".range");
        reference.endParameter = loader.Number(*range, "last", where + ".range");
    }
    return reference;
}

void ReadDefinition(Loader& loader, Feature& feature, const JsonValue& definition,
    const std::string& where)
{
    switch (feature.type) {
    case FeatureType::CreatePoint: {
        CreatePointDefinition made;
        made.positionMm = loader.ReadVector(definition, "position", where);
        made.sourcePlaneId = loader.ParseOptionalId<EntityId>(
            definition.Find("sourcePlaneId"), where + ".sourcePlaneId");
        const JsonValue* expressions = loader.ObjectAt(definition, "expressions", where);
        if (expressions != nullptr) {
            const std::string place = where + ".expressions";
            made.xExpression = loader.ReadExpression(*expressions, "x", place);
            made.yExpression = loader.ReadExpression(*expressions, "y", place);
            made.zExpression = loader.ReadExpression(*expressions, "z", place);
        }
        feature.definition = std::move(made);
        break;
    }
    case FeatureType::CreateWire: {
        CreateWireDefinition made;
        const JsonValue* wire = loader.ObjectAt(definition, "wire", where);
        if (wire != nullptr) {
            const std::string place = where + ".wire";
            const JsonArray* segments = loader.ArrayAt(*wire, "segments", place);
            if (segments != nullptr) {
                for (std::size_t index = 0; index < segments->size(); ++index) {
                    auto [segment, id] = ReadSegment(loader, (*segments)[index],
                        place + ".segments[" + std::to_string(index) + "]");
                    made.segments.push_back(std::move(segment));
                    made.segmentIds.push_back(id);
                }
            }
        }
        made.sourcePlaneId = loader.ParseOptionalId<EntityId>(
            definition.Find("sourcePlaneId"), where + ".sourcePlaneId");
        made.construction = loader.Bool(definition, "construction", where, false);
        feature.definition = std::move(made);
        break;
    }
    case FeatureType::TransformWire: {
        TransformWireDefinition made;
        made.method = loader.ParseEnum(kTransformMethods,
            loader.String(definition, "method", where), where + ".method",
            WireTransformMethod::Move);
        const JsonArray* inputs = loader.ArrayAt(definition, "inputs", where);
        if (inputs != nullptr) {
            for (std::size_t index = 0; index < inputs->size(); ++index) {
                made.inputs.push_back(ReadSegmentRef(loader, (*inputs)[index],
                    where + ".inputs[" + std::to_string(index) + "]"));
            }
        }
        const JsonValue* parameters = loader.ObjectAt(definition, "parameters", where);
        if (parameters != nullptr) {
            const std::string place = where + ".parameters";
            made.vectorArgument = loader.ReadVector(*parameters, "vector", place);
            made.pointArgument = loader.ReadVector(*parameters, "point", place);
            made.scalarArgument = loader.ReadExpression(*parameters, "scalar", place);
        }
        feature.definition = std::move(made);
        break;
    }
    case FeatureType::FreezeDerived: {
        FreezeDerivedDefinition made;
        const JsonArray* sources = loader.ArrayAt(definition, "sources", where);
        if (sources != nullptr) {
            for (std::size_t index = 0; index < sources->size(); ++index) {
                const JsonValue& item = (*sources)[index];
                if (item.Type() != JsonType::String) {
                    loader.Fail(kBadValue, "IDは文字列でなければなりません。",
                        where + ".sources[" + std::to_string(index) + "]");
                    continue;
                }
                made.sources.push_back(loader.ParseId<EntityId>(item.AsString(),
                    where + ".sources[" + std::to_string(index) + "]"));
            }
        }
        feature.definition = std::move(made);
        break;
    }
    default:
        // まだ定義を実装していないFeature。空でなければならない。
        if (!definition.AsObject().empty()) {
            loader.Warn("この版ではまだ扱えない指示が入っています。",
                where + " (" + std::string(FeatureTypeName(feature.type)) + ")");
        }
        feature.definition = std::monostate{};
        break;
    }
}

} // namespace

Result<DocumentFile> ReadDocumentJson(std::string_view text)
{
    const auto parsed = ParseJson(text);
    if (!parsed.HasValue()) {
        return Result<DocumentFile>::Failure(parsed.Diagnostics());
    }
    const JsonValue& root = parsed.Value();
    Loader loader;
    if (!root.IsObject()) {
        loader.Fail(kNotOurFile, "文書の中身が組ではありません。", "$");
        return Result<DocumentFile>::Failure(loader.Take());
    }
    const std::string format = loader.String(root, "format", "$");
    if (format != kFormatName) {
        loader.Fail(kNotOurFile, "このソフトの文書ではありません。", "format=" + format);
        return Result<DocumentFile>::Failure(loader.Take());
    }
    const double schemaVersion = loader.Number(root, "schemaVersion", "$");
    if (schemaVersion != static_cast<double>(kSchemaVersion)) {
        loader.Fail(kNotOurFile, "対応していない版の文書です。",
            "schemaVersion=" + std::to_string(static_cast<long long>(schemaVersion))
                + " (このソフトは " + std::to_string(kSchemaVersion) + ")");
        return Result<DocumentFile>::Failure(loader.Take());
    }

    DocumentFile file;
    DocumentSnapshot& snapshot = file.snapshot;
    snapshot.id = loader.ParseId<DocumentId>(loader.String(root, "documentId", "$"),
        "$.documentId");
    const double revision = loader.Number(root, "revision", "$");
    if (!(revision >= 0.0) || revision != std::floor(revision)) {
        loader.Fail(kBadValue, "版数が正しくありません。", "$.revision");
    }
    snapshot.revision = static_cast<std::uint64_t>(std::max(0.0, revision));

    if (const JsonValue* tolerances = loader.ObjectAt(root, "tolerances", "$")) {
        geometry::GeometryTolerance& tolerance = snapshot.settings.tolerance;
        tolerance.numericEpsilon = loader.Number(*tolerances, "numericEpsilon", "$.tolerances");
        tolerance.modelLinearMm = loader.Number(*tolerances, "modelLinearMm", "$.tolerances");
        tolerance.modelAngularRad =
            loader.Number(*tolerances, "modelAngularRad", "$.tolerances");
        tolerance.interactiveJoinMm =
            loader.Number(*tolerances, "interactiveJoinMm", "$.tolerances");
        if (const JsonValue* found = tolerances->Find("displayPickPx")) {
            if (found->Type() == JsonType::Number) {
                tolerance.displayPickPx = found->AsNumber();
            }
        }
        if (const JsonValue* found = tolerances->Find("candidateMenuPx")) {
            if (found->Type() == JsonType::Number) {
                tolerance.candidateMenuPx = found->AsNumber();
            }
        }
        if (!(tolerance.modelLinearMm > 0.0) || !(tolerance.interactiveJoinMm > 0.0)
            || !(tolerance.numericEpsilon > 0.0) || !(tolerance.modelAngularRad > 0.0)) {
            loader.Fail(kBadValue, "許容差は正の値でなければなりません。", "$.tolerances");
        }
    }

    if (const JsonValue* metadata = loader.ObjectAt(root, "metadata", "$")) {
        file.metadata.title = loader.String(*metadata, "title", "$.metadata");
        file.metadata.author = loader.String(*metadata, "author", "$.metadata");
        file.metadata.description = loader.String(*metadata, "description", "$.metadata");
    }

    snapshot.settings.activeGroupId = loader.ParseOptionalId<GroupId>(
        root.Find("activeGroupId"), "$.activeGroupId");

    if (const JsonArray* groups = loader.ArrayAt(root, "groups", "$")) {
        for (std::size_t index = 0; index < groups->size(); ++index) {
            const JsonValue& item = (*groups)[index];
            const std::string where = "$.groups[" + std::to_string(index) + "]";
            if (!item.IsObject()) {
                loader.Fail(kBadValue, "グループの記述が組ではありません。", where);
                continue;
            }
            Group group;
            group.id = loader.ParseId<GroupId>(loader.String(item, "id", where), where + ".id");
            group.displayName = loader.String(item, "displayName", where);
            group.parentId = loader.ParseOptionalId<GroupId>(item.Find("parentGroupId"),
                where + ".parentGroupId");
            snapshot.groups.push_back(std::move(group));
        }
    }

    if (const JsonArray* entities = loader.ArrayAt(root, "entities", "$")) {
        for (std::size_t index = 0; index < entities->size(); ++index) {
            const JsonValue& item = (*entities)[index];
            const std::string where = "$.entities[" + std::to_string(index) + "]";
            if (!item.IsObject()) {
                loader.Fail(kBadValue, "オブジェクトの記述が組ではありません。", where);
                continue;
            }
            Entity entity;
            entity.id = loader.ParseId<EntityId>(loader.String(item, "id", where), where + ".id");
            entity.kind = loader.ParseEnum(kEntityKinds, loader.String(item, "kind", where),
                where + ".kind", EntityKind::Point);
            entity.displayName = loader.String(item, "displayName", where);
            entity.groupId = loader.ParseOptionalId<GroupId>(item.Find("groupId"),
                where + ".groupId");
            entity.visibility = loader.ParseEnum(kVisibilities,
                loader.String(item, "visibility", where), where + ".visibility",
                Visibility::Visible);
            entity.editPolicy = loader.ParseEnum(kEditPolicies,
                loader.String(item, "editPolicy", where), where + ".editPolicy",
                EditPolicy::Source);
            entity.createdBy = loader.ParseId<FeatureId>(loader.String(item, "createdBy", where),
                where + ".createdBy");
            entity.revision = static_cast<std::uint64_t>(
                std::max(0.0, loader.Number(item, "revision", where)));
            entity.construction = loader.Bool(item, "construction", where, false);
            entity.datum = loader.Bool(item, "datum", where, false);

            const JsonValue* partProperties = item.Find("partProperties");
            const bool hasPart = partProperties != nullptr && partProperties->IsObject();
            if (entity.kind == EntityKind::Part && !hasPart) {
                loader.Fail(kBadShape, "部品には部品の属性が必要です。",
                    where + ".partProperties");
            }
            if (entity.kind != EntityKind::Part && hasPart) {
                loader.Fail(kBadShape, "部品以外に部品の属性が付いています。",
                    where + ".partProperties");
            }
            if (hasPart) {
                const std::string place = where + ".partProperties";
                entity.partRole = loader.ParseEnum(kPartRoles,
                    loader.String(*partProperties, "purpose", place), place + ".purpose",
                    PartRole::FinishedModel);
                const JsonValue* manufacturing = partProperties->Find("manufacturing");
                if (manufacturing != nullptr && manufacturing->IsObject()) {
                    const std::string spot = place + ".manufacturing";
                    ManufacturingProperties properties;
                    properties.materialName = loader.String(*manufacturing, "materialName", spot);
                    properties.colorName = loader.String(*manufacturing, "colorName", spot);
                    properties.processName = loader.String(*manufacturing, "processName", spot);
                    properties.note = loader.String(*manufacturing, "notes", spot);
                    properties.layerCount = static_cast<int>(
                        loader.Number(*manufacturing, "layerCount", spot));
                    if (properties.layerCount < 1) {
                        loader.Fail(kBadValue, "重ね枚数は1以上でなければなりません。", spot);
                    }
                    if (const JsonValue* found = manufacturing->Find("nominalThicknessMm")) {
                        if (found->Type() != JsonType::Number || !(found->AsNumber() > 0.0)) {
                            loader.Fail(kBadValue, "板厚は正の値でなければなりません。", spot);
                        } else {
                            properties.nominalThicknessMm = found->AsNumber();
                        }
                    }
                    if (const JsonValue* found =
                            manufacturing->Find("referenceScaleDenominator")) {
                        if (found->Type() != JsonType::Number || !(found->AsNumber() > 0.0)) {
                            loader.Fail(kBadValue, "縮尺は正の値でなければなりません。", spot);
                        } else {
                            properties.scaleDenominator = found->AsNumber();
                        }
                    }
                    entity.manufacturing = std::move(properties);
                }
            }
            snapshot.entities.push_back(std::move(entity));
        }
    }

    if (const JsonArray* features = loader.ArrayAt(root, "features", "$")) {
        for (std::size_t index = 0; index < features->size(); ++index) {
            const JsonValue& item = (*features)[index];
            const std::string where = "$.features[" + std::to_string(index) + "]";
            if (!item.IsObject()) {
                loader.Fail(kBadValue, "指示の記述が組ではありません。", where);
                continue;
            }
            Feature feature;
            feature.id = loader.ParseId<FeatureId>(loader.String(item, "id", where),
                where + ".id");
            feature.type = loader.ParseEnum(kFeatureTypes, loader.String(item, "type", where),
                where + ".type", FeatureType::CreatePoint);
            feature.displayName = loader.String(item, "displayName", where);
            feature.enabled = loader.Bool(item, "enabled", where, true);
            feature.revision = static_cast<std::uint64_t>(
                std::max(0.0, loader.Number(item, "revision", where)));

            if (const JsonValue* definition = loader.ObjectAt(item, "definition", where)) {
                ReadDefinition(loader, feature, *definition, where + ".definition");
            }

            std::set<std::string> keys;
            if (const JsonArray* outputs = loader.ArrayAt(item, "outputs", where)) {
                for (std::size_t at = 0; at < outputs->size(); ++at) {
                    const JsonValue& output = (*outputs)[at];
                    const std::string place = where + ".outputs[" + std::to_string(at) + "]";
                    if (!output.IsObject()) {
                        loader.Fail(kBadValue, "出力の記述が組ではありません。", place);
                        continue;
                    }
                    FeatureOutput made;
                    made.key = loader.String(output, "key", place);
                    made.entityId = loader.ParseId<EntityId>(
                        loader.String(output, "entityId", place), place + ".entityId");
                    made.kind = loader.ParseEnum(kEntityKinds,
                        loader.String(output, "kind", place), place + ".kind", EntityKind::Point);
                    if (!keys.insert(made.key).second) {
                        loader.Fail(kBadShape, "同じ指示の中で出力の名前が重なっています。",
                            place + ".key=" + made.key);
                    }
                    feature.outputs.push_back(std::move(made));
                }
            }
            if (const JsonArray* inputs = loader.ArrayAt(item, "inputEntityIds", where)) {
                for (std::size_t at = 0; at < inputs->size(); ++at) {
                    const JsonValue& input = (*inputs)[at];
                    const std::string place = where + ".inputEntityIds[" + std::to_string(at) + "]";
                    if (input.Type() != JsonType::String) {
                        loader.Fail(kBadValue, "IDは文字列でなければなりません。", place);
                        continue;
                    }
                    feature.inputEntityIds.push_back(
                        loader.ParseId<EntityId>(input.AsString(), place));
                }
            }
            feature.derivedGroupId = loader.ParseOptionalId<GroupId>(
                item.Find("derivedGroupId"), where + ".derivedGroupId");
            snapshot.features.push_back(std::move(feature));
        }
    }

    // 残した参照寸法。古い文書には無いので、無ければ空のままにする。
    if (const JsonValue* found = root.Find("referenceDimensions")) {
        if (!found->IsArray()) {
            loader.Fail(kBadValue, "残した寸法の一覧が配列ではありません。",
                "$.referenceDimensions");
        } else {
            const JsonArray& dimensions = found->AsArray();
            for (std::size_t index = 0; index < dimensions.size(); ++index) {
                const JsonValue& item = dimensions[index];
                const std::string where =
                    "$.referenceDimensions[" + std::to_string(index) + "]";
                if (!item.IsObject()) {
                    loader.Fail(kBadValue, "寸法の記述が組ではありません。", where);
                    continue;
                }
                ReferenceDimension dimension;
                dimension.id = loader.ParseId<DimensionId>(loader.String(item, "id", where),
                    where + ".id");
                dimension.label = loader.String(item, "label", where);
                dimension.kind = loader.String(item, "kind", where);
                dimension.recordedValue = loader.Number(item, "recordedValue", where);
                dimension.unit = loader.String(item, "unit", where);
                dimension.noteJa = loader.String(item, "note", where);
                if (const JsonArray* targets = loader.ArrayAt(item, "targets", where)) {
                    for (std::size_t at = 0; at < targets->size(); ++at) {
                        const JsonValue& target = (*targets)[at];
                        const std::string place =
                            where + ".targets[" + std::to_string(at) + "]";
                        if (target.Type() != JsonType::String) {
                            loader.Fail(kBadValue, "IDは文字列でなければなりません。",
                                place);
                            continue;
                        }
                        dimension.targets.push_back(
                            loader.ParseId<EntityId>(target.AsString(), place));
                    }
                }
                if (const JsonArray* parameters =
                        loader.ArrayAt(item, "parameters", where)) {
                    for (std::size_t at = 0; at < parameters->size(); ++at) {
                        const JsonValue& value = (*parameters)[at];
                        if (value.Type() != JsonType::Number) {
                            loader.Fail(kBadValue, "位置は数でなければなりません。",
                                where + ".parameters[" + std::to_string(at) + "]");
                            continue;
                        }
                        dimension.parameters.push_back(value.AsNumber());
                    }
                }
                snapshot.referenceDimensions.push_back(std::move(dimension));
            }
        }
    }

    if (const JsonValue* uiState = root.Find("uiState")) {
        if (!uiState->IsObject()) {
            loader.Fail(kBadValue, "画面の状態が組ではありません。", "$.uiState");
        } else {
            file.uiState = *uiState;
        }
    }

    // 参照の整合。ここまでで型は合っている前提で、つながりを見る。
    if (!loader.Failed()) {
        for (const Entity& entity : snapshot.entities) {
            if (entity.groupId.has_value()) {
                const bool found = std::any_of(snapshot.groups.begin(), snapshot.groups.end(),
                    [&](const Group& group) { return group.id == *entity.groupId; });
                if (!found) {
                    loader.Fail(kBadShape, "無いグループを指しています。",
                        entity.displayName + " -> " + entity.groupId->ToString());
                }
            }
        }
        for (const ReferenceDimension& dimension : snapshot.referenceDimensions) {
            for (const EntityId& target : dimension.targets) {
                const bool found = std::any_of(snapshot.entities.begin(),
                    snapshot.entities.end(),
                    [&](const Entity& entity) { return entity.id == target; });
                if (!found) {
                    loader.Fail(kBadShape, "残した寸法が、無いものを指しています。",
                        dimension.label + " -> " + target.ToString());
                }
            }
        }
        if (snapshot.settings.activeGroupId.has_value()) {
            const bool found = std::any_of(snapshot.groups.begin(), snapshot.groups.end(),
                [&](const Group& group) {
                    return group.id == *snapshot.settings.activeGroupId;
                });
            if (!found) {
                loader.Fail(kBadShape, "無いグループが選択中になっています。",
                    snapshot.settings.activeGroupId->ToString());
            }
        }
        // Document 自身の検証(重複ID、参照切れ、循環)へ通す。
        snapshot.evaluationOrder = document::Document::TopologicalOrder(snapshot);
        std::vector<Diagnostic> structural = document::Document::Validate(snapshot);
        for (Diagnostic& diagnostic : structural) {
            if (diagnostic.IsError()) {
                loader.Fail(kBadShape, std::move(diagnostic.summaryJa),
                    std::move(diagnostic.detailsJa));
            }
        }
    }

    if (loader.Failed()) {
        std::vector<Diagnostic> diagnostics = loader.Take();
        const bool hasError = std::any_of(diagnostics.begin(), diagnostics.end(),
            [](const Diagnostic& diagnostic) { return diagnostic.IsError(); });
        if (hasError) {
            return Result<DocumentFile>::Failure(std::move(diagnostics));
        }
        return Result<DocumentFile>::Success(std::move(file), std::move(diagnostics));
    }
    return Result<DocumentFile>::Success(std::move(file));
}

Result<std::string> SaveDocument(const DocumentFile& file)
{
    std::vector<ZipEntry> entries;
    entries.push_back(ZipEntry{kDocumentEntry, WriteDocumentJson(file)});
    for (const ZipEntry& entry : file.sideEntries) {
        if (entry.path == kDocumentEntry) {
            return Result<std::string>::Failure(MakeError(kBadShape,
                "document.json は1つだけです。", entry.path));
        }
        entries.push_back(entry);
    }
    return WriteZip(entries);
}

Result<DocumentFile> LoadDocument(std::string_view archive)
{
    const auto read = ReadZip(archive);
    if (!read.HasValue()) {
        return Result<DocumentFile>::Failure(read.Diagnostics());
    }
    const std::vector<ZipEntry>& entries = read.Value();
    const ZipEntry* document = nullptr;
    for (const ZipEntry& entry : entries) {
        if (entry.path == kDocumentEntry) {
            document = &entry;
            break;
        }
    }
    if (document == nullptr) {
        return Result<DocumentFile>::Failure(MakeError(kNoDocument,
            "文書の本体(document.json)が入っていません。",
            "このファイルは .kcd2 ではないか、壊れています。"));
    }
    auto parsed = ReadDocumentJson(document->data);
    if (!parsed.HasValue()) {
        return parsed;
    }
    DocumentFile file = parsed.Value();
    for (const ZipEntry& entry : entries) {
        if (entry.path != kDocumentEntry) {
            file.sideEntries.push_back(entry);
        }
    }
    return Result<DocumentFile>::Success(std::move(file), parsed.Diagnostics());
}

} // namespace kachakacha::v2::io
