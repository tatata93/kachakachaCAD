// 開き直したときに、立体と面を作り直す段取り(app/ShapeRebuild.h)。
//
// 実形状は文書に持たないので、開いたら作り方から作り直すしかない。
// これをしていなかったので、保存して開き直すと立体も面も消えていた。
#include "kachakacha/app/ShapeRebuild.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/document/Commands.h"

#include <string>

using kachakacha::v2::app::NeedsShapeRebuild;
using kachakacha::v2::app::PlanShapeRebuild;
using kachakacha::v2::app::ShapeRebuildKind;
using kachakacha::v2::app::ShapeRebuildKindNameJa;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::document::DocumentSnapshot;
using kachakacha::v2::domain::Entity;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::Feature;
using kachakacha::v2::domain::FeatureOutput;
using kachakacha::v2::domain::FeatureType;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

//! 作り方を1つ足す。出来上がりのものも一緒に足す。
void Add(DocumentSnapshot& snapshot, DeterministicIdGenerator& ids, FeatureType type,
    EntityKind kind, const std::string& name, bool enabled = true)
{
    Feature feature;
    feature.id = ids.NextTyped<IdKind::Feature>();
    feature.type = type;
    feature.displayName = name;
    feature.enabled = enabled;
    Entity entity;
    entity.id = ids.NextTyped<IdKind::Entity>();
    entity.kind = kind;
    entity.displayName = name;
    entity.createdBy = feature.id;
    feature.outputs.push_back(FeatureOutput{"out", entity.id, kind});
    snapshot.evaluationOrder.push_back(feature.id);
    snapshot.features.push_back(std::move(feature));
    snapshot.entities.push_back(std::move(entity));
}

} // namespace

KACHA_V2_TEST(shape_rebuild, 形を作る作り方だけを拾う)
{
    DocumentSnapshot snapshot;
    DeterministicIdGenerator ids;
    Add(snapshot, ids, FeatureType::CreateWire, EntityKind::Wire, "線");
    Add(snapshot, ids, FeatureType::Extrude, EntityKind::Part, "押し出し");
    Add(snapshot, ids, FeatureType::CreateWorkPlane, EntityKind::WorkPlane, "平面");
    Add(snapshot, ids, FeatureType::CreateGuideSurface, EntityKind::GuideSurface, "面");
    const auto steps = PlanShapeRebuild(snapshot);
    Require(steps.size() == 2, "線と作業平面は形を作らない");
    Require(steps[0].kind == ShapeRebuildKind::Extrude, "押し出し");
    Require(steps[1].kind == ShapeRebuildKind::GuideSurface, "形状ガイド");
    Require(NeedsShapeRebuild(snapshot), "作り直しが要る");
}

KACHA_V2_TEST(shape_rebuild, 線しかない文書は作り直しが要らない)
{
    DocumentSnapshot snapshot;
    DeterministicIdGenerator ids;
    Add(snapshot, ids, FeatureType::CreateWire, EntityKind::Wire, "線");
    Require(PlanShapeRebuild(snapshot).empty(), "何も無い");
    Require(!NeedsShapeRebuild(snapshot), "要らない");
}

KACHA_V2_TEST(shape_rebuild, 評価順に従う)
{
    // 足し算は材料の立体が出来た後でないと作れない。
    // 並びを自分で決め直すと、材料がまだ無いと言われる。
    DocumentSnapshot snapshot;
    DeterministicIdGenerator ids;
    Add(snapshot, ids, FeatureType::Boolean, EntityKind::Part, "足す");
    Add(snapshot, ids, FeatureType::Extrude, EntityKind::Part, "土台");
    // 評価順だけを入れ替える。並びの元はこちらである。
    std::swap(snapshot.evaluationOrder[0], snapshot.evaluationOrder[1]);
    const auto steps = PlanShapeRebuild(snapshot);
    Require(steps.size() == 2, "2つ");
    RequireEqual(steps[0].displayName, std::string("土台"), "材料が先");
    RequireEqual(steps[1].displayName, std::string("足す"), "足し算が後");
}

KACHA_V2_TEST(shape_rebuild, 切ってある作り方は飛ばす)
{
    DocumentSnapshot snapshot;
    DeterministicIdGenerator ids;
    Add(snapshot, ids, FeatureType::Extrude, EntityKind::Part, "切ってある", false);
    Require(PlanShapeRebuild(snapshot).empty(), "飛ばす");
}

KACHA_V2_TEST(shape_rebuild, 隠してあるものは飛ばさない)
{
    // 隠れていても、足し算の材料になっていることがある。
    // 飛ばすと、足し算のときに材料が無いと言われる。
    DocumentSnapshot snapshot;
    DeterministicIdGenerator ids;
    Add(snapshot, ids, FeatureType::Extrude, EntityKind::Part, "隠してある");
    snapshot.entities.back().visibility = kachakacha::v2::domain::Visibility::Hidden;
    Require(PlanShapeRebuild(snapshot).size() == 1, "飛ばさない");
}

