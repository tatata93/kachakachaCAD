// 新しく足した Feature の定義が、保存して読み直しても同じかどうか。
//
// 定義を足したのに保存を忘れる、というのがいちばん怖い。
// 画面では作れるのに、開き直すと消えている、という壊れ方をする。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/io/DocumentFile.h"

#include <string>

using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::SegmentId;
using kachakacha::v2::base::Uuid;
using kachakacha::v2::document::DocumentSnapshot;
using kachakacha::v2::domain::BooleanDefinition;
using kachakacha::v2::domain::CreateFabricationModelDefinition;
using kachakacha::v2::domain::CreateGuideSurfaceDefinition;
using kachakacha::v2::domain::CreatePartFromWireCageDefinition;
using kachakacha::v2::domain::CreatePatternDefinition;
using kachakacha::v2::domain::CreateWorkPlaneDefinition;
using kachakacha::v2::domain::Entity;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::ExtrudeDefinition;
using kachakacha::v2::domain::Feature;
using kachakacha::v2::domain::FeatureOutput;
using kachakacha::v2::domain::FeatureType;
using kachakacha::v2::domain::ProjectWireDefinition;
using kachakacha::v2::domain::SegmentRef;
using kachakacha::v2::domain::WireChainRef;
using kachakacha::v2::io::DocumentFile;
using kachakacha::v2::io::LoadDocument;
using kachakacha::v2::io::SaveDocument;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] EntityId Ent(std::uint8_t number)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = number;
    return EntityId(Uuid(bytes));
}

[[nodiscard]] SegmentId Seg(std::uint8_t number)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[14] = number;
    return SegmentId(Uuid(bytes));
}

//! その定義を1つだけ持つ文書を作り、保存して読み直す。
template<class Definition>
[[nodiscard]] Definition RoundTrip(FeatureType type, Definition definition)
{
    DocumentFile file;
    std::array<std::uint8_t, 16> documentBytes{};
    documentBytes[15] = 1;
    file.snapshot.id = kachakacha::v2::base::DocumentId(Uuid(documentBytes));
    Feature feature;
    std::array<std::uint8_t, 16> featureBytes{};
    featureBytes[13] = 1;
    feature.id = kachakacha::v2::base::FeatureId(Uuid(featureBytes));
    feature.type = type;
    feature.displayName = "しけん";
    feature.definition = std::move(definition);
    Entity entity;
    entity.id = Ent(200);
    entity.kind = EntityKind::Part;
    entity.displayName = "しけん";
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{"out", entity.id, EntityKind::Part});
    file.snapshot.features.push_back(std::move(feature));
    file.snapshot.entities.push_back(std::move(entity));

    const auto archive = SaveDocument(file);
    Require(archive.HasValue(), "保存できる");
    const auto loaded = LoadDocument(archive.Value());
    for (const auto& diagnostic : loaded.Diagnostics()) {
        Require(!diagnostic.IsError(), "読める: " + diagnostic.code + " "
                + diagnostic.summaryJa + " / " + diagnostic.detailsJa);
    }
    Require(loaded.HasValue(), "読める");
    RequireEqual(std::to_string(loaded.Value().snapshot.features.size()),
        std::string("1"), "Featureが1つ");
    const auto* got = std::get_if<Definition>(
        &loaded.Value().snapshot.features.front().definition);
    Require(got != nullptr, "同じ種類の定義で戻る");
    return *got;
}

} // namespace

KACHA_V2_TEST(feature_definition, 作業平面が保存して読み直せる)
{
    CreateWorkPlaneDefinition made;
    made.method = 3;
    made.inputs = {Ent(1), Ent(2)};
    made.origin = {1.0, 2.0, 3.0};
    made.normal = {0.0, 1.0, 0.0};
    made.uDirection = {0.0, 0.0, 1.0};
    made.offset.value = 12.5;
    made.offset.expression = "12.5";
    const auto back = RoundTrip(FeatureType::CreateWorkPlane, made);
    RequireEqual(std::to_string(back.method), std::string("3"), "作り方");
    RequireEqual(std::to_string(back.inputs.size()), std::string("2"), "入力の数");
    RequireNear(back.origin.y, 2.0, 1.0e-12, "原点");
    RequireNear(back.normal.y, 1.0, 1.0e-12, "法線");
    RequireNear(back.offset.value, 12.5, 1.0e-12, "距離");
    RequireEqual(back.offset.expression, std::string("12.5"), "式も残る");
}

