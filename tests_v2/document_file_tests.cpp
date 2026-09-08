// `.kcd2` の保存と読み込み。作った文書がそのまま戻ること、壊れた文書を黙って通さないこと。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/io/DocumentFile.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::DocumentId;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::FeatureId;
using kachakacha::v2::base::GroupId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::base::SegmentId;
using kachakacha::v2::document::DocumentSnapshot;
using kachakacha::v2::document::Group;
using kachakacha::v2::domain::CreatePointDefinition;
using kachakacha::v2::domain::CreateWireDefinition;
using kachakacha::v2::domain::EditPolicy;
using kachakacha::v2::domain::Entity;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::Feature;
using kachakacha::v2::domain::FeatureOutput;
using kachakacha::v2::domain::FeatureType;
using kachakacha::v2::domain::FreezeDerivedDefinition;
using kachakacha::v2::domain::ManufacturingProperties;
using kachakacha::v2::domain::PartRole;
using kachakacha::v2::domain::SegmentRef;
using kachakacha::v2::domain::TransformWireDefinition;
using kachakacha::v2::domain::Visibility;
using kachakacha::v2::domain::WireTransformMethod;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::EvaluatedValue;
using kachakacha::v2::geometry::QuantityKind;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::io::DocumentFile;
using kachakacha::v2::io::JsonValue;
using kachakacha::v2::io::LoadDocument;
using kachakacha::v2::io::ParseJson;
using kachakacha::v2::io::ReadDocumentJson;
using kachakacha::v2::io::SaveDocument;
using kachakacha::v2::io::WriteDocumentJson;
using kachakacha::v2::io::WriteJson;
using kachakacha::v2::io::ZipEntry;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

void RequireCount(std::size_t actual, std::size_t expected, const std::string& why)
{
    RequireEqual(std::to_string(actual), std::to_string(expected), why);
}

//! 試験用の文書を組み立てる道具。IDは決定的に振る(同じ入力から同じファイルが出るため)。
struct Maker {
    DeterministicIdGenerator ids{7};

    [[nodiscard]] FeatureId NextFeature() { return ids.NextTyped<IdKind::Feature>(); }
    [[nodiscard]] EntityId NextEntity() { return ids.NextTyped<IdKind::Entity>(); }
    [[nodiscard]] GroupId NextGroup() { return ids.NextTyped<IdKind::Group>(); }
    [[nodiscard]] SegmentId NextSegment() { return ids.NextTyped<IdKind::Segment>(); }

    //! 点を1つ作るFeatureとEntity。
    void AddPoint(DocumentSnapshot& snapshot, const std::string& name, Vector3 position)
    {
        Feature feature;
        feature.id = NextFeature();
        feature.type = FeatureType::CreatePoint;
        feature.displayName = name;
        CreatePointDefinition definition;
        definition.positionMm = position;
        definition.xExpression = EvaluatedValue{"10*2", position.x, QuantityKind::Length};
        definition.yExpression = EvaluatedValue{"0", position.y, QuantityKind::Length};
        definition.zExpression = EvaluatedValue{"0", position.z, QuantityKind::Length};
        feature.definition = definition;

        Entity entity;
        entity.id = NextEntity();
        entity.kind = EntityKind::Point;
        entity.displayName = name;
        entity.createdBy = feature.id;
        feature.outputs.push_back(FeatureOutput{"point", entity.id, EntityKind::Point});
        snapshot.features.push_back(std::move(feature));
        snapshot.entities.push_back(std::move(entity));
    }

    //! 5種類の線を全部持つワイヤー。種類が保たれるかを見るため。
    void AddWire(DocumentSnapshot& snapshot, const std::string& name)
    {
        Feature feature;
        feature.id = NextFeature();
        feature.type = FeatureType::CreateWire;
        feature.displayName = name;
        CreateWireDefinition definition;
        definition.segments.push_back(
            CurveSegment::MakeLine({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}).Value());
        definition.segments.push_back(CurveSegment::MakeCircularArc({10.0, 5.0, 0.0},
            {0.0, 0.0, 1.0}, {0.0, -1.0, 0.0}, 5.0, 0.0, 1.5707963267948966).Value());
        definition.segments.push_back(CurveSegment::MakeCircle({0.0, 20.0, 0.0},
            {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}, 3.25).Value());
        definition.segments.push_back(CurveSegment::MakeCubicBezier(
            {{0.0, 0.0, 0.0}, {1.0, 2.0, 0.0}, {3.0, 2.0, 0.0}, {4.0, 0.0, 0.0}}).Value());
        definition.segments.push_back(CurveSegment::MakeCubicBSpline(
            {{0.0, 0.0, 0.0}, {1.0, 3.0, 1.0}, {4.0, 3.0, -1.0}, {5.0, 0.0, 0.0},
                {7.0, -2.0, 0.5}}).Value());
        for (std::size_t index = 0; index < definition.segments.size(); ++index) {
            definition.segmentIds.push_back(NextSegment());
        }
        definition.construction = true;
        feature.definition = definition;

        Entity entity;
        entity.id = NextEntity();
        entity.kind = EntityKind::Wire;
        entity.displayName = name;
        entity.createdBy = feature.id;
        entity.construction = true;
        feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});
        snapshot.features.push_back(std::move(feature));
        snapshot.entities.push_back(std::move(entity));
    }
};

