// 作業中グループ(AT-UIX-006)。
//
// 決まりは2つ。
//   1. これから作る「利用者が作ったもの」は、作業中グループへ入る。
//   2. 派生物(部品・形状ガイド・型紙)は、Feature が指す派生グループへ入る。
//      作業中グループを切り替えても、派生物の居場所は動かない。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/io/DocumentFile.h"

#include <algorithm>
#include <string>

using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::GroupId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::document::AddFeatureCommand;
using kachakacha::v2::document::AddGroupCommand;
using kachakacha::v2::document::Document;
using kachakacha::v2::document::DocumentSnapshot;
using kachakacha::v2::document::Group;
using kachakacha::v2::document::MoveEntitiesToGroupCommand;
using kachakacha::v2::document::SetActiveGroupCommand;
using kachakacha::v2::domain::Entity;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::Feature;
using kachakacha::v2::domain::FeatureOutput;
using kachakacha::v2::domain::FeatureType;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

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
    DeterministicIdGenerator ids{21};

    [[nodiscard]] Made Make(EntityKind kind, const std::string& name,
        std::optional<GroupId> derivedGroup = std::nullopt)
    {
        Made made;
        made.feature.id = ids.NextTyped<IdKind::Feature>();
        made.feature.type = FeatureType::CreatePoint;
        made.feature.displayName = name;
        kachakacha::v2::domain::CreatePointDefinition definition;
        definition.positionMm = kachakacha::v2::geometry::Vector3{1.0, 2.0, 3.0};
        made.feature.definition = definition;
        made.feature.derivedGroupId = derivedGroup;
        made.entity.id = ids.NextTyped<IdKind::Entity>();
        made.entity.kind = kind;
        made.entity.displayName = name;
        made.entity.createdBy = made.feature.id;
        made.feature.outputs.push_back(FeatureOutput{"out", made.entity.id, kind});
        return made;
    }

    [[nodiscard]] Group MakeGroup(const std::string& name,
        std::optional<GroupId> parent = std::nullopt)
    {
        Group group;
        group.id = ids.NextTyped<IdKind::Group>();
        group.displayName = name;
        group.parentId = parent;
        return group;
    }
};

//! その Entity が入っているグループ。
[[nodiscard]] std::string GroupNameOf(const Document& document, EntityId id)
{
    const Entity* entity = document.FindEntity(id);
    if (entity == nullptr || !entity->groupId.has_value()) {
        return "(なし)";
    }
    for (const Group& group : document.Snapshot().groups) {
        if (group.id == *entity->groupId) {
            return group.displayName;
        }
    }
    return "(不明)";
}

//! 利用者が作る5種類。契約が名指ししているものである。
const EntityKind kUserKinds[] = {EntityKind::Point, EntityKind::Wire,
    EntityKind::WorkPlane, EntityKind::GuideSurface, EntityKind::Part};

} // namespace

KACHA_V2_TEST(active_group, 切り替えたあとに作ったものは全部そこへ入る)
{
    Maker maker;
    Document document(maker.ids.NextTyped<IdKind::Document>());
    const Group bodyGroup = maker.MakeGroup("車体");
    (void)document.Run(AddGroupCommand(bodyGroup));
    const auto set = document.Run(SetActiveGroupCommand(bodyGroup.id));
    Require(set.committed, "決められた");
    RequireEqual(document.Snapshot().settings.activeGroupId->ToString(),
        bodyGroup.id.ToString(), "作業中グループが変わる");

    for (EntityKind kind : kUserKinds) {
        const Made made = maker.Make(kind, std::string(EntityKindNameJa(kind)));
        const auto added = document.Run(
            AddFeatureCommand(made.feature, {made.entity}, "作る"));
        Require(added.committed, "作れた");
        RequireEqual(GroupNameOf(document, made.entity.id), "車体",
            std::string(EntityKindName(kind)) + " が車体へ入る");
    }
}