KACHA_V2_TEST(feature_definition, 面へ投影が保存して読み直せる)
{
    ProjectWireDefinition made;
    made.inputs = {Ent(5)};
    made.targetPlaneId = Ent(9);
    made.direction = {0.0, 0.0, -1.0};
    const auto back = RoundTrip(FeatureType::ProjectWire, made);
    Require(back.targetPlaneId == Ent(9), "投影先");
    RequireNear(back.direction.z, -1.0, 1.0e-12, "向き");
}

KACHA_V2_TEST(feature_definition, 形状ガイドが保存して読み直せる)
{
    CreateGuideSurfaceDefinition made;
    made.method = 2;
    WireChainRef chain;
    chain.segments.push_back(SegmentRef{Ent(1), Seg(1)});
    chain.segments.push_back(SegmentRef{Ent(2), Seg(2)});
    chain.reversed = {false, true};
    made.chains.push_back(chain);
    made.roles = {1};
    made.revolveAxisPoint = {1.0, 2.0, 3.0};
    made.revolveAxisDirection = {0.0, 1.0, 0.0};
    made.revolveAngleRad = 1.25;
    const auto back = RoundTrip(FeatureType::CreateGuideSurface, made);
    RequireEqual(std::to_string(back.method), std::string("2"), "作り方");
    RequireNear(back.revolveAxisPoint.z, 3.0, 1.0e-12, "回転体の軸の点");
    RequireNear(back.revolveAxisDirection.y, 1.0, 1.0e-12, "回転体の軸の向き");
    RequireNear(back.revolveAngleRad, 1.25, 1.0e-12, "回転体の角度");
    RequireEqual(std::to_string(back.chains.size()), std::string("1"), "鎖の数");
    RequireEqual(std::to_string(back.chains.front().segments.size()), std::string("2"),
        "線の数");
    // 向きの反転は形を決める。落ちると別の面ができる。
    Require(!back.chains.front().reversed[0], "1本目はそのまま");
    Require(back.chains.front().reversed[1], "2本目は反転");
    RequireEqual(std::to_string(back.roles.front()), std::string("1"), "役割");
}

KACHA_V2_TEST(feature_definition, 押し出しが保存して読み直せる)
{
    ExtrudeDefinition made;
    made.profiles = {Ent(1), Ent(2)};
    made.direction = {0.0, 0.0, 1.0};
    made.distance.value = 0.5;
    made.extentMode = 1;
    made.booleanMode = 2;
    made.targets = {Ent(7)};
    const auto back = RoundTrip(FeatureType::Extrude, made);
    RequireEqual(std::to_string(back.profiles.size()), std::string("2"), "輪郭の数");
    RequireNear(back.distance.value, 0.5, 1.0e-12, "距離");
    RequireEqual(std::to_string(back.extentMode), std::string("1"), "伸ばし方");
    RequireEqual(std::to_string(back.booleanMode), std::string("2"), "足すか引くか");
    RequireEqual(std::to_string(back.targets.size()), std::string("1"), "相手の数");
}

KACHA_V2_TEST(feature_definition, かごから部品が保存して読み直せる)
{
    CreatePartFromWireCageDefinition made;
    made.wires = {Ent(1), Ent(2), Ent(3)};
    made.thickness.value = 0.3;
    made.placement = 2;
    const auto back = RoundTrip(FeatureType::CreatePartFromWireCage, made);
    RequireEqual(std::to_string(back.wires.size()), std::string("3"), "線の数");
    RequireNear(back.thickness.value, 0.3, 1.0e-12, "板厚");
    // 板厚をどちらへ付けるかは外寸と内寸を変える。落ちると寸法が変わる。
    RequireEqual(std::to_string(back.placement), std::string("2"), "板厚の付け方");
}

KACHA_V2_TEST(feature_definition, 足す引くが保存して読み直せる)
{
    BooleanDefinition made;
    made.mode = 1;
    made.targets = {Ent(1)};
    made.tools = {Ent(2), Ent(3)};
    const auto back = RoundTrip(FeatureType::Boolean, made);
    RequireEqual(std::to_string(back.mode), std::string("1"), "引く");
    RequireEqual(std::to_string(back.tools.size()), std::string("2"), "道具の数");
}

