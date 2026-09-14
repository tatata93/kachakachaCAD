// 整理用のまとまり(フォルダ)。GR-01〜10。
#include "kachakacha/app/GroupTree.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/document/Document.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using kachakacha::v2::app::ChildGroupsOf;
using kachakacha::v2::app::EntitiesUnderGroup;
using kachakacha::v2::app::EntityEffectivelyVisible;
using kachakacha::v2::app::GroupChainVisible;
using kachakacha::v2::app::GroupDepth;
using kachakacha::v2::app::GroupPathJa;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::DocumentId;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::GroupId;
using kachakacha::v2::document::AddFeatureCommand;
using kachakacha::v2::document::AddGroupCommand;
using kachakacha::v2::document::Document;
using kachakacha::v2::document::Group;
using kachakacha::v2::document::MoveEntitiesToGroupCommand;
using kachakacha::v2::document::RemoveGroupCommand;
using kachakacha::v2::document::RenameGroupCommand;
using kachakacha::v2::document::SetGroupParentCommand;
using kachakacha::v2::document::SetGroupVisibilityCommand;
using kachakacha::v2::domain::Entity;
using kachakacha::v2::domain::Feature;
using kachakacha::v2::domain::FeatureType;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::Visibility;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

struct Bench {
    DeterministicIdGenerator ids{7};
    Document document;

    Bench() : document(DocumentId{}) {}

    [[nodiscard]] GroupId AddGroup(const std::string& name,
        std::optional<GroupId> parent = std::nullopt)
    {
        Group group;
        group.id = ids.NextTyped<kachakacha::v2::base::IdKind::Group>();
        group.displayName = name;
        group.parentId = parent;
        const auto id = group.id;
        const auto done = document.Run(AddGroupCommand(std::move(group)));
        Require(done.committed, "まとまりを作れる: " + name);
        return id;
    }

    //! 文書へ物を1つ作る。作り方は他の試験と同じ道(AddFeatureCommand)を通す。
    [[nodiscard]] EntityId AddEntity(const std::string& name,
        std::optional<GroupId> group = std::nullopt)
    {
        Feature feature;
        feature.id = ids.NextTyped<kachakacha::v2::base::IdKind::Feature>();
        feature.type = FeatureType::CreatePoint;
        feature.displayName = name;
        kachakacha::v2::domain::CreatePointDefinition definition;
        definition.positionMm = kachakacha::v2::geometry::Vector3{1.0, 2.0, 3.0};
        feature.definition = definition;
        feature.derivedGroupId = group;
        Entity entity;
        entity.id = ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
        entity.kind = EntityKind::Wire;
        entity.displayName = name;
        entity.createdBy = feature.id;
        entity.groupId = group;
        const auto id = entity.id;
        feature.outputs.push_back(
            kachakacha::v2::domain::FeatureOutput{"out", entity.id, EntityKind::Wire});
        const auto added = document.Run(AddFeatureCommand(feature, {entity}, "作る"));
        Require(added.committed, "物を作れる: " + name);
        return id;
    }

    //! その物を、利用者が自分で隠した状態にする。
    void HideByUser(const EntityId& id)
    {
        const auto done = document.Run(
            kachakacha::v2::document::SetVisibilityCommand({id}, Visibility::Hidden));
        Require(done.committed, "自分で隠せる");
    }

    [[nodiscard]] const kachakacha::v2::document::DocumentSnapshot& Snapshot() const
    {
        return document.Snapshot();
    }
};

} // namespace

KACHA_V2_TEST(group_tree, 入れ子のまとまりを作れる)
{
    // GR-01 / GR-04。
    Bench bench;
    const auto root = bench.AddGroup("RailwayNose_HO");
    const auto sections = bench.AddGroup("Sections", root);
    RequireEqual(std::to_string(GroupDepth(bench.Snapshot(), root)), std::string("0"),
        "最上位の深さは0");
    RequireEqual(std::to_string(GroupDepth(bench.Snapshot(), sections)), std::string("1"),
        "その下は1");
    RequireEqual(GroupPathJa(bench.Snapshot(), sections),
        std::string("RailwayNose_HO/Sections"), "道が繋がる");
    RequireEqual(std::to_string(ChildGroupsOf(bench.Snapshot(), root).size()),
        std::string("1"), "直下は1つ");
}

KACHA_V2_TEST(group_tree, まとまりの名前を変えられる)
{
    // GR-05。
    Bench bench;
    const auto group = bench.AddGroup("なまえ");
    Require(bench.document.Run(RenameGroupCommand(group, "Sections")).committed,
        "名前を変えられる");
    RequireEqual(GroupPathJa(bench.Snapshot(), group), std::string("Sections"), "変わる");
    Require(!bench.document.Run(RenameGroupCommand(group, "")).committed,
        "空の名前は断る");
}

KACHA_V2_TEST(group_tree, まとまりを別のまとまりへ移せる)
{
    // GR-03 の中身(引きずって移す)。
    Bench bench;
    const auto root = bench.AddGroup("RailwayNose_HO");
    const auto loose = bench.AddGroup("Guides");
    Require(bench.document.Run(SetGroupParentCommand(loose, root)).committed, "移せる");
    RequireEqual(GroupPathJa(bench.Snapshot(), loose),
        std::string("RailwayNose_HO/Guides"), "下に入る");
    Require(bench.document.Run(SetGroupParentCommand(loose, std::nullopt)).committed,
        "外へも出せる");
    RequireEqual(GroupPathJa(bench.Snapshot(), loose), std::string("Guides"), "外へ出る");
}