//! 一通りの中身が入った文書。ほとんどの試験がこれを土台にする。
[[nodiscard]] DocumentFile MakeSampleDocument()
{
    Maker maker;
    DocumentFile file;
    DocumentSnapshot& snapshot = file.snapshot;
    snapshot.id = DocumentId::Parse("00000000-0000-4000-8000-000000000001").value();
    snapshot.revision = 42;
    snapshot.settings.tolerance.modelLinearMm = 1.0e-6;
    snapshot.settings.tolerance.interactiveJoinMm = 0.02;
    file.metadata.title = "ER2前頭部";
    file.metadata.author = "ざわ";
    file.metadata.description = "曲面の試作。日本語のまま保存されること。";

    Group group;
    group.id = maker.NextGroup();
    group.displayName = "前面";
    snapshot.groups.push_back(group);
    snapshot.settings.activeGroupId = group.id;

    maker.AddPoint(snapshot, "原点", {0.0, 0.0, 0.0});
    maker.AddPoint(snapshot, "端点", {20.0, -3.5, 0.125});
    maker.AddWire(snapshot, "窓上輪郭");
    snapshot.entities[0].groupId = group.id;
    snapshot.entities[1].visibility = Visibility::Reference;
    snapshot.entities[2].editPolicy = EditPolicy::Derived;

    // 部品を1つ。属性が付く唯一の種類。
    {
        Feature feature;
        feature.id = maker.NextFeature();
        feature.type = FeatureType::Extrude;
        feature.displayName = "押し出し";
        feature.inputEntityIds.push_back(snapshot.entities[2].id);
        Entity entity;
        entity.id = maker.NextEntity();
        entity.kind = EntityKind::Part;
        entity.displayName = "側板";
        entity.createdBy = feature.id;
        entity.partRole = PartRole::FabricationPart;
        ManufacturingProperties properties;
        properties.materialName = "ABS";
        properties.colorName = "白";
        properties.processName = "レーザー切断";
        properties.note = "0.3mm 板";
        properties.nominalThicknessMm = 0.3;
        properties.scaleDenominator = 87.0;
        properties.layerCount = 2;
        entity.manufacturing = properties;
        feature.outputs.push_back(FeatureOutput{"part", entity.id, EntityKind::Part});
        snapshot.features.push_back(std::move(feature));
        snapshot.entities.push_back(std::move(entity));
    }

    // ワイヤー編集(移動)。SegmentRef が往復するかを見る。
    {
        Feature feature;
        feature.id = maker.NextFeature();
        feature.type = FeatureType::TransformWire;
        feature.displayName = "移動";
        TransformWireDefinition definition;
        definition.method = WireTransformMethod::Rotate;
        SegmentRef reference;
        reference.entityId = snapshot.entities[2].id;
        reference.segmentId = std::get<CreateWireDefinition>(
            snapshot.features[2].definition).segmentIds[1];
        reference.startParameter = 0.25;
        reference.endParameter = 0.75;
        definition.inputs.push_back(reference);
        definition.vectorArgument = {0.0, 0.0, 1.0};
        definition.pointArgument = {1.5, -2.5, 0.0};
        definition.scalarArgument =
            EvaluatedValue{"deg(30)", 0.5235987755982988, QuantityKind::Angle};
        feature.definition = definition;
        feature.inputEntityIds.push_back(snapshot.entities[2].id);
        Entity entity;
        entity.id = maker.NextEntity();
        entity.kind = EntityKind::Wire;
        entity.displayName = "窓上輪郭(回転)";
        entity.createdBy = feature.id;
        entity.editPolicy = EditPolicy::Derived;
        feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});
        snapshot.features.push_back(std::move(feature));
        snapshot.entities.push_back(std::move(entity));
    }

    // 固定。EntityIdの配列を持つ定義。
    {
        Feature feature;
        feature.id = maker.NextFeature();
        feature.type = FeatureType::FreezeDerived;
        feature.displayName = "固定";
        FreezeDerivedDefinition definition;
        definition.sources.push_back(snapshot.entities[4].id);
        feature.definition = definition;
        feature.inputEntityIds.push_back(snapshot.entities[4].id);
        Entity entity;
        entity.id = maker.NextEntity();
        entity.kind = EntityKind::Wire;
        entity.displayName = "固定した輪郭";
        entity.createdBy = feature.id;
        entity.editPolicy = EditPolicy::Frozen;
        feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});
        snapshot.features.push_back(std::move(feature));
        snapshot.entities.push_back(std::move(entity));
    }

    kachakacha::v2::io::JsonObject ui;
    ui["activeMode"] = JsonValue::String("drawing");
    ui["theme"] = JsonValue::String("windows95");
    file.uiState = JsonValue::Object(std::move(ui));
    return file;
}

//! 文書JSONを読み、値を1つ差し替えて書き直す。
//! 文字列置換だと桁揃えや空白に振り回されるので、構造として直す。
[[nodiscard]] std::string Mutate(const std::string& text,
    const std::function<void(kachakacha::v2::io::JsonObject&)>& change)
{
    const auto parsed = ParseJson(text);
    Require(parsed.HasValue(), "土台のJSONが読めること");
    JsonValue root = parsed.Value();
    change(root.MutableObject());
    return WriteJson(root);
}

//! features[index].definition を取り出す。
[[nodiscard]] kachakacha::v2::io::JsonObject& DefinitionOf(
    kachakacha::v2::io::JsonObject& root, std::size_t index)
{
    return root["features"].MutableArray()[index].MutableObject()["definition"]
        .MutableObject();
}

//! JSON文字列の一部を差し替える。壊れた文書を作るために使う。
[[nodiscard]] std::string Replace(const std::string& text, const std::string& from,
    const std::string& to)
{
    const std::size_t position = text.find(from);
    Require(position != std::string::npos, "差し替え対象が見つかること: " + from);
    std::string made = text;
    made.replace(position, from.size(), to);
    return made;
}

