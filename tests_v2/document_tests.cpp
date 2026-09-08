// WP-03 の受入。Document / Feature DAG / Command / Undo。
// 大事なのは「失敗したら文書が一切変わらない」ことと「壊れた参照を黙って直さない」こと。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/document/Document.h"

#include <string>

using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::FeatureId;
using kachakacha::v2::base::GroupId;
using kachakacha::v2::base::IdKind;
using kachakacha::v2::document::AddFeatureCommand;
using kachakacha::v2::document::AddGroupCommand;
using kachakacha::v2::document::Document;
using kachakacha::v2::document::DocumentSnapshot;
using kachakacha::v2::document::Group;
using kachakacha::v2::document::MoveEntitiesToGroupCommand;
using kachakacha::v2::document::RemoveFeatureCommand;
using kachakacha::v2::document::RemoveGroupCommand;
using kachakacha::v2::document::RemovePolicy;
using kachakacha::v2::document::RenameEntityCommand;
using kachakacha::v2::document::SetFeatureEnabledCommand;
using kachakacha::v2::document::SetVisibilityCommand;
using kachakacha::v2::document::UpdateFeatureDefinitionCommand;
using kachakacha::v2::domain::CreatePointDefinition;
using kachakacha::v2::domain::Entity;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::Feature;
using kachakacha::v2::domain::FeatureOutput;
using kachakacha::v2::domain::FeatureType;
using kachakacha::v2::domain::Visibility;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

//! 点を1つ作るFeatureと、その出力Entityを組にして返す試験用の道具。
struct Maker {
    DeterministicIdGenerator ids{1};

    struct Made {
        Feature feature;
        Entity entity;
    };

    [[nodiscard]] Made MakePoint(const std::string& name,
        std::vector<EntityId> inputs = {})
    {
        Made made;
        made.feature.id = ids.NextTyped<IdKind::Feature>();
        made.feature.type = FeatureType::CreatePoint;
        made.feature.displayName = name;
        made.feature.inputEntityIds = std::move(inputs);
        made.entity.id = ids.NextTyped<IdKind::Entity>();
        made.entity.kind = EntityKind::Point;
        made.entity.displayName = name;
        made.entity.createdBy = made.feature.id;
        made.feature.outputs.push_back(
            FeatureOutput{"point", made.entity.id, EntityKind::Point});
        return made;
    }
};

[[nodiscard]] Document NewDocument(DeterministicIdGenerator& ids)
{
    return Document(ids.NextTyped<IdKind::Document>());
}

} // namespace

// ---- 追加と検索 ----

KACHA_V2_TEST(document, adding_a_feature_adds_its_output_entity)
{
    DeterministicIdGenerator ids(100);
    Document document = NewDocument(ids);
    Maker maker;
    const auto made = maker.MakePoint("作図点1");
    const auto result = document.Run(
        AddFeatureCommand(made.feature, {made.entity}, "作図点を置く"));
    Require(result.committed, "the command commits");
    Require(document.FindEntity(made.entity.id) != nullptr, "the entity is there");
    Require(document.FindFeature(made.feature.id) != nullptr, "the feature is there");
    RequireEqual(document.FindEntity(made.entity.id)->displayName, "作図点1",
        "the display name is kept");
    Require(document.Revision() > 1, "the revision moved forward");
}

KACHA_V2_TEST(document, a_duplicate_id_is_refused_and_nothing_changes)
{
    DeterministicIdGenerator ids(101);
    Document document = NewDocument(ids);
    Maker maker;
    const auto made = maker.MakePoint("作図点1");
    (void)document.Run(AddFeatureCommand(made.feature, {made.entity}, "1回目"));
    const std::uint64_t before = document.Revision();

    const auto again = document.Run(AddFeatureCommand(made.feature, {made.entity}, "2回目"));
    Require(!again.committed, "adding the same ids twice is refused");
    Require(document.Revision() == before, "a refused command does not move the revision");
    Require(document.Snapshot().entities.size() == 1, "no half-applied entity is left");
    Require(document.Snapshot().features.size() == 1, "no half-applied feature is left");
}

KACHA_V2_TEST(document, a_missing_input_is_refused)
{
    DeterministicIdGenerator ids(102);
    Document document = NewDocument(ids);
    Maker maker;
    DeterministicIdGenerator strangers(999);
    const EntityId nowhere = strangers.NextTyped<IdKind::Entity>();
    const auto made = maker.MakePoint("ぶら下がり", {nowhere});
    const auto result = document.Run(AddFeatureCommand(made.feature, {made.entity}, "足す"));
    Require(!result.committed, "a feature whose input does not exist is refused");
    Require(document.Snapshot().entities.empty(), "nothing was added");
    Require(!result.diagnostics.empty(), "the refusal carries a diagnostic");
}