KACHA_V2_TEST(group_tree, 自分の中へは入れられない)
{
    // 輪ができると木を辿れなくなる。
    Bench bench;
    const auto parent = bench.AddGroup("親");
    const auto child = bench.AddGroup("子", parent);
    Require(!bench.document.Run(SetGroupParentCommand(parent, parent)).committed,
        "自分自身の下へは入れない");
    Require(!bench.document.Run(SetGroupParentCommand(parent, child)).committed,
        "自分の子の下へも入れない");
    RequireEqual(GroupPathJa(bench.Snapshot(), child), std::string("親/子"),
        "断られても形は変わらない");
}

KACHA_V2_TEST(group_tree, まとまりを隠しても中身の設定を書き換えない)
{
    // GR-06 と §12。出し直したときに、1つずつ隠していたものまで出てはならない。
    Bench bench;
    const auto group = bench.AddGroup("Sections");
    const auto shown = bench.AddEntity("出ている線", group);
    const auto hidden = bench.AddEntity("自分で隠した線", group);
    bench.HideByUser(hidden);
    Require(bench.document.Run(SetGroupVisibilityCommand(group, false)).committed,
        "まとまりを隠せる");
    for (const auto& entity : bench.Snapshot().entities) {
        Require(!EntityEffectivelyVisible(bench.Snapshot(), entity),
            "まとまりを隠せば中身は見えない");
    }
    Require(bench.document.Run(SetGroupVisibilityCommand(group, true)).committed,
        "まとまりを出せる");
    for (const auto& entity : bench.Snapshot().entities) {
        if (entity.id == shown) {
            Require(EntityEffectivelyVisible(bench.Snapshot(), entity),
                "出していたものは戻る");
        }
        if (entity.id == hidden) {
            Require(!EntityEffectivelyVisible(bench.Snapshot(), entity),
                "自分で隠していたものは隠れたまま");
        }
    }
}

KACHA_V2_TEST(group_tree, 親を隠せば孫まで見えない)
{
    Bench bench;
    const auto root = bench.AddGroup("RailwayNose_HO");
    const auto sections = bench.AddGroup("Sections", root);
    const auto wire = bench.AddEntity("NoseSection_X000", sections);
    (void)wire;
    Require(GroupChainVisible(bench.Snapshot(), sections), "はじめは見える");
    Require(bench.document.Run(SetGroupVisibilityCommand(root, false)).committed, "隠す");
    Require(!GroupChainVisible(bench.Snapshot(), sections), "孫まで届く");
    for (const auto& entity : bench.Snapshot().entities) {
        Require(!EntityEffectivelyVisible(bench.Snapshot(), entity), "中身も見えない");
    }
}

KACHA_V2_TEST(group_tree, まとまりを解いても中身は消えない)
{
    // GR-07。まとまりだけ消して、中身は親へ戻す。
    Bench bench;
    const auto root = bench.AddGroup("RailwayNose_HO");
    const auto sections = bench.AddGroup("Sections", root);
    const auto wire = bench.AddEntity("NoseSection_X000", sections);
    const std::size_t before = bench.Snapshot().entities.size();
    Require(bench.document.Run(RemoveGroupCommand(sections)).committed, "解ける");
    RequireEqual(std::to_string(bench.Snapshot().entities.size()),
        std::to_string(before), "中身は消えない");
    for (const auto& entity : bench.Snapshot().entities) {
        if (entity.id == wire) {
            Require(entity.groupId.has_value() && *entity.groupId == root,
                "中身は親のまとまりへ戻る");
        }
    }
}

KACHA_V2_TEST(group_tree, まとまりの下にある物を数えられる)
{
    Bench bench;
    const auto root = bench.AddGroup("RailwayNose_HO");
    const auto sections = bench.AddGroup("Sections", root);
    const auto guides = bench.AddGroup("Guides", root);
    (void)bench.AddEntity("NoseSection_X000", sections);
    (void)bench.AddEntity("NoseSection_X003", sections);
    (void)bench.AddEntity("RoofCenterGuide", guides);
    (void)bench.AddEntity("まとまりの外", std::nullopt);
    RequireEqual(std::to_string(EntitiesUnderGroup(bench.Snapshot(), root).size()),
        std::string("3"), "下のまとまりの中身まで数える");
    RequireEqual(std::to_string(EntitiesUnderGroup(bench.Snapshot(), sections).size()),
        std::string("2"), "自分の直下だけ");
}

KACHA_V2_TEST(group_tree, 物をまとまりへ入れても出しても消えない)
{
    // GR-02。
    Bench bench;
    const auto group = bench.AddGroup("Sections");
    const auto wire = bench.AddEntity("NoseSection_X000");
    Require(bench.document.Run(MoveEntitiesToGroupCommand({wire}, group)).committed,
        "入れられる");
    RequireEqual(std::to_string(EntitiesUnderGroup(bench.Snapshot(), group).size()),
        std::string("1"), "入る");
    Require(bench.document.Run(MoveEntitiesToGroupCommand({wire}, std::nullopt)).committed,
        "出せる");
    RequireEqual(std::to_string(EntitiesUnderGroup(bench.Snapshot(), group).size()),
        std::string("0"), "出る");
    RequireEqual(std::to_string(bench.Snapshot().entities.size()), std::string("1"),
        "物そのものは消えない");
}

KACHA_V2_TEST_MAIN("group_tree_tests")