void RequireRejects(const std::string& text, const std::string& expectedCode,
    const std::string& why)
{
    const auto read = ReadDocumentJson(text);
    Require(!read.HasValue(), "断ること: " + why);
    Require(!read.Diagnostics().empty(), "診断が付くこと: " + why);
    const bool found = std::any_of(read.Diagnostics().begin(), read.Diagnostics().end(),
        [&](const auto& diagnostic) { return diagnostic.code == expectedCode; });
    Require(found, "診断コード " + expectedCode + " が出ること: " + why + " (実際: "
            + read.Diagnostics().front().code + " / " + read.Diagnostics().front().detailsJa + ")");
}

} // namespace

// ---------------------------------------------------------------- 往復

KACHA_V2_TEST(documentFile, 一通りの中身が往復する)
{
    const DocumentFile original = MakeSampleDocument();
    const std::string text = WriteDocumentJson(original);
    const auto read = ReadDocumentJson(text);
    Require(read.HasValue(), "読めること");
    const DocumentFile& loaded = read.Value();

    RequireEqual(loaded.snapshot.id.ToString(), original.snapshot.id.ToString(), "文書ID");
    RequireCount(static_cast<std::size_t>(loaded.snapshot.revision),
        static_cast<std::size_t>(original.snapshot.revision), "版数");
    RequireEqual(loaded.metadata.title, original.metadata.title, "題名");
    RequireEqual(loaded.metadata.author, original.metadata.author, "作った人");
    RequireEqual(loaded.metadata.description, original.metadata.description, "説明");
    RequireCount(loaded.snapshot.groups.size(), original.snapshot.groups.size(), "グループ数");
    RequireCount(loaded.snapshot.entities.size(), original.snapshot.entities.size(),
        "オブジェクト数");
    RequireCount(loaded.snapshot.features.size(), original.snapshot.features.size(), "指示数");
    RequireNear(loaded.snapshot.settings.tolerance.interactiveJoinMm, 0.02, 1e-15,
        "つながりの許容差");
}

KACHA_V2_TEST(documentFile, 2回書いても同じ文字列になる)
{
    const DocumentFile original = MakeSampleDocument();
    const std::string first = WriteDocumentJson(original);
    const auto read = ReadDocumentJson(first);
    Require(read.HasValue(), "読めること");
    const std::string second = WriteDocumentJson(read.Value());
    Require(first == second, "書いて読んで書いたものが一致すること");

    const auto again = ReadDocumentJson(second);
    Require(again.HasValue(), "もう一度読めること");
    Require(WriteDocumentJson(again.Value()) == second, "3回目も一致すること");
}

KACHA_V2_TEST(documentFile, 線の種類が保たれる)
{
    const DocumentFile original = MakeSampleDocument();
    const auto read = ReadDocumentJson(WriteDocumentJson(original));
    Require(read.HasValue(), "読めること");
    const auto& before = std::get<CreateWireDefinition>(original.snapshot.features[2].definition);
    const auto& after = std::get<CreateWireDefinition>(
        read.Value().snapshot.features[2].definition);
    RequireCount(after.segments.size(), before.segments.size(), "線の本数");
    const CurveKind expected[]{CurveKind::Line, CurveKind::CircularArc, CurveKind::Circle,
        CurveKind::CubicBezier, CurveKind::CubicBSpline};
    for (std::size_t index = 0; index < after.segments.size(); ++index) {
        Require(after.segments[index].Kind() == expected[index],
            "種類が変わっていないこと " + std::to_string(index));
        RequireEqual(after.segmentIds[index].ToString(), before.segmentIds[index].ToString(),
            "線のID " + std::to_string(index));
    }
    Require(after.construction, "補助線であること");
}

KACHA_V2_TEST(documentFile, 円弧の値がそのまま戻る)
{
    const DocumentFile original = MakeSampleDocument();
    const auto read = ReadDocumentJson(WriteDocumentJson(original));
    Require(read.HasValue(), "読めること");
    const auto& after = std::get<CreateWireDefinition>(
        read.Value().snapshot.features[2].definition);
    const CurveSegment& arc = after.segments[1];
    RequireNear(arc.Radius(), 5.0, 1e-15, "半径");
    RequireNear(arc.StartAngleRad(), 0.0, 1e-15, "開始角");
    RequireNear(arc.SweepAngleRad(), 1.5707963267948966, 1e-15, "掃引角");
    RequireNear(arc.Center().x, 10.0, 1e-15, "中心x");
    RequireNear(arc.Center().y, 5.0, 1e-15, "中心y");
    RequireNear(arc.Normal().z, 1.0, 1e-15, "法線z");
    const CurveSegment& circle = after.segments[2];
    RequireNear(circle.Radius(), 3.25, 1e-15, "円の半径");
}

KACHA_V2_TEST(documentFile, 制御点がそのまま戻る)
{
    const DocumentFile original = MakeSampleDocument();
    const auto read = ReadDocumentJson(WriteDocumentJson(original));
    Require(read.HasValue(), "読めること");
    const auto& before = std::get<CreateWireDefinition>(original.snapshot.features[2].definition);
    const auto& after = std::get<CreateWireDefinition>(
        read.Value().snapshot.features[2].definition);
    for (std::size_t index : {std::size_t{3}, std::size_t{4}}) {
        const auto& want = before.segments[index].ControlPoints();
        const auto& got = after.segments[index].ControlPoints();
        RequireCount(got.size(), want.size(), "制御点の数 " + std::to_string(index));
        for (std::size_t at = 0; at < want.size(); ++at) {
            RequireNear(got[at].x, want[at].x, 1e-15, "x");
            RequireNear(got[at].y, want[at].y, 1e-15, "y");
            RequireNear(got[at].z, want[at].z, 1e-15, "z");
        }
    }
}

