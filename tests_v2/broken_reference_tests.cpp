// 壊れた参照(AT-GEO-007)。
//
// 契約が名指しで禁じていること: 「最寄り要素へ壊れた参照を自動付け替えする」。
// ここではその禁止を、機械で守る。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/document/BrokenReference.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/document/Document.h"

#include <algorithm>
#include <string>

using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::document::AddFeatureCommand;
using kachakacha::v2::document::BrokenReference;
using kachakacha::v2::document::Document;
using kachakacha::v2::document::DocumentSnapshot;
using kachakacha::v2::document::FindBrokenReferences;
using kachakacha::v2::document::RemoveFeatureCommand;
using kachakacha::v2::document::RemovePolicy;
using kachakacha::v2::document::RepairBrokenReference;
using kachakacha::v2::document::RepairCandidatesFor;
using kachakacha::v2::document::RepairChoice;
using kachakacha::v2::document::RepairChoiceNameJa;
using kachakacha::v2::domain::CreateWireDefinition;
using kachakacha::v2::domain::Entity;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::Feature;
using kachakacha::v2::domain::FeatureOutput;
using kachakacha::v2::domain::FeatureType;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] std::string FirstCode(const std::vector<Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

struct Made {
    Feature feature;
    Entity entity;
};

struct Maker {
    DeterministicIdGenerator ids{61};

    [[nodiscard]] Made MakeWire(const std::string& name, Vector3 from, Vector3 to)
    {
        Made made;
        made.feature.id = ids.NextTyped<IdKind::Feature>();
        made.feature.type = FeatureType::CreateWire;
        made.feature.displayName = name;
        CreateWireDefinition definition;
        definition.segments.push_back(CurveSegment::MakeLine(from, to).Value());
        definition.segmentIds.push_back(ids.NextTyped<IdKind::Segment>());
        made.feature.definition = definition;
        made.entity.id = ids.NextTyped<IdKind::Entity>();
        made.entity.kind = EntityKind::Wire;
        made.entity.displayName = name;
        made.entity.createdBy = made.feature.id;
        made.feature.outputs.push_back(
            FeatureOutput{"wire", made.entity.id, EntityKind::Wire});
        return made;
    }

    //! 形状ガイドのつもりの操作。入力のワイヤーを指す。
    [[nodiscard]] Made MakeGuide(const std::string& name, std::vector<EntityId> inputs)
    {
        Made made;
        made.feature.id = ids.NextTyped<IdKind::Feature>();
        made.feature.type = FeatureType::CreateGuideSurface;
        made.feature.displayName = name;
        made.feature.inputEntityIds = std::move(inputs);
        made.entity.id = ids.NextTyped<IdKind::Entity>();
        made.entity.kind = EntityKind::GuideSurface;
        made.entity.displayName = name;
        made.entity.createdBy = made.feature.id;
        made.feature.outputs.push_back(
            FeatureOutput{"surface", made.entity.id, EntityKind::GuideSurface});
        return made;
    }
};

//! 断面を3本置き、真ん中を指す形状ガイドを作った文書。
struct Fixture {
    Maker maker;
    Document document{DeterministicIdGenerator(62).NextTyped<IdKind::Document>()};
    Made first;
    Made middle;
    Made last;
    Made guide;

    Fixture()
        : first(maker.MakeWire("断面1", {0, 0, 0}, {0, 20, 0}))
        , middle(maker.MakeWire("断面2", {30, 0, 0}, {30, 20, 0}))
        , last(maker.MakeWire("断面3", {60, 0, 0}, {60, 20, 0}))
    {
        (void)document.Run(AddFeatureCommand(first.feature, {first.entity}, "断面1"));
        (void)document.Run(AddFeatureCommand(middle.feature, {middle.entity}, "断面2"));
        (void)document.Run(AddFeatureCommand(last.feature, {last.entity}, "断面3"));
        guide = maker.MakeGuide("形状ガイド", {middle.entity.id});
        (void)document.Run(AddFeatureCommand(guide.feature, {guide.entity}, "ガイド"));
    }

    //! 指していたものが消えた断面を作る。
    //!
    //! Command はそもそも壊れた参照を残さない(下流があれば断るか、まとめて消す)。
    //! 壊れた参照が生まれるのは、外から来た文書や、再計算で意味的キーが
    //! 消えたときである。ここではその状態を直接こしらえる。
    [[nodiscard]] DocumentSnapshot WithoutMiddle() const
    {
        return Without({middle.entity.id}, {middle.feature.id});
    }

    [[nodiscard]] DocumentSnapshot Without(const std::vector<EntityId>& entities,
        const std::vector<kachakacha::v2::base::FeatureId>& features) const
    {
        DocumentSnapshot snapshot = document.Snapshot();
        snapshot.entities.erase(
            std::remove_if(snapshot.entities.begin(), snapshot.entities.end(),
                [&](const Entity& entity) {
                    return std::find(entities.begin(), entities.end(), entity.id)
                        != entities.end();
                }),
            snapshot.entities.end());
        snapshot.features.erase(
            std::remove_if(snapshot.features.begin(), snapshot.features.end(),
                [&](const Feature& feature) {
                    return std::find(features.begin(), features.end(), feature.id)
                        != features.end();
                }),
            snapshot.features.end());
        return snapshot;
    }
};

[[nodiscard]] const Feature* FindFeatureIn(const DocumentSnapshot& snapshot,
    const kachakacha::v2::base::FeatureId& id)
{
    for (const Feature& feature : snapshot.features) {
        if (feature.id == id) {
            return &feature;
        }
    }
    return nullptr;
}

} // namespace

