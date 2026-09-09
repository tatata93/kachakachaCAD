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

KACHA_V2_TEST(shape_rebuild, 4種類すべてに名前がある)
{
    for (const ShapeRebuildKind kind : {ShapeRebuildKind::Extrude,
             ShapeRebuildKind::WireCage, ShapeRebuildKind::Boolean,
             ShapeRebuildKind::GuideSurface}) {
        Require(!ShapeRebuildKindNameJa(kind).empty(), "名前がある");
        Require(ShapeRebuildKindNameJa(kind) != std::string_view("不明"), "不明でない");
    }
}

KACHA_V2_TEST_MAIN("shape_rebuild_tests")