// ---- 依存と削除 ----

KACHA_V2_TEST(document, downstream_features_are_found)
{
    DeterministicIdGenerator ids(103);
    Document document = NewDocument(ids);
    Maker maker;
    const auto base = maker.MakePoint("元");
    (void)document.Run(AddFeatureCommand(base.feature, {base.entity}, "元"));
    const auto child = maker.MakePoint("子", {base.entity.id});
    (void)document.Run(AddFeatureCommand(child.feature, {child.entity}, "子"));
    const auto grandchild = maker.MakePoint("孫", {child.entity.id});
    (void)document.Run(AddFeatureCommand(grandchild.feature, {grandchild.entity}, "孫"));

    const auto downstream = document.DownstreamFeatures(base.entity.id);
    Require(downstream.size() == 2, "both the child and the grandchild are downstream");
}

KACHA_V2_TEST(document, removing_something_in_use_is_refused_by_default)
{
    DeterministicIdGenerator ids(104);
    Document document = NewDocument(ids);
    Maker maker;
    const auto base = maker.MakePoint("元");
    (void)document.Run(AddFeatureCommand(base.feature, {base.entity}, "元"));
    const auto child = maker.MakePoint("子", {base.entity.id});
    (void)document.Run(AddFeatureCommand(child.feature, {child.entity}, "子"));

    const auto refused = document.Run(
        RemoveFeatureCommand(base.feature.id, RemovePolicy::RefuseIfUsed, "消す"));
    Require(!refused.committed, "removing something still in use is refused");
    Require(document.Snapshot().entities.size() == 2, "nothing was removed");
    Require(refused.diagnostics.front().detailsJa.find("子") != std::string::npos,
        "the message names what is still using it");
}

KACHA_V2_TEST(document, cascade_removal_takes_the_downstream_with_it)
{
    DeterministicIdGenerator ids(105);
    Document document = NewDocument(ids);
    Maker maker;
    const auto base = maker.MakePoint("元");
    (void)document.Run(AddFeatureCommand(base.feature, {base.entity}, "元"));
    const auto child = maker.MakePoint("子", {base.entity.id});
    (void)document.Run(AddFeatureCommand(child.feature, {child.entity}, "子"));

    const auto removed = document.Run(
        RemoveFeatureCommand(base.feature.id, RemovePolicy::Cascade, "まとめて消す"));
    Require(removed.committed, "cascade removal commits");
    Require(document.Snapshot().entities.empty(), "both entities are gone");
    Require(document.Snapshot().features.empty(), "both features are gone");
    Require(!removed.diagnostics.empty(), "it warns that it took the downstream too");
}

// ---- Undo / Redo ----

KACHA_V2_TEST(document, undo_and_redo_restore_exactly)
{
    DeterministicIdGenerator ids(106);
    Document document = NewDocument(ids);
    Maker maker;
    const auto first = maker.MakePoint("1つ目");
    const auto second = maker.MakePoint("2つ目");
    (void)document.Run(AddFeatureCommand(first.feature, {first.entity}, "1つ目を置く"));
    (void)document.Run(AddFeatureCommand(second.feature, {second.entity}, "2つ目を置く"));
    Require(document.Snapshot().entities.size() == 2, "two entities exist");
    RequireEqual(document.UndoLabel(), "2つ目を置く", "the undo label names the last action");

    Require(document.Undo(), "undo works");
    Require(document.Snapshot().entities.size() == 1, "one entity is left");
    Require(document.Undo(), "undo again");
    Require(document.Snapshot().entities.empty(), "the document is empty again");
    Require(!document.CanUndo(), "there is nothing left to undo");

    Require(document.Redo(), "redo works");
    Require(document.Snapshot().entities.size() == 1, "the first is back");
    Require(document.Redo(), "redo again");
    Require(document.Snapshot().entities.size() == 2, "both are back");
    RequireEqual(document.Snapshot().entities.back().displayName, "2つ目",
        "the restored entity is identical");
}

KACHA_V2_TEST(document, a_new_action_clears_the_redo_stack)
{
    DeterministicIdGenerator ids(107);
    Document document = NewDocument(ids);
    Maker maker;
    const auto first = maker.MakePoint("1つ目");
    (void)document.Run(AddFeatureCommand(first.feature, {first.entity}, "1つ目"));
    Require(document.Undo(), "undo");
    Require(document.CanRedo(), "redo is available");
    const auto second = maker.MakePoint("別の物");
    (void)document.Run(AddFeatureCommand(second.feature, {second.entity}, "別の物"));
    Require(!document.CanRedo(), "a new action drops the redo history");
}