KACHA_V2_TEST(active_group, 切り替える前に作ったものは動かない)
{
    Maker maker;
    Document document(maker.ids.NextTyped<IdKind::Document>());
    const Group bodyGroup = maker.MakeGroup("車体");
    const Group roofGroup = maker.MakeGroup("屋根");
    (void)document.Run(AddGroupCommand(bodyGroup));
    (void)document.Run(AddGroupCommand(roofGroup));
    (void)document.Run(SetActiveGroupCommand(bodyGroup.id));
    const Made first = maker.Make(EntityKind::Wire, "先に作った線");
    (void)document.Run(AddFeatureCommand(first.feature, {first.entity}, "作る"));

    (void)document.Run(SetActiveGroupCommand(roofGroup.id));
    const Made second = maker.Make(EntityKind::Wire, "あとで作った線");
    (void)document.Run(AddFeatureCommand(second.feature, {second.entity}, "作る"));

    RequireEqual(GroupNameOf(document, first.entity.id), "車体", "先のは車体のまま");
    RequireEqual(GroupNameOf(document, second.entity.id), "屋根", "あとのは屋根");
}

KACHA_V2_TEST(active_group, 派生物は派生グループへ入り作業中グループを使わない)
{
    Maker maker;
    Document document(maker.ids.NextTyped<IdKind::Document>());
    const Group bodyGroup = maker.MakeGroup("車体");
    const Group derivedGroup = maker.MakeGroup("派生", bodyGroup.id);
    (void)document.Run(AddGroupCommand(bodyGroup));
    (void)document.Run(AddGroupCommand(derivedGroup));
    (void)document.Run(SetActiveGroupCommand(bodyGroup.id));

    const Made part = maker.Make(EntityKind::Part, "部品", derivedGroup.id);
    (void)document.Run(AddFeatureCommand(part.feature, {part.entity}, "部品を作る"));
    RequireEqual(GroupNameOf(document, part.entity.id), "派生", "派生グループへ入る");

    // 作業中グループを切り替えても、派生物は動かない。
    const Group roofGroup = maker.MakeGroup("屋根");
    (void)document.Run(AddGroupCommand(roofGroup));
    (void)document.Run(SetActiveGroupCommand(roofGroup.id));
    RequireEqual(GroupNameOf(document, part.entity.id), "派生", "動かない");

    const Made pattern = maker.Make(EntityKind::Pattern, "型紙", derivedGroup.id);
    (void)document.Run(AddFeatureCommand(pattern.feature, {pattern.entity}, "型紙"));
    RequireEqual(GroupNameOf(document, pattern.entity.id), "派生",
        "型紙も派生グループへ");
}

KACHA_V2_TEST(active_group, 作業中グループを外せる)
{
    Maker maker;
    Document document(maker.ids.NextTyped<IdKind::Document>());
    const Group bodyGroup = maker.MakeGroup("車体");
    (void)document.Run(AddGroupCommand(bodyGroup));
    (void)document.Run(SetActiveGroupCommand(bodyGroup.id));
    const auto cleared = document.Run(SetActiveGroupCommand(std::nullopt));
    Require(cleared.committed, "外せた");
    Require(!document.Snapshot().settings.activeGroupId.has_value(), "空になる");

    const Made made = maker.Make(EntityKind::Point, "点");
    (void)document.Run(AddFeatureCommand(made.feature, {made.entity}, "作る"));
    RequireEqual(GroupNameOf(document, made.entity.id), "(なし)", "どこにも入らない");
}

KACHA_V2_TEST(active_group, 無いグループは作業中にできない)
{
    Maker maker;
    Document document(maker.ids.NextTyped<IdKind::Document>());
    const GroupId missing = maker.ids.NextTyped<IdKind::Group>();
    const std::uint64_t before = document.Revision();
    const auto refused = document.Run(SetActiveGroupCommand(missing));
    Require(!refused.committed, "断る");
    RequireEqual(FirstCode(refused.diagnostics), "DOC-C001", "見つからない");
    RequireEqual(std::to_string(document.Revision()), std::to_string(before),
        "文書が変わらない");
}

KACHA_V2_TEST(active_group, あとから別のグループへ移せる)
{
    Maker maker;
    Document document(maker.ids.NextTyped<IdKind::Document>());
    const Group bodyGroup = maker.MakeGroup("車体");
    const Group roofGroup = maker.MakeGroup("屋根");
    (void)document.Run(AddGroupCommand(bodyGroup));
    (void)document.Run(AddGroupCommand(roofGroup));
    (void)document.Run(SetActiveGroupCommand(bodyGroup.id));
    const Made made = maker.Make(EntityKind::Wire, "線");
    (void)document.Run(AddFeatureCommand(made.feature, {made.entity}, "作る"));
    const auto moved = document.Run(
        MoveEntitiesToGroupCommand({made.entity.id}, roofGroup.id));
    Require(moved.committed, "移せた");
    RequireEqual(GroupNameOf(document, made.entity.id), "屋根", "屋根へ移る");
}