KACHA_V2_TEST(broken_ref, 参照が生きているうちは壊れていない)
{
    Fixture fixture;
    Require(FindBrokenReferences(fixture.document.Snapshot()).empty(), "0件");
}

KACHA_V2_TEST(broken_ref, 指していた線を消すと壊れた参照になる)
{
    Fixture fixture;
    // 断面2 が消えた状態。形状ガイドはそれを指している。
    const auto broken = FindBrokenReferences(fixture.WithoutMiddle());
    RequireEqual(std::to_string(broken.size()), "1", "1件");
    RequireEqual(broken.front().featureId.ToString(),
        fixture.guide.feature.id.ToString(), "形状ガイドが壊れている");
    RequireEqual(broken.front().missingEntityId.ToString(),
        fixture.middle.entity.id.ToString(), "消えた相手を覚えている");
    Require(!broken.front().summaryJa.empty(), "画面に出す文がある");
}

KACHA_V2_TEST(broken_ref, 近くの線へ勝手に付け替わらない)
{
    // 契約が名指しで禁じていること。断面2 を消しても、
    // 形状ガイドの入力が断面1や断面3へ移ってはならない。
    Fixture fixture;
    const EntityId gone = fixture.middle.entity.id;
    const DocumentSnapshot after = fixture.WithoutMiddle();

    const Feature* guide = FindFeatureIn(after, fixture.guide.feature.id);
    Require(guide != nullptr, "形状ガイドは残っている");
    RequireEqual(std::to_string(guide->inputEntityIds.size()), "1", "入力は1つのまま");
    RequireEqual(guide->inputEntityIds.front().ToString(), gone.ToString(),
        "消えたものを指したまま。近くへ移っていない");
    Require(guide->inputEntityIds.front() != fixture.first.entity.id, "断面1でない");
    Require(guide->inputEntityIds.front() != fixture.last.entity.id, "断面3でない");
}

KACHA_V2_TEST(broken_ref, 候補は挙げるが選ばない)
{
    Fixture fixture;
    const DocumentSnapshot after = fixture.WithoutMiddle();
    const auto broken = FindBrokenReferences(after);
    const auto candidates = RepairCandidatesFor(after, broken.front(),
        Vector3{30, 10, 0}, 5);
    Require(candidates.size() >= 2, "候補が挙がる");
    // 近い順に並ぶ。
    for (std::size_t at = 0; at + 1 < candidates.size(); ++at) {
        Require(candidates[at].distanceMm <= candidates[at + 1].distanceMm,
            "近い順");
    }
    // 挙げただけでは、文書は変わっていない。
    const Feature* guide = FindFeatureIn(after, fixture.guide.feature.id);
    RequireEqual(guide->inputEntityIds.front().ToString(),
        fixture.middle.entity.id.ToString(), "まだ壊れたまま");
}

KACHA_V2_TEST(broken_ref, 相手を選ばずに指し直そうとしたら断る)
{
    Fixture fixture;
    const DocumentSnapshot after = fixture.WithoutMiddle();
    const auto broken = FindBrokenReferences(after);
    const auto refused = RepairBrokenReference(after, broken.front(),
        RepairChoice::Reassign, std::nullopt);
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "DOC-C008", "選ばれていない");
    Require(refused.Diagnostics().front().detailsJa.find("勝手に") != std::string::npos,
        "勝手に付け替えないと言う");
}