KACHA_V2_TEST(document, a_refused_command_does_not_touch_the_undo_history)
{
    DeterministicIdGenerator ids(108);
    Document document = NewDocument(ids);
    Maker maker;
    const auto made = maker.MakePoint("作図点1");
    (void)document.Run(AddFeatureCommand(made.feature, {made.entity}, "置く"));
    (void)document.Run(AddFeatureCommand(made.feature, {made.entity}, "二重に置く"));
    Require(document.Undo(), "one undo returns to empty");
    Require(document.Snapshot().entities.empty(),
        "the refused command left no entry in the history");
}

KACHA_V2_TEST(document, a_drag_becomes_a_single_undo_step)
{
    DeterministicIdGenerator ids(109);
    Document document = NewDocument(ids);
    Maker maker;
    document.BeginCompound("ドラッグで動かす");
    for (int index = 0; index < 5; ++index) {
        const auto made = maker.MakePoint("途中" + std::to_string(index));
        (void)document.Run(AddFeatureCommand(made.feature, {made.entity}, "途中"));
    }
    document.EndCompound();
    Require(document.Snapshot().entities.size() == 5, "all five steps applied");
    RequireEqual(document.UndoLabel(), "ドラッグで動かす",
        "the compound carries the drag label");
    Require(document.Undo(), "one undo");
    Require(document.Snapshot().entities.empty(),
        "a single undo removes the whole drag, not one step of it");
}

KACHA_V2_TEST(document, opening_a_file_is_a_history_boundary)
{
    DeterministicIdGenerator ids(110);
    Document document = NewDocument(ids);
    Maker maker;
    const auto made = maker.MakePoint("作図点1");
    (void)document.Run(AddFeatureCommand(made.feature, {made.entity}, "置く"));
    Require(document.CanUndo(), "there is history");
    document.MarkHistoryBoundary();
    Require(!document.CanUndo(), "opening a file clears the history");
    Require(!document.CanRedo(), "and the redo side too");
}

// ---- 名前・表示・有効 ----

KACHA_V2_TEST(document, names_are_display_only_and_may_repeat)
{
    DeterministicIdGenerator ids(111);
    Document document = NewDocument(ids);
    Maker maker;
    const auto first = maker.MakePoint("前面");
    const auto second = maker.MakePoint("別の名前");
    (void)document.Run(AddFeatureCommand(first.feature, {first.entity}, "1"));
    (void)document.Run(AddFeatureCommand(second.feature, {second.entity}, "2"));

    const auto renamed = document.Run(RenameEntityCommand(second.entity.id, "前面"));
    Require(renamed.committed, "two objects may share a display name");
    RequireEqual(document.FindEntity(second.entity.id)->displayName, "前面",
        "the rename applied");
    Require(document.FindEntity(first.entity.id)->id != document.FindEntity(second.entity.id)->id,
        "they are still different objects");

    Require(!document.Run(RenameEntityCommand(second.entity.id, "")).committed,
        "an empty name is refused");
}

KACHA_V2_TEST(document, visibility_has_three_states)
{
    DeterministicIdGenerator ids(112);
    Document document = NewDocument(ids);
    Maker maker;
    const auto made = maker.MakePoint("作図点1");
    (void)document.Run(AddFeatureCommand(made.feature, {made.entity}, "置く"));
    for (const Visibility state :
        {Visibility::Hidden, Visibility::Reference, Visibility::Visible}) {
        Require(document.Run(SetVisibilityCommand({made.entity.id}, state)).committed,
            "the visibility change commits");
        Require(document.FindEntity(made.entity.id)->visibility == state,
            "the state is stored");
    }
}

KACHA_V2_TEST(document, a_feature_can_be_disabled_without_deleting_it)
{
    DeterministicIdGenerator ids(113);
    Document document = NewDocument(ids);
    Maker maker;
    const auto made = maker.MakePoint("作図点1");
    (void)document.Run(AddFeatureCommand(made.feature, {made.entity}, "置く"));
    Require(document.Run(SetFeatureEnabledCommand(made.feature.id, false)).committed,
        "disabling commits");
    Require(!document.FindFeature(made.feature.id)->enabled, "it is disabled");
    Require(document.FindFeature(made.feature.id) != nullptr, "but it still exists");
}