KACHA_V2_TEST(documentFile, 式が文字列のまま戻る)
{
    const DocumentFile original = MakeSampleDocument();
    const auto read = ReadDocumentJson(WriteDocumentJson(original));
    Require(read.HasValue(), "読めること");
    const auto& point = std::get<CreatePointDefinition>(
        read.Value().snapshot.features[1].definition);
    RequireEqual(point.xExpression.expression, std::string("10*2"), "式そのもの");
    Require(point.xExpression.kind == QuantityKind::Length, "単位の種類");
    const auto& transform = std::get<TransformWireDefinition>(
        read.Value().snapshot.features[4].definition);
    RequireEqual(transform.scalarArgument.expression, std::string("deg(30)"), "角度の式");
    Require(transform.scalarArgument.kind == QuantityKind::Angle, "角度であること");
    RequireNear(transform.scalarArgument.value, 0.5235987755982988, 1e-15, "評価値");
}

KACHA_V2_TEST(documentFile, ワイヤー編集の参照が往復する)
{
    const DocumentFile original = MakeSampleDocument();
    const auto read = ReadDocumentJson(WriteDocumentJson(original));
    Require(read.HasValue(), "読めること");
    const auto& before = std::get<TransformWireDefinition>(
        original.snapshot.features[4].definition);
    const auto& after = std::get<TransformWireDefinition>(
        read.Value().snapshot.features[4].definition);
    Require(after.method == WireTransformMethod::Rotate, "方法");
    RequireCount(after.inputs.size(), before.inputs.size(), "参照の数");
    RequireEqual(after.inputs[0].segmentId.ToString(), before.inputs[0].segmentId.ToString(),
        "線のID");
    RequireNear(after.inputs[0].startParameter, 0.25, 1e-15, "始まり");
    RequireNear(after.inputs[0].endParameter, 0.75, 1e-15, "終わり");
    RequireNear(after.vectorArgument.z, 1.0, 1e-15, "軸");
    RequireNear(after.pointArgument.x, 1.5, 1e-15, "軸上の点");
}

KACHA_V2_TEST(documentFile, 部品の属性が往復する)
{
    const DocumentFile original = MakeSampleDocument();
    const auto read = ReadDocumentJson(WriteDocumentJson(original));
    Require(read.HasValue(), "読めること");
    const Entity& part = read.Value().snapshot.entities[3];
    Require(part.kind == EntityKind::Part, "部品であること");
    Require(part.partRole == PartRole::FabricationPart, "用途");
    Require(part.manufacturing.has_value(), "製作の属性があること");
    RequireEqual(part.manufacturing->materialName, std::string("ABS"), "材料");
    RequireEqual(part.manufacturing->colorName, std::string("白"), "色");
    RequireEqual(part.manufacturing->processName, std::string("レーザー切断"), "工程");
    RequireEqual(part.manufacturing->note, std::string("0.3mm 板"), "備考");
    Require(part.manufacturing->nominalThicknessMm.has_value(), "板厚があること");
    RequireNear(*part.manufacturing->nominalThicknessMm, 0.3, 1e-15, "板厚");
    RequireNear(*part.manufacturing->scaleDenominator, 87.0, 1e-15, "縮尺");
    RequireCount(static_cast<std::size_t>(part.manufacturing->layerCount), 2, "重ね枚数");
}

KACHA_V2_TEST(documentFile, 表示の状態が往復する)
{
    const DocumentFile original = MakeSampleDocument();
    const auto read = ReadDocumentJson(WriteDocumentJson(original));
    Require(read.HasValue(), "読めること");
    const auto& entities = read.Value().snapshot.entities;
    Require(entities[0].groupId.has_value(), "グループに入っていること");
    Require(entities[1].visibility == Visibility::Reference, "参照表示");
    Require(entities[2].editPolicy == EditPolicy::Derived, "派生");
    Require(entities[5].editPolicy == EditPolicy::Frozen, "固定");
    Require(read.Value().snapshot.settings.activeGroupId.has_value(), "選択中のグループ");
}

KACHA_V2_TEST(documentFile, 画面の状態を落とさない)
{
    const DocumentFile original = MakeSampleDocument();
    const auto read = ReadDocumentJson(WriteDocumentJson(original));
    Require(read.HasValue(), "読めること");
    const JsonValue& ui = read.Value().uiState;
    Require(ui.IsObject(), "組であること");
    const JsonValue* theme = ui.Find("theme");
    Require(theme != nullptr, "themeがあること");
    RequireEqual(theme->AsString(), std::string("windows95"), "theme");
}

KACHA_V2_TEST(documentFile, この版が知らない画面設定も落とさない)
{
    // 新しい版が書いた画面設定を、古い版が開いて保存し直しても消さない。
    DocumentFile original = MakeSampleDocument();
    kachakacha::v2::io::JsonObject ui = original.uiState.AsObject();
    ui["未来の設定"] = JsonValue::String("大事な値");
    kachakacha::v2::io::JsonObject nested;
    nested["深いところ"] = JsonValue::Number(3.5);
    ui["extensions"] = JsonValue::Object(std::move(nested));
    original.uiState = JsonValue::Object(std::move(ui));

    const auto read = ReadDocumentJson(WriteDocumentJson(original));
    Require(read.HasValue(), "読めること");
    const JsonValue* kept = read.Value().uiState.Find("未来の設定");
    Require(kept != nullptr, "知らない設定が残ること");
    RequireEqual(kept->AsString(), std::string("大事な値"), "値も残ること");
    const JsonValue* extensions = read.Value().uiState.Find("extensions");
    Require(extensions != nullptr && extensions->IsObject(), "extensionsが残ること");
}

