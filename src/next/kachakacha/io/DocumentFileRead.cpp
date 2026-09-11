//! kcd2 の読み込み(DocumentFile.cpp の読み込み側)。
//!
//! 書き出しと同じ名前の表(DocumentFileNames.h)を使う。
//! 分けたのは、ファイルの長さの門(1500行)を守るため。

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

    //! 無くてもよい真偽。古い文書に項目が無いことがあるので、既定値を返す。
    [[nodiscard]] bool BoolOr(const JsonValue& parent, const char* key, bool fallback)
    {
        const JsonValue* found = parent.Find(key);
        if (found == nullptr || found->Type() != JsonType::Bool) {
            return fallback;
        }
        return found->AsBool();
    }

    //! 無くてもよい数。古い文書に項目が無いことがあるので、既定値を返す。
    [[nodiscard]] double NumberOr(const JsonValue& parent, const char* key,
        double fallback)
    {
        const JsonValue* found = parent.Find(key);
        if (found == nullptr || found->Type() != JsonType::Number) {
            return fallback;
        }
        return found->AsNumber();
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

//! IDの並びを読む。新しい定義がどれもこの形なので、1か所にまとめる。
//! 無くてもよい数の並び。無ければ空。数でないものが混ざっていれば断る。
[[nodiscard]] std::vector<double> ReadNumberArray(Loader& loader, const JsonValue& parent,
    const char* key, const std::string& where)
{
    std::vector<double> values;
    const JsonValue* found = parent.Find(key);
    if (found == nullptr) {
        return values;
    }
    const JsonArray* array = loader.ArrayAt(parent, key, where);
    if (array == nullptr) {
        return values;
    }
    for (std::size_t index = 0; index < array->size(); ++index) {
        const JsonValue& item = (*array)[index];
        if (item.Type() != JsonType::Number) {
            loader.Fail(kBadValue, "数であるべき項目が数ではありません。",
                where + "." + key + "[" + std::to_string(index) + "]");
            continue;
        }
        values.push_back(item.AsNumber());
    }
    return values;
}

[[nodiscard]] std::vector<EntityId> ReadIdArray(Loader& loader, const JsonValue& parent,
    const char* key, const std::string& where)
{
    std::vector<EntityId> ids;
    const JsonArray* array = loader.ArrayAt(parent, key, where);
    if (array == nullptr) {
        return ids;
    }
    for (std::size_t index = 0; index < array->size(); ++index) {
        const JsonValue& item = (*array)[index];
        const std::string place = where + "." + key + "[" + std::to_string(index) + "]";
        if (item.Type() != JsonType::String) {
            loader.Fail(kBadValue, "IDは文字列でなければなりません。", place);
            continue;
        }
        ids.push_back(loader.ParseId<EntityId>(item.AsString(), place));
    }
    return ids;
}

//! 役割1つぶんの鎖を読む。
[[nodiscard]] domain::WireChainRef ReadChainRef(Loader& loader, const JsonValue& value,
    const std::string& where)
{
    domain::WireChainRef chain;
    const JsonArray* segments = loader.ArrayAt(value, "segments", where);
    if (segments != nullptr) {
        for (std::size_t index = 0; index < segments->size(); ++index) {
            chain.segments.push_back(ReadSegmentRef(loader, (*segments)[index],
                where + ".segments[" + std::to_string(index) + "]"));
        }
    }
    const JsonArray* reversed = loader.ArrayAt(value, "reversed", where);
    if (reversed != nullptr) {
        for (const JsonValue& item : *reversed) {
            chain.reversed.push_back(item.Type() == JsonType::Bool && item.AsBool());
        }
    }
    return chain;
}

void ReadDefinition(Loader& loader, Feature& feature, const JsonValue& definition,
    const std::string& where)
{
    // 中身の無い定義は、そのまま空にしておく。
    // 定義を足す前に書かれた文書には、その項目がまだ無い。
    // 「必要な項目がありません」と言って読めなくすると、古い文書が開けなくなる。
    // 新しく書いたものは必ず項目を持つので、そちらは下の検査が効く。
    if (definition.Type() != JsonType::Object || definition.AsObject().empty()) {
        return;
    }
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
            // 古い文書には無い。無ければ対称・自動・全部の角(前と同じ意味)。
            made.secondScalarMm = loader.NumberOr(*parameters, "scalar2", 0.0);
            made.firstKeepSide = static_cast<int>(loader.NumberOr(*parameters, "keepFirst", 0.0));
            made.secondKeepSide = static_cast<int>(
                loader.NumberOr(*parameters, "keepSecond", 0.0));
            made.cornerIndex = static_cast<int>(loader.NumberOr(*parameters, "corner", -1.0));
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
    case FeatureType::CreateWorkPlane: {
        domain::CreateWorkPlaneDefinition made;
        made.method = static_cast<int>(loader.NumberOr(definition, "method", 0.0));
        made.inputs = ReadIdArray(loader, definition, "inputs", where);
        made.origin = loader.ReadVector(definition, "origin", where);
        made.normal = loader.ReadVector(definition, "normal", where);
        made.uDirection = loader.ReadVector(definition, "uDirection", where);
        made.offset = loader.ReadExpression(definition, "offset", where);
        made.isOriginPlane = loader.BoolOr(definition, "isOriginPlane", false);
        feature.definition = std::move(made);
        break;
    }
    case FeatureType::ProjectWire: {
        domain::ProjectWireDefinition made;
        made.inputs = ReadIdArray(loader, definition, "inputs", where);
        made.targetPlaneId = loader.ParseId<EntityId>(
            loader.String(definition, "targetPlaneId", where), where + ".targetPlaneId");
        made.direction = loader.ReadVector(definition, "direction", where);
        feature.definition = std::move(made);
        break;
    }
    case FeatureType::CreateGuideSurface: {
        domain::CreateGuideSurfaceDefinition made;
        made.method = static_cast<int>(loader.NumberOr(definition, "method", 0.0));
        const JsonArray* chains = loader.ArrayAt(definition, "chains", where);
        if (chains != nullptr) {
            for (std::size_t index = 0; index < chains->size(); ++index) {
                made.chains.push_back(ReadChainRef(loader, (*chains)[index],
                    where + ".chains[" + std::to_string(index) + "]"));
            }
        }
        const JsonArray* roles = loader.ArrayAt(definition, "roles", where);
        if (roles != nullptr) {
            for (const JsonValue& item : *roles) {
                made.roles.push_back(item.Type() == JsonType::Number
                        ? static_cast<int>(item.AsNumber())
                        : 0);
            }
        }
        made.offsetDistanceMm = loader.NumberOr(definition, "offsetDistanceMm", 0.0);
        if (definition.Find("revolveAxisPoint") != nullptr) {
            made.revolveAxisPoint = loader.ReadVector(definition, "revolveAxisPoint", where);
            made.revolveAxisDirection =
                loader.ReadVector(definition, "revolveAxisDirection", where);
        }
        made.revolveAngleRad = loader.NumberOr(definition, "revolveAngleRad", 0.0);
        feature.definition = std::move(made);
        break;
    }
    case FeatureType::Extrude: {
        domain::ExtrudeDefinition made;
        made.profiles = ReadIdArray(loader, definition, "profiles", where);
        made.direction = loader.ReadVector(definition, "direction", where);
        made.distance = loader.ReadExpression(definition, "distance", where);
        made.extentMode = static_cast<int>(loader.NumberOr(definition, "extentMode", 0.0));
        made.booleanMode = static_cast<int>(loader.NumberOr(definition, "booleanMode", 0.0));
        made.targets = ReadIdArray(loader, definition, "targets", where);
        feature.definition = std::move(made);
        break;
    }
    case FeatureType::CreatePartFromWireCage: {
        domain::CreatePartFromWireCageDefinition made;
        made.wires = ReadIdArray(loader, definition, "wires", where);
        made.thickness = loader.ReadExpression(definition, "thickness", where);
        made.placement = static_cast<int>(loader.NumberOr(definition, "placement", 1.0));
        feature.definition = std::move(made);
        break;
    }
    case FeatureType::Boolean: {
        domain::BooleanDefinition made;
        made.mode = static_cast<int>(loader.NumberOr(definition, "mode", 0.0));
        made.targets = ReadIdArray(loader, definition, "targets", where);
        made.tools = ReadIdArray(loader, definition, "tools", where);
        feature.definition = std::move(made);
        break;
    }
    case FeatureType::CreateFabricationModel: {
        domain::CreateFabricationModelDefinition made;
        made.parts = ReadIdArray(loader, definition, "parts", where);
        made.materialThickness = loader.ReadExpression(definition, "materialThickness",
            where);
        made.targetMaxDeviation = loader.ReadExpression(definition, "targetMaxDeviation",
            where);
        made.fidelity = static_cast<int>(loader.NumberOr(definition, "fidelity", 6.0));
        made.method = static_cast<int>(loader.NumberOr(definition, "method", 0.0));
        made.splitAxis = static_cast<int>(loader.NumberOr(definition, "splitAxis", 1.0));
        made.automaticBoundaries = loader.BoolOr(definition, "automaticBoundaries", true);
        made.maximumPartCount =
            static_cast<int>(loader.NumberOr(definition, "maximumPartCount", 12.0));
        made.minimumPartWidthMm = loader.NumberOr(definition, "minimumPartWidthMm", 4.0);
        made.manualBoundaries = ReadNumberArray(loader, definition, "manualBoundaries", where);
        made.openingWires = ReadIdArray(loader, definition, "openingWires", where);
        made.foldWires = ReadIdArray(loader, definition, "foldWires", where);
        if (definition.Find("reliefCutWires") != nullptr) {
            made.reliefCutWires = ReadIdArray(loader, definition, "reliefCutWires", where);
        }
        made.connectionWires = ReadIdArray(loader, definition, "connectionWires", where);
        made.masterPercent = loader.NumberOr(definition, "masterPercent", 100.0);
        made.rangeUMin = loader.NumberOr(definition, "rangeUMin", 0.0);
        made.rangeUMax = loader.NumberOr(definition, "rangeUMax", 1.0);
        made.rangeVMin = loader.NumberOr(definition, "rangeVMin", 0.0);
        made.rangeVMax = loader.NumberOr(definition, "rangeVMax", 1.0);
        made.creaseProgress = ReadNumberArray(loader, definition, "creaseProgress", where);
        made.bandProgress = ReadNumberArray(loader, definition, "bandProgress", where);
        feature.definition = std::move(made);
        break;
    }
    case FeatureType::ThickenSurface: {
        domain::ThickenSurfaceDefinition made;
        if (const JsonValue* one = definition.Find("surface");
            one != nullptr && one->Type() == JsonType::String) {
            const auto parsed = EntityId::Parse(one->AsString());
            if (parsed.has_value()) {
                made.surface = *parsed;
            }
        }
        made.thickness = loader.ReadExpression(definition, "thickness", where);
        made.placement = static_cast<int>(loader.NumberOr(definition, "placement", 1.0));
        if (const JsonValue* plane = definition.Find("targetPlane");
            plane != nullptr && plane->Type() == JsonType::String) {
            made.targetPlane = EntityId::Parse(plane->AsString());
        }
        feature.definition = std::move(made);
        break;
    }
    case FeatureType::CreatePattern: {
        domain::CreatePatternDefinition made;
        made.fabricationModels = ReadIdArray(loader, definition, "fabricationModels", where);
        made.pageWidth = loader.ReadExpression(definition, "pageWidth", where);
        made.pageHeight = loader.ReadExpression(definition, "pageHeight", where);
        made.marginMm = loader.ReadExpression(definition, "margin", where);
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

} // namespace kachakacha::v2::io