KACHA_V2_TEST(broken_ref, 候補が1つでも自動では選ばない)
{
    // 断面1と断面3も消して、候補を1つだけにする。それでも自動で選ばない。
    Fixture fixture;
    const DocumentSnapshot after = fixture.Without(
        {fixture.middle.entity.id, fixture.last.entity.id},
        {fixture.middle.feature.id, fixture.last.feature.id});
    const auto broken = FindBrokenReferences(after);
    const auto candidates = RepairCandidatesFor(after, broken.front(),
        Vector3{30, 10, 0}, 5);
    RequireEqual(std::to_string(candidates.size()), "1", "候補は1つ");
    const auto refused = RepairBrokenReference(after, broken.front(),
        RepairChoice::Reassign, std::nullopt);
    Require(!refused.HasValue(), "それでも断る");
}

KACHA_V2_TEST(broken_ref, 選んだ相手へは指し直せる)
{
    Fixture fixture;
    const DocumentSnapshot after = fixture.WithoutMiddle();
    const auto broken = FindBrokenReferences(after);
    const auto candidates = RepairCandidatesFor(after, broken.front(),
        Vector3{30, 10, 0}, 5);
    const auto repaired = RepairBrokenReference(after, broken.front(),
        RepairChoice::Reassign, candidates.front());
    Require(repaired.HasValue(), "指し直せる");
    Require(FindBrokenReferences(repaired.Value()).empty(), "壊れた参照が消える");
    for (const Feature& feature : repaired.Value().features) {
        if (feature.id == fixture.guide.feature.id) {
            RequireEqual(feature.inputEntityIds.front().ToString(),
                candidates.front().entityId.ToString(), "選んだ相手を指す");
        }
    }
}

KACHA_V2_TEST(broken_ref, 効かなくすることもできる)
{
    Fixture fixture;
    const DocumentSnapshot after = fixture.WithoutMiddle();
    const auto broken = FindBrokenReferences(after);
    const auto disabled = RepairBrokenReference(after, broken.front(),
        RepairChoice::Disable, std::nullopt);
    Require(disabled.HasValue(), "効かなくできる");
    bool found = false;
    for (const Feature& feature : disabled.Value().features) {
        if (feature.id == fixture.guide.feature.id) {
            Require(!feature.enabled, "効かなくなる");
            found = true;
        }
    }
    Require(found, "消さずに残る");
}

KACHA_V2_TEST(broken_ref, 消すこともできる)
{
    Fixture fixture;
    const DocumentSnapshot after = fixture.WithoutMiddle();
    const auto broken = FindBrokenReferences(after);
    const auto removed = RepairBrokenReference(after, broken.front(),
        RepairChoice::Remove, std::nullopt);
    Require(removed.HasValue(), "消せる");
    for (const Feature& feature : removed.Value().features) {
        Require(feature.id != fixture.guide.feature.id, "操作が消えている");
    }
    for (const Entity& entity : removed.Value().entities) {
        Require(entity.id != fixture.guide.entity.id, "出来ていたものも消えている");
    }
    Require(FindBrokenReferences(removed.Value()).empty(), "壊れた参照が消える");
}

KACHA_V2_TEST(broken_ref, 3つの直し方すべてに名前がある)
{
    for (RepairChoice choice : {RepairChoice::Reassign, RepairChoice::Disable,
             RepairChoice::Remove}) {
        Require(!std::string(RepairChoiceNameJa(choice)).empty(), "名前がある");
        Require(std::string(RepairChoiceNameJa(choice)) != "不明", "不明でない");
    }
}

KACHA_V2_TEST(broken_ref, 壊れた参照の並びは毎回同じ)
{
    Fixture fixture;
    const DocumentSnapshot after = fixture.Without(
        {fixture.first.entity.id, fixture.middle.entity.id},
        {fixture.first.feature.id, fixture.middle.feature.id});
    const auto once = FindBrokenReferences(after);
    const auto again = FindBrokenReferences(after);
    RequireEqual(std::to_string(once.size()), std::to_string(again.size()), "同じ数");
    for (std::size_t at = 0; at < once.size(); ++at) {
        RequireEqual(once[at].missingEntityId.ToString(),
            again[at].missingEntityId.ToString(), "同じ並び");
    }
}

KACHA_V2_TEST_MAIN("broken_reference_tests")