KACHA_V2_TEST(feature_definition, 製作モデルが保存して読み直せる)
{
    CreateFabricationModelDefinition made;
    made.parts = {Ent(1)};
    made.materialThickness.value = 0.5;
    made.targetMaxDeviation.value = 0.05;
    made.fidelity = 9;
    const auto back = RoundTrip(FeatureType::CreateFabricationModel, made);
    RequireNear(back.materialThickness.value, 0.5, 1.0e-12, "板厚");
    RequireNear(back.targetMaxDeviation.value, 0.05, 1.0e-12, "目標偏差");
    RequireEqual(std::to_string(back.fidelity), std::string("9"), "忠実度");
}

KACHA_V2_TEST(feature_definition, 近似の方式と曲げ状態が保存して読み直せる)
{
    // V1 の part_model_fold / part_model_assembly / part_model_part_assembly と同じことを
    // 1つの定義で持つ。ここが保存されないと、曲げ具合を決めても開き直すと消える。
    CreateFabricationModelDefinition made;
    made.parts = {Ent(1)};
    made.method = 1;
    made.splitAxis = 0;
    made.automaticBoundaries = false;
    made.maximumPartCount = 7;
    made.minimumPartWidthMm = 6.5;
    made.manualBoundaries = {0.25, 0.75};
    made.masterPercent = 42.0;
    made.creaseProgress = {1.0, 0.5};
    made.bandProgress = {1.0, 0.0, 1.0};
    made.rangeUMin = 0.1;
    made.rangeUMax = 0.9;
    made.rangeVMin = 0.2;
    made.rangeVMax = 0.8;
    const auto back = RoundTrip(FeatureType::CreateFabricationModel, made);
    RequireNear(back.rangeUMin, 0.1, 1.0e-12, "範囲 u 最小");
    RequireNear(back.rangeUMax, 0.9, 1.0e-12, "範囲 u 最大");
    RequireNear(back.rangeVMin, 0.2, 1.0e-12, "範囲 v 最小");
    RequireNear(back.rangeVMax, 0.8, 1.0e-12, "範囲 v 最大");
    RequireEqual(std::to_string(back.method), std::string("1"), "方式");
    RequireEqual(std::to_string(back.splitAxis), std::string("0"), "分割軸");
    Require(!back.automaticBoundaries, "手動境界");
    RequireEqual(std::to_string(back.maximumPartCount), std::string("7"), "上限部材数");
    RequireNear(back.minimumPartWidthMm, 6.5, 1.0e-12, "最小幅");
    RequireEqual(std::to_string(back.manualBoundaries.size()), std::string("2"), "境界の数");
    RequireNear(back.manualBoundaries[1], 0.75, 1.0e-12, "境界の値");
    RequireNear(back.masterPercent, 42.0, 1.0e-12, "組立率");
    RequireEqual(std::to_string(back.creaseProgress.size()), std::string("2"), "折り線の数");
    RequireNear(back.creaseProgress[1], 0.5, 1.0e-12, "折り線の進行度");
    RequireEqual(std::to_string(back.bandProgress.size()), std::string("3"), "帯の数");
    RequireNear(back.bandProgress[1], 0.0, 1.0e-12, "帯の進行度");
}

KACHA_V2_TEST(feature_definition, 古い製作モデルの定義は既定値で読める)
{
    // 曲げ状態の項目が無い文書(前の版)も、既定値で開ける。
    CreateFabricationModelDefinition made;
    made.parts = {Ent(1)};
    const auto back = RoundTrip(FeatureType::CreateFabricationModel, made);
    RequireEqual(std::to_string(back.method), std::string("0"), "方式は V2 が既定");
    RequireNear(back.masterPercent, 100.0, 1.0e-12, "完成形が既定");
    Require(back.creaseProgress.empty() && back.bandProgress.empty(), "個別値は無し");
    Require(back.automaticBoundaries, "自動分割が既定");
    Require(back.rangeUMin == 0.0 && back.rangeUMax == 1.0 && back.rangeVMin == 0.0
            && back.rangeVMax == 1.0,
        "範囲は全体が既定");
}

KACHA_V2_TEST(feature_definition, 型紙が保存して読み直せる)
{
    CreatePatternDefinition made;
    made.fabricationModels = {Ent(1)};
    made.pageWidth.value = 210.0;
    made.pageHeight.value = 297.0;
    made.marginMm.value = 10.0;
    const auto back = RoundTrip(FeatureType::CreatePattern, made);
    RequireNear(back.pageWidth.value, 210.0, 1.0e-12, "紙の幅");
    RequireNear(back.pageHeight.value, 297.0, 1.0e-12, "紙の高さ");
    RequireNear(back.marginMm.value, 10.0, 1.0e-12, "余白");
}

KACHA_V2_TEST_MAIN("feature_definition_tests")