KACHA_V2_TEST(active_group, 元に戻せば作業中グループも戻る)
{
    Maker maker;
    Document document(maker.ids.NextTyped<IdKind::Document>());
    const Group bodyGroup = maker.MakeGroup("車体");
    const Group roofGroup = maker.MakeGroup("屋根");
    (void)document.Run(AddGroupCommand(bodyGroup));
    (void)document.Run(AddGroupCommand(roofGroup));
    (void)document.Run(SetActiveGroupCommand(bodyGroup.id));
    (void)document.Run(SetActiveGroupCommand(roofGroup.id));
    Require(document.Undo(), "戻せる");
    RequireEqual(document.Snapshot().settings.activeGroupId->ToString(),
        bodyGroup.id.ToString(), "車体へ戻る");
}

KACHA_V2_TEST(active_group, 保存して読み直しても保たれる)
{
    Maker maker;
    Document document(maker.ids.NextTyped<IdKind::Document>());
    const Group bodyGroup = maker.MakeGroup("車体");
    const Group derivedGroup = maker.MakeGroup("派生", bodyGroup.id);
    (void)document.Run(AddGroupCommand(bodyGroup));
    (void)document.Run(AddGroupCommand(derivedGroup));
    (void)document.Run(SetActiveGroupCommand(bodyGroup.id));
    std::vector<EntityId> userMade;
    for (EntityKind kind : kUserKinds) {
        const Made made = maker.Make(kind, std::string(EntityKindNameJa(kind)));
        (void)document.Run(AddFeatureCommand(made.feature, {made.entity}, "作る"));
        userMade.push_back(made.entity.id);
    }
    const Made part = maker.Make(EntityKind::Part, "派生部品", derivedGroup.id);
    (void)document.Run(AddFeatureCommand(part.feature, {part.entity}, "部品"));

    kachakacha::v2::io::DocumentFile file;
    file.snapshot = document.Snapshot();
    const auto saved = kachakacha::v2::io::SaveDocument(file);
    Require(saved.HasValue(), "保存できる");
    const auto loaded = kachakacha::v2::io::LoadDocument(saved.Value());
    if (!loaded.HasValue()) {
        Require(false, "読める: " + FirstCode(loaded.Diagnostics()) + " "
            + (loaded.Diagnostics().empty() ? std::string()
                                            : loaded.Diagnostics().front().detailsJa));
    }

    const DocumentSnapshot& back = loaded.Value().snapshot;
    Require(back.settings.activeGroupId.has_value(), "作業中グループが残る");
    RequireEqual(back.settings.activeGroupId->ToString(), bodyGroup.id.ToString(), "車体");

    // 読み直した断面をそのまま見る。
    for (const EntityId& id : userMade) {
        const auto found = std::find_if(back.entities.begin(), back.entities.end(),
            [&](const Entity& entity) { return entity.id == id; });
        Require(found != back.entities.end(), "残っている");
        Require(found->groupId.has_value() && *found->groupId == bodyGroup.id,
            "車体に入ったまま");
    }
    const auto foundPart = std::find_if(back.entities.begin(), back.entities.end(),
        [&](const Entity& entity) { return entity.id == part.entity.id; });
    Require(foundPart != back.entities.end(), "部品が残っている");
    Require(foundPart->groupId.has_value() && *foundPart->groupId == derivedGroup.id,
        "派生に入ったまま");
    // Feature の派生グループの指定も残る。
    const auto foundFeature = std::find_if(back.features.begin(), back.features.end(),
        [&](const Feature& feature) { return feature.id == part.feature.id; });
    Require(foundFeature != back.features.end(), "操作が残っている");
    Require(foundFeature->derivedGroupId.has_value()
            && *foundFeature->derivedGroupId == derivedGroup.id,
        "派生グループの指定も残る");
}

KACHA_V2_TEST_MAIN("active_group_tests")