KACHA_V2_TEST(documentFile, 空の文書も往復する)
{
    DocumentFile file;
    file.snapshot.id = DocumentId::Parse("00000000-0000-4000-8000-0000000000ff").value();
    const std::string text = WriteDocumentJson(file);
    const auto read = ReadDocumentJson(text);
    Require(read.HasValue(), "読めること");
    Require(read.Value().snapshot.entities.empty(), "オブジェクトが無いこと");
    Require(read.Value().snapshot.features.empty(), "指示が無いこと");
    Require(WriteDocumentJson(read.Value()) == text, "同じ文字列になること");
}

KACHA_V2_TEST(documentFile, 大きな文書も往復する)
{
    Maker maker;
    DocumentFile file;
    file.snapshot.id = DocumentId::Parse("00000000-0000-4000-8000-000000000002").value();
    for (int index = 0; index < 400; ++index) {
        maker.AddPoint(file.snapshot, "点" + std::to_string(index),
            {static_cast<double>(index) * 0.125, -static_cast<double>(index), 0.0});
    }
    for (int index = 0; index < 60; ++index) {
        maker.AddWire(file.snapshot, "輪郭" + std::to_string(index));
    }
    const std::string text = WriteDocumentJson(file);
    const auto read = ReadDocumentJson(text);
    Require(read.HasValue(), "読めること");
    RequireCount(read.Value().snapshot.entities.size(), 460, "オブジェクト数");
    Require(WriteDocumentJson(read.Value()) == text, "同じ文字列になること");
}

KACHA_V2_TEST(documentFile, 数の細かい値が壊れない)
{
    Maker maker;
    DocumentFile file;
    file.snapshot.id = DocumentId::Parse("00000000-0000-4000-8000-000000000003").value();
    const double awkward[]{0.1, 1.0 / 3.0, 1e-9, 1e9, -0.000123456789012345,
        3.141592653589793, 2.718281828459045, 1.7976931348623157e300, 5e-300};
    for (const double value : awkward) {
        maker.AddPoint(file.snapshot, "点", {value, -value, value * 0.5});
    }
    const auto read = ReadDocumentJson(WriteDocumentJson(file));
    Require(read.HasValue(), "読めること");
    for (std::size_t index = 0; index < std::size(awkward); ++index) {
        const auto& definition = std::get<CreatePointDefinition>(
            read.Value().snapshot.features[index].definition);
        Require(definition.positionMm.x == awkward[index],
            "ぴったり同じ値で戻ること " + std::to_string(index));
        Require(definition.positionMm.y == -awkward[index], "符号違いも同じ値で戻ること");
    }
}

// ---------------------------------------------------------------- 書庫として

KACHA_V2_TEST(documentFile, 書庫として保存して開ける)
{
    const DocumentFile original = MakeSampleDocument();
    const auto saved = SaveDocument(original);
    Require(saved.HasValue(), "保存できること");
    const auto loaded = LoadDocument(saved.Value());
    Require(loaded.HasValue(), "開けること");
    RequireEqual(loaded.Value().metadata.title, original.metadata.title, "題名");
    RequireCount(loaded.Value().snapshot.entities.size(),
        original.snapshot.entities.size(), "オブジェクト数");
}

KACHA_V2_TEST(documentFile, 同じ文書からは同じファイルが出る)
{
    const DocumentFile original = MakeSampleDocument();
    const auto first = SaveDocument(original);
    const auto second = SaveDocument(original);
    Require(first.HasValue() && second.HasValue(), "保存できること");
    Require(first.Value() == second.Value(), "バイト単位で一致すること");

    const auto loaded = LoadDocument(first.Value());
    Require(loaded.HasValue(), "開けること");
    const auto third = SaveDocument(loaded.Value());
    Require(third.HasValue(), "開いたものを保存できること");
    Require(third.Value() == first.Value(), "開いて保存しても同じであること");
}

KACHA_V2_TEST(documentFile, 知らないentryを落とさない)
{
    DocumentFile original = MakeSampleDocument();
    original.sideEntries.push_back(ZipEntry{"meta/thumbnail.png",
        std::string("\x89PNG\r\n\x1a\n", 8)});
    original.sideEntries.push_back(ZipEntry{"未来/新機能.json", "{\"a\":1}"});
    const auto saved = SaveDocument(original);
    Require(saved.HasValue(), "保存できること");
    const auto loaded = LoadDocument(saved.Value());
    Require(loaded.HasValue(), "開けること");
    RequireCount(loaded.Value().sideEntries.size(), 2, "追加entryの数");
    const auto again = SaveDocument(loaded.Value());
    Require(again.HasValue(), "保存し直せること");
    Require(again.Value() == saved.Value(), "追加entryも含めて同じであること");
}

KACHA_V2_TEST(documentFile, 本体が無い書庫を断る)
{
    const auto archive = kachakacha::v2::io::WriteZip({ZipEntry{"meta/thumbnail.png", "x"}});
    Require(archive.HasValue(), "書庫を作れること");
    const auto loaded = LoadDocument(archive.Value());
    Require(!loaded.HasValue(), "断ること");
    RequireEqual(loaded.Diagnostics().front().code, std::string("KCD2-D005"), "診断コード");
}

KACHA_V2_TEST(documentFile, ZIPでないものを開こうとしても落ちない)
{
    for (const std::string& text : {std::string(), std::string("hello"),
             std::string("{\"format\":\"kachakachaCAD\"}"), std::string(500, '\0')}) {
        const auto loaded = LoadDocument(text);
        Require(!loaded.HasValue(), "断ること");
        Require(!loaded.Diagnostics().empty(), "診断が付くこと");
    }
}