KACHA_V2_TEST(document, editing_a_definition_keeps_the_output_id)
{
    DeterministicIdGenerator ids(114);
    Document document = NewDocument(ids);
    Maker maker;
    const auto made = maker.MakePoint("作図点1");
    (void)document.Run(AddFeatureCommand(made.feature, {made.entity}, "置く"));
    CreatePointDefinition definition;
    definition.positionMm = {10, 20, 30};
    const auto updated = document.Run(UpdateFeatureDefinitionCommand(made.feature.id,
        definition, {}, "位置を変える"));
    Require(updated.committed, "the definition update commits");
    RequireEqual(document.FindFeature(made.feature.id)->outputs.front().entityId.ToString(),
        made.entity.id.ToString(),
        "the output keeps its id so downstream references stay valid");
}

// ---- グループ ----

KACHA_V2_TEST(document, removing_a_group_moves_its_contents_to_the_parent)
{
    DeterministicIdGenerator ids(115);
    Document document = NewDocument(ids);
    Maker maker;
    Group parent{ids.NextTyped<IdKind::Group>(), "親", std::nullopt};
    Group child{ids.NextTyped<IdKind::Group>(), "子", parent.id};
    Require(document.Run(AddGroupCommand(parent)).committed, "the parent group is added");
    Require(document.Run(AddGroupCommand(child)).committed, "the child group is added");

    const auto made = maker.MakePoint("作図点1");
    (void)document.Run(AddFeatureCommand(made.feature, {made.entity}, "置く"));
    Require(document.Run(MoveEntitiesToGroupCommand({made.entity.id}, child.id)).committed,
        "the entity moves into the child group");

    Require(document.Run(RemoveGroupCommand(child.id)).committed, "the child group is removed");
    Require(document.FindEntity(made.entity.id) != nullptr,
        "removing a group does not delete its contents");
    Require(document.FindEntity(made.entity.id)->groupId.has_value()
            && *document.FindEntity(made.entity.id)->groupId == parent.id,
        "the contents moved up to the parent group");
}

KACHA_V2_TEST(document, moving_into_a_group_that_does_not_exist_is_refused)
{
    DeterministicIdGenerator ids(116);
    Document document = NewDocument(ids);
    Maker maker;
    const auto made = maker.MakePoint("作図点1");
    (void)document.Run(AddFeatureCommand(made.feature, {made.entity}, "置く"));
    DeterministicIdGenerator strangers(888);
    const GroupId nowhere = strangers.NextTyped<IdKind::Group>();
    Require(!document.Run(MoveEntitiesToGroupCommand({made.entity.id}, nowhere)).committed,
        "moving into a missing group is refused");
    Require(!document.FindEntity(made.entity.id)->groupId.has_value(),
        "the entity was not touched");
}

// ---- 評価順 ----

KACHA_V2_TEST(document, the_evaluation_order_puts_inputs_before_outputs)
{
    DeterministicIdGenerator ids(117);
    Document document = NewDocument(ids);
    Maker maker;
    const auto base = maker.MakePoint("元");
    (void)document.Run(AddFeatureCommand(base.feature, {base.entity}, "元"));
    const auto child = maker.MakePoint("子", {base.entity.id});
    (void)document.Run(AddFeatureCommand(child.feature, {child.entity}, "子"));

    const auto& order = document.Snapshot().evaluationOrder;
    Require(order.size() == 2, "both features are in the order");
    RequireEqual(order.front().ToString(), base.feature.id.ToString(),
        "the upstream feature is evaluated first");
}

KACHA_V2_TEST(document, the_evaluation_order_is_the_same_every_time)
{
    // 同じ文書を2回組み立てて、評価順が一致すること(DOC-013 / DOC-014)。
    const auto build = [] {
        DeterministicIdGenerator ids(118);
        Document document = NewDocument(ids);
        Maker maker;
        const auto a = maker.MakePoint("A");
        const auto b = maker.MakePoint("B");
        const auto c = maker.MakePoint("C", {a.entity.id});
        (void)document.Run(AddFeatureCommand(a.feature, {a.entity}, "A"));
        (void)document.Run(AddFeatureCommand(b.feature, {b.entity}, "B"));
        (void)document.Run(AddFeatureCommand(c.feature, {c.entity}, "C"));
        std::string joined;
        for (const auto& id : document.Snapshot().evaluationOrder) {
            joined += id.ToString() + ";";
        }
        return joined;
    };
    RequireEqual(build(), build(), "the evaluation order is deterministic");
}

// ---- 検証 ----