KACHA_V2_TEST(shape_rebuild, 出来上がりが無い作り方は飛ばす)
{
    DocumentSnapshot snapshot;
    DeterministicIdGenerator ids;
    Feature feature;
    feature.id = ids.NextTyped<IdKind::Feature>();
    feature.type = FeatureType::Extrude;
    feature.displayName = "出来ていない";
    snapshot.evaluationOrder.push_back(feature.id);
    snapshot.features.push_back(std::move(feature));
    Require(PlanShapeRebuild(snapshot).empty(), "指す先が無いので飛ばす");
}

KACHA_V2_TEST(shape_rebuild, 同じ押し出しから出来た部品は作った順に何番目かを持つ)
{
    // 1 回の押し出しで部品が 2 個できると、定義の同じ作り方が 2 個並ぶ。作り直した 2 個の
    // 立体を順に配らないと、2 個目が 1 個目の写しになる。定義の違う押し出しは 0 番から数える。
    DocumentSnapshot snapshot;
    DeterministicIdGenerator ids;
    kachakacha::v2::domain::ExtrudeDefinition same;
    same.profiles = {ids.NextTyped<IdKind::Entity>(), ids.NextTyped<IdKind::Entity>()};
    same.distance.value = 10.0;
    for (const char* name : {"押し出し 1", "押し出し 2"}) {
        Add(snapshot, ids, FeatureType::Extrude, EntityKind::Part, name);
        snapshot.features.back().definition = same;
    }
    auto other = same;
    other.distance.value = 20.0;
    Add(snapshot, ids, FeatureType::Extrude, EntityKind::Part, "別の押し出し");
    snapshot.features.back().definition = other;
    Add(snapshot, ids, FeatureType::Extrude, EntityKind::Part, "押し出し 3", false);
    snapshot.features.back().definition = same;
    const auto steps = PlanShapeRebuild(snapshot);
    Require(steps.size() == 3, "切ってあるものは作り直さない");
    Require(steps[0].outputOrdinal == 0 && steps[1].outputOrdinal == 1, "同じ定義は 0, 1 番");
    Require(steps[2].outputOrdinal == 0, "定義の違う押し出しは 0 番");
}

KACHA_V2_TEST(shape_rebuild, 部品の配置は形を作る作り方で材料の後に作り直す)
{
    // P-18: 動かした・写した部品は、元の部品の形に変換を掛けて作り直す。評価順のとおり元が先。
    DocumentSnapshot snapshot;
    DeterministicIdGenerator ids;
    Add(snapshot, ids, FeatureType::Extrude, EntityKind::Part, "元の箱");
    Add(snapshot, ids, FeatureType::TransformPart, EntityKind::Part, "鏡に写した箱");
    const auto steps = PlanShapeRebuild(snapshot);
    Require(steps.size() == 2, "配置も作り直す");
    Require(steps[0].kind == ShapeRebuildKind::Extrude, "元が先");
    Require(steps[1].kind == ShapeRebuildKind::TransformPart, "配置が後");
}

KACHA_V2_TEST(shape_rebuild, シェルと分割は元の部品の後に作り直す)
{
    // P-13: シェル・分割は元の部品の形に掛ける。評価順のとおり元が先。
    DocumentSnapshot snapshot;
    DeterministicIdGenerator ids;
    Add(snapshot, ids, FeatureType::Extrude, EntityKind::Part, "元の箱");
    Add(snapshot, ids, FeatureType::ShellSplit, EntityKind::Part, "分割");
    Add(snapshot, ids, FeatureType::ShellSplit, EntityKind::Part, "分割 2");
    const auto steps = PlanShapeRebuild(snapshot);
    Require(steps.size() == 3, "分けた両側も作り直す");
    Require(steps[0].kind == ShapeRebuildKind::Extrude, "元が先");
    Require(steps[1].kind == ShapeRebuildKind::ShellSplit && steps[2].kind == ShapeRebuildKind::ShellSplit,
        "形状編集が後");
}

KACHA_V2_TEST(shape_rebuild, 全種類に名前がある)
{
    for (const ShapeRebuildKind kind : {ShapeRebuildKind::Extrude,
             ShapeRebuildKind::WireCage, ShapeRebuildKind::Boolean,
             ShapeRebuildKind::GuideSurface, ShapeRebuildKind::ThickenSurface,
             ShapeRebuildKind::FabricationModel, ShapeRebuildKind::EditSurface,
             ShapeRebuildKind::TransformPart, ShapeRebuildKind::Solid,
             ShapeRebuildKind::EdgeFinish, ShapeRebuildKind::ShellSplit}) {
        Require(!ShapeRebuildKindNameJa(kind).empty(), "名前がある");
        Require(ShapeRebuildKindNameJa(kind) != std::string_view("不明"), "不明でない");
    }
}

KACHA_V2_TEST_MAIN("shape_rebuild_tests")