KACHA_V2_TEST(documentFile, 書庫が1バイト壊れていたら開かない)
{
    const auto saved = SaveDocument(MakeSampleDocument());
    Require(saved.HasValue(), "保存できること");
    const std::string archive = saved.Value();
    // 本体の中身のどこかを壊す。検査値で必ず気付くこと。
    const std::size_t position = archive.find("kachakachaCAD");
    Require(position != std::string::npos, "本体が見つかること");
    for (std::size_t offset : {std::size_t{0}, std::size_t{5}, std::size_t{200},
             std::size_t{800}}) {
        if (position + offset >= archive.size()) {
            continue;
        }
        std::string broken = archive;
        broken[position + offset] = static_cast<char>(broken[position + offset] ^ 0x40);
        const auto loaded = LoadDocument(broken);
        Require(!loaded.HasValue(), "断ること " + std::to_string(offset));
    }
}

// ---------------------------------------------------------------- 壊れた文書

KACHA_V2_TEST(documentFile, 別のソフトのファイルを断る)
{
    RequireRejects("{\"format\":\"someOtherCad\",\"schemaVersion\":2}", "KCD2-D001",
        "別のソフト");
    RequireRejects("{}", "KCD2-D002", "空の組");
    RequireRejects("[]", "KCD2-D001", "配列");
    RequireRejects("null", "KCD2-D001", "null");
    RequireRejects("\"kachakachaCAD\"", "KCD2-D001", "文字列だけ");
}

KACHA_V2_TEST(documentFile, 知らない版のファイルを断る)
{
    const std::string text = WriteDocumentJson(MakeSampleDocument());
    RequireRejects(Replace(text, "\"schemaVersion\": 2", "\"schemaVersion\": 3"), "KCD2-D001",
        "先の版");
    RequireRejects(Replace(text, "\"schemaVersion\": 2", "\"schemaVersion\": 1"), "KCD2-D001",
        "前の版");
    RequireRejects(Replace(text, "\"schemaVersion\": 2", "\"schemaVersion\": \"2\""),
        "KCD2-D002", "版が文字列");
}

KACHA_V2_TEST(documentFile, 壊れたJSONを断る)
{
    const std::string text = WriteDocumentJson(MakeSampleDocument());
    const auto read = ReadDocumentJson(text.substr(0, text.size() / 2));
    Require(!read.HasValue(), "途中で切れた文書を断ること");
    RequireEqual(read.Diagnostics().front().code, std::string("KCD2-J001"), "JSONの診断");
}

KACHA_V2_TEST(documentFile, 必要な項目が無ければ断る)
{
    const std::string text = WriteDocumentJson(MakeSampleDocument());
    const char* required[]{"\"documentId\"", "\"revision\"", "\"tolerances\"", "\"metadata\"",
        "\"groups\"", "\"entities\"", "\"features\""};
    for (const char* key : required) {
        // キー名を変えて「無い」状態にする。
        const std::string broken = Replace(text, key, "\"消えた項目\"");
        const auto read = ReadDocumentJson(broken);
        Require(!read.HasValue(), std::string("無ければ断ること: ") + key);
    }
}

KACHA_V2_TEST(documentFile, 型が違えば断る)
{
    const std::string text = WriteDocumentJson(MakeSampleDocument());
    RequireRejects(Replace(text, "\"revision\": 42", "\"revision\": \"42\""), "KCD2-D002",
        "版数が文字列");
    RequireRejects(Mutate(text, [](auto& root) {
        root["entities"] = JsonValue::Object({});
    }), "KCD2-D002", "オブジェクトの一覧が組");
    RequireRejects(Mutate(text, [](auto& root) {
        root["features"] = JsonValue::String("なにか");
    }), "KCD2-D002", "指示の一覧が文字列");
    RequireRejects(Mutate(text, [](auto& root) {
        root["uiState"] = JsonValue::Array({});
    }), "KCD2-D002", "画面の状態が配列");
    RequireRejects(Replace(text, "\"title\": \"ER2前頭部\"", "\"title\": 5"), "KCD2-D002",
        "題名が数");
    RequireRejects(Replace(text, "\"enabled\": true", "\"enabled\": \"true\""), "KCD2-D002",
        "有効かどうかが文字列");
}

KACHA_V2_TEST(documentFile, 知らない種類を断る)
{
    const std::string text = WriteDocumentJson(MakeSampleDocument());
    RequireRejects(Replace(text, "\"kind\": \"point\"", "\"kind\": \"blob\""), "KCD2-D003",
        "知らないオブジェクトの種類");
    RequireRejects(Replace(text, "\"type\": \"create_point\"", "\"type\": \"do_magic\""),
        "KCD2-D003", "知らない指示");
    RequireRejects(Replace(text, "\"visibility\": \"visible\"", "\"visibility\": \"maybe\""),
        "KCD2-D003", "知らない表示状態");
    RequireRejects(Replace(text, "\"editPolicy\": \"source\"", "\"editPolicy\": \"editable\""),
        "KCD2-D003", "知らない編集方針");
    RequireRejects(Replace(text, "\"type\": \"line\"", "\"type\": \"nurbs\""), "KCD2-D003",
        "知らない線の種類");
    RequireRejects(Replace(text, "\"method\": \"rotate\"", "\"method\": \"warp\""), "KCD2-D003",
        "知らない編集方法");
    RequireRejects(Replace(text, "\"purpose\": \"fabrication_part\"", "\"purpose\": \"toy\""),
        "KCD2-D003", "知らない用途");
}