KACHA_V2_TEST(document, validation_catches_a_cycle)
{
    // Commandを通さず、わざと循環した断面を作って検証だけを試す。
    DeterministicIdGenerator ids(119);
    DocumentSnapshot snapshot;
    snapshot.id = ids.NextTyped<IdKind::Document>();
    Maker maker;
    auto first = maker.MakePoint("A");
    auto second = maker.MakePoint("B");
    first.feature.inputEntityIds = {second.entity.id};
    second.feature.inputEntityIds = {first.entity.id};
    snapshot.features = {first.feature, second.feature};
    snapshot.entities = {first.entity, second.entity};

    const auto diagnostics = Document::Validate(snapshot);
    bool sawCycle = false;
    for (const auto& diagnostic : diagnostics) {
        sawCycle = sawCycle || diagnostic.code == "DOC-V004";
    }
    Require(sawCycle, "a cycle in the feature graph is reported");
}

KACHA_V2_TEST(document, validation_catches_an_orphan_entity)
{
    DeterministicIdGenerator ids(120);
    DocumentSnapshot snapshot;
    snapshot.id = ids.NextTyped<IdKind::Document>();
    Entity orphan;
    orphan.id = ids.NextTyped<IdKind::Entity>();
    orphan.displayName = "誰も作っていない";
    snapshot.entities = {orphan};
    const auto diagnostics = Document::Validate(snapshot);
    bool sawOrphan = false;
    for (const auto& diagnostic : diagnostics) {
        sawOrphan = sawOrphan || diagnostic.code == "DOC-V005";
    }
    Require(sawOrphan, "an entity with no creating feature is reported");
}

KACHA_V2_TEST(document, 補助線にできる)
{
    // V1の「補助線として作図」「補助線化」。幾何は変えない。
    Maker maker;
    Document document(kachakacha::v2::base::DocumentId{});
    Maker::Made wire = maker.MakePoint("線");
    wire.entity.kind = EntityKind::Wire;
    Require(document.Run(AddFeatureCommand(wire.feature, {wire.entity}, "線")).committed,
        "足せること");

    Require(document.Run(kachakacha::v2::document::SetConstructionCommand(
                             {wire.entity.id}, true))
                .committed,
        "補助線にできること");
    Require(document.FindEntity(wire.entity.id)->construction, "補助線であること");

    Require(document.Run(kachakacha::v2::document::SetConstructionCommand(
                             {wire.entity.id}, false))
                .committed,
        "戻せること");
    Require(!document.FindEntity(wire.entity.id)->construction, "戻ったこと");

    // Undo で戻せること。
    Require(document.Undo(), "取り消せること");
    Require(document.FindEntity(wire.entity.id)->construction, "補助線に戻ること");
}

KACHA_V2_TEST(document, 部品は補助線にできない)
{
    Maker maker;
    Document document(kachakacha::v2::base::DocumentId{});
    Maker::Made part = maker.MakePoint("部品");
    part.entity.kind = EntityKind::Part;
    Require(document.Run(AddFeatureCommand(part.feature, {part.entity}, "部品")).committed,
        "足せること");
    const auto result = document.Run(
        kachakacha::v2::document::SetConstructionCommand({part.entity.id}, true));
    Require(!result.committed, "断ること");
    RequireEqual(result.diagnostics.front().code, std::string("DOC-C005"), "診断コード");
}

KACHA_V2_TEST(document, 基準線にできる)
{
    // V1の「基準線に設定」「基準解除」。
    Maker maker;
    Document document(kachakacha::v2::base::DocumentId{});
    Maker::Made wire = maker.MakePoint("線");
    wire.entity.kind = EntityKind::Wire;
    Require(document.Run(AddFeatureCommand(wire.feature, {wire.entity}, "線")).committed,
        "足せること");
    Require(
        document.Run(kachakacha::v2::document::SetDatumCommand({wire.entity.id}, true))
            .committed,
        "基準にできること");
    Require(document.FindEntity(wire.entity.id)->datum, "基準であること");
    Require(
        document.Run(kachakacha::v2::document::SetDatumCommand({wire.entity.id}, false))
            .committed,
        "解除できること");
    Require(!document.FindEntity(wire.entity.id)->datum, "解除されたこと");
}

KACHA_V2_TEST(document, 無いものを補助線にしようとしたら断る)
{
    Document document(kachakacha::v2::base::DocumentId{});
    kachakacha::v2::base::DeterministicIdGenerator ids{99};
    const EntityId missing = ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
    const auto result =
        document.Run(kachakacha::v2::document::SetConstructionCommand({missing}, true));
    Require(!result.committed, "断ること");
    RequireEqual(result.diagnostics.front().code, std::string("DOC-C001"), "診断コード");
}

KACHA_V2_TEST_MAIN("document_tests")