KACHA_V2_TEST(documentFile, IDの書き方が違えば断る)
{
    const std::string text = WriteDocumentJson(MakeSampleDocument());
    const char* wrong[]{"\"\"", "\"not-a-uuid\"", "\"00000000000040008000000000000001\"",
        "\"00000000-0000-4000-8000-00000000000\"", "\"00000000-0000-4000-8000-0000000000011\"",
        "\"00000000-0000-4000-8000-00000000000G\"",
        "\"00000000-0000-4000-8000-000000000001 \"",
        "\"{00000000-0000-4000-8000-000000000001}\"",
        "\"00000000-0000-4000-8000-000000000001\\n\""};
    for (const char* value : wrong) {
        const std::string broken = Replace(text,
            "\"documentId\": \"00000000-0000-4000-8000-000000000001\"",
            std::string("\"documentId\": ") + value);
        RequireRejects(broken, "KCD2-D002", std::string("IDが ") + value);
    }
    // 大文字のUUIDも受け付けない(kcd2-format.md §3: lowercase canonical)。
    RequireRejects(Replace(text, "\"documentId\": \"00000000-0000-4000-8000-000000000001\"",
                       "\"documentId\": \"00000000-0000-4000-8000-00000000000A\""),
        "KCD2-D002", "大文字のID");
}

KACHA_V2_TEST(documentFile, 有限でない数を断る)
{
    const std::string text = WriteDocumentJson(MakeSampleDocument());
    // JSON としてそもそも NaN / Infinity は書けない。数でないものが来たら断る。
    RequireRejects(Mutate(text, [](auto& root) {
        root["tolerances"].MutableObject()["modelLinearMm"] = JsonValue::String("1e-06");
    }), "KCD2-D002", "許容差が文字列");
    const auto nan = ReadDocumentJson(Replace(text, "\"revision\": 42", "\"revision\": NaN"));
    Require(!nan.HasValue(), "NaN と書かれたら断ること");
}

KACHA_V2_TEST(documentFile, 許容差が0や負なら断る)
{
    const std::string text = WriteDocumentJson(MakeSampleDocument());
    const auto setTolerance = [](const char* key, double value) {
        return [key, value](kachakacha::v2::io::JsonObject& root) {
            root["tolerances"].MutableObject()[key] = JsonValue::Number(value);
        };
    };
    RequireRejects(Mutate(text, setTolerance("modelLinearMm", 0.0)), "KCD2-D002", "0の許容差");
    RequireRejects(Mutate(text, setTolerance("modelLinearMm", -1e-6)), "KCD2-D002",
        "負の許容差");
    RequireRejects(Mutate(text, setTolerance("interactiveJoinMm", 0.0)), "KCD2-D002",
        "0のつながり許容差");
    RequireRejects(Mutate(text, setTolerance("numericEpsilon", 0.0)), "KCD2-D002",
        "0の数値許容差");
    RequireRejects(Mutate(text, setTolerance("modelAngularRad", -1.0)), "KCD2-D002",
        "負の角度許容差");
}

KACHA_V2_TEST(documentFile, 版数が負や小数なら断る)
{
    const std::string text = WriteDocumentJson(MakeSampleDocument());
    RequireRejects(Replace(text, "\"revision\": 42", "\"revision\": -1"), "KCD2-D002", "負");
    RequireRejects(Replace(text, "\"revision\": 42", "\"revision\": 4.5"), "KCD2-D002", "小数");
}

KACHA_V2_TEST(documentFile, 部品の属性の付け方が違えば断る)
{
    const std::string text = WriteDocumentJson(MakeSampleDocument());
    // 部品なのに属性が null。
    // 部品は entities[3]。属性を null にすると断ること。
    RequireRejects(Mutate(text, [](auto& root) {
        root["entities"].MutableArray()[3].MutableObject()["partProperties"] = JsonValue::Null();
    }), "KCD2-D004", "部品に属性が無い");
    // 部品でないものに属性を付けても断ること。
    RequireRejects(Mutate(text, [](auto& root) {
        kachakacha::v2::io::JsonObject part;
        part["purpose"] = JsonValue::String("finished_model");
        part["manufacturing"] = JsonValue::Null();
        root["entities"].MutableArray()[0].MutableObject()["partProperties"] =
            JsonValue::Object(std::move(part));
    }), "KCD2-D004", "点に部品の属性");

    const auto setManufacturing = [](const char* key, double value) {
        return [key, value](kachakacha::v2::io::JsonObject& root) {
            root["entities"].MutableArray()[3].MutableObject()["partProperties"]
                .MutableObject()["manufacturing"].MutableObject()[key] =
                JsonValue::Number(value);
        };
    };
    RequireRejects(Mutate(text, setManufacturing("nominalThicknessMm", 0.0)), "KCD2-D002",
        "板厚が0");
    RequireRejects(Mutate(text, setManufacturing("nominalThicknessMm", -1.0)), "KCD2-D002",
        "板厚が負");
    RequireRejects(Mutate(text, setManufacturing("referenceScaleDenominator", 0.0)),
        "KCD2-D002", "縮尺が0");
    RequireRejects(Mutate(text, setManufacturing("layerCount", 0.0)), "KCD2-D002",
        "重ね枚数が0");
}

KACHA_V2_TEST(documentFile, 参照切れを断る)
{
    const std::string text = WriteDocumentJson(MakeSampleDocument());
    // 無いグループを指す。
    RequireRejects(Replace(text, "\"activeGroupId\": \"", "\"activeGroupId\": \"ffffffff-0000-4000-8000-00000000000"),
        "KCD2-D002", "IDが長くなって壊れる");
    // Entity が無いFeatureを createdBy に持つ。
    const std::string broken = Replace(text, "\"createdBy\": \"", "\"createdBy\": \"ffffffff");
    const auto read = ReadDocumentJson(broken);
    Require(!read.HasValue(), "参照が壊れていたら断ること");
}

KACHA_V2_TEST(documentFile, 出力の名前が重なっていたら断る)
{
    DocumentFile file = MakeSampleDocument();
    file.snapshot.features[0].outputs.push_back(
        FeatureOutput{"point", file.snapshot.entities[0].id, EntityKind::Point});
    RequireRejects(WriteDocumentJson(file), "KCD2-D004", "同じ出力名が2つ");
}

KACHA_V2_TEST(documentFile, 線として成り立たない記述を断る)
{
    const std::string text = WriteDocumentJson(MakeSampleDocument());
    const auto setArc = [](const char* key, double value) {
        return [key, value](kachakacha::v2::io::JsonObject& root) {
            DefinitionOf(root, 2)["wire"].MutableObject()["segments"].MutableArray()[1]
                .MutableObject()[key] = JsonValue::Number(value);
        };
    };
    RequireRejects(Mutate(text, setArc("radiusMm", 0.0)), "KCD2-D002", "半径0の円弧");
    RequireRejects(Mutate(text, setArc("radiusMm", -5.0)), "KCD2-D002", "半径が負");
    RequireRejects(Mutate(text, setArc("sweepAngleRad", 0.0)), "KCD2-D002", "掃引角0");
    // 長さ0の直線。ワイヤーは features[2]、その最初の線が直線。
    RequireRejects(Mutate(text, [](auto& root) {
        auto& segment = DefinitionOf(root, 2)["wire"].MutableObject()["segments"]
                            .MutableArray()[0].MutableObject();
        segment["end"] = segment["start"];
    }), "KCD2-D002", "長さ0の直線");
    // 制御点の数が足りないBezier。
    RequireRejects(Mutate(text, [](auto& root) {
        auto& segment = DefinitionOf(root, 2)["wire"].MutableObject()["segments"]
                            .MutableArray()[3].MutableObject();
        segment["controlPoints"].MutableArray().pop_back();
    }), "KCD2-D002", "制御点が3つのBezier");
    // 法線が零ベクトルの円。
    RequireRejects(Mutate(text, [](auto& root) {
        auto& segment = DefinitionOf(root, 2)["wire"].MutableObject()["segments"]
                            .MutableArray()[2].MutableObject();
        kachakacha::v2::io::JsonObject zero;
        zero["x"] = JsonValue::Number(0.0);
        zero["y"] = JsonValue::Number(0.0);
        zero["z"] = JsonValue::Number(0.0);
        segment["normal"] = JsonValue::Object(std::move(zero));
    }), "KCD2-D002", "法線が零の円");
}

KACHA_V2_TEST(documentFile, 近い形へ黙って直さない)
{
    // 読めない円弧を「折れ線にして開く」ような振る舞いをしていないこと。
    const std::string text = WriteDocumentJson(MakeSampleDocument());
    const std::string broken = Mutate(text, [](auto& root) {
        DefinitionOf(root, 2)["wire"].MutableObject()["segments"].MutableArray()[1]
            .MutableObject()["radiusMm"] = JsonValue::Number(0.0);
    });
    const auto read = ReadDocumentJson(broken);
    Require(!read.HasValue(), "値を返さないこと");
    const bool explains = std::any_of(read.Diagnostics().begin(), read.Diagnostics().end(),
        [](const auto& diagnostic) {
            return diagnostic.detailsJa.find("segments") != std::string::npos;
        });
    Require(explains, "どの線が悪いのかを示すこと");
}

KACHA_V2_TEST(documentFile, 日本語がそのまま残る)
{
    const DocumentFile original = MakeSampleDocument();
    const std::string text = WriteDocumentJson(original);
    Require(text.find("ER2前頭部") != std::string::npos,
        "題名がそのままのバイト列で入っていること");
    Require(text.find("窓上輪郭") != std::string::npos, "名前がそのまま入っていること");
    const auto read = ReadDocumentJson(text);
    Require(read.HasValue(), "読めること");
    RequireEqual(read.Value().snapshot.entities[2].displayName, std::string("窓上輪郭"), "名前");
}

KACHA_V2_TEST(documentFile, 絵文字や制御文字を含む名前も往復する)
{
    DocumentFile file = MakeSampleDocument();
    file.snapshot.entities[0].displayName = "名前\tタブ\n改行\"引用\\逆斜線🚃電車";
    file.metadata.description = "ゼロ幅​と絵文字😀";
    const auto read = ReadDocumentJson(WriteDocumentJson(file));
    Require(read.HasValue(), "読めること");
    RequireEqual(read.Value().snapshot.entities[0].displayName,
        file.snapshot.entities[0].displayName, "名前");
    RequireEqual(read.Value().metadata.description, file.metadata.description, "説明");
}

KACHA_V2_TEST(documentFile, 評価順を読み込み時に作り直す)
{
    const DocumentFile original = MakeSampleDocument();
    const auto read = ReadDocumentJson(WriteDocumentJson(original));
    Require(read.HasValue(), "読めること");
    const auto& order = read.Value().snapshot.evaluationOrder;
    RequireCount(order.size(), original.snapshot.features.size(), "全部の指示が並ぶこと");
    // 押し出しは、その入力になっているワイヤーより後に来ること。
    const auto& features = read.Value().snapshot.features;
    const auto position = [&](const FeatureId& id) {
        return static_cast<std::size_t>(
            std::find(order.begin(), order.end(), id) - order.begin());
    };
    Require(position(features[3].id) > position(features[2].id),
        "押し出しはワイヤーより後");
    Require(position(features[5].id) > position(features[4].id),
        "固定は回転より後");
}

KACHA_V2_TEST_MAIN("document_file_tests")
