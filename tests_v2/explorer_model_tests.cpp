// Model Explorer の節(app/ExplorerModel.h、正本 3 HTML 2026-09-18)。
#include "kachakacha/app/ExplorerModel.h"
#include "kachakacha/app/OriginPlanes.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/domain/Feature.h"

#include <string>

using kachakacha::v2::app::ExplorerSection;
using kachakacha::v2::app::ExplorerSectionNameJa;
using kachakacha::v2::app::ExplorerSections;
using kachakacha::v2::app::SectionForEntity;
using kachakacha::v2::domain::Entity;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

KACHA_V2_TEST(explorer, 原点が最上段で_節の並びは正本どおり)
{
    const auto& sections = ExplorerSections();
    Require(sections.size() == 9 && sections.front() == ExplorerSection::Origin, "原点が先頭");
    Require(ExplorerSectionNameJa(sections[2]) == "グループ", "「まとまり」ではなく「グループ」");
    Require(ExplorerSectionNameJa(sections.back()) == "生成物", "末尾は生成物");
}

KACHA_V2_TEST(explorer, 種類とグループと作られ方で節が決まる)
{
    kachakacha::v2::document::DocumentSnapshot snapshot;
    kachakacha::v2::base::DeterministicIdGenerator ids{1};
    Entity wire;
    wire.id = ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
    wire.kind = EntityKind::Wire;
    Require(SectionForEntity(snapshot, wire) == ExplorerSection::Wires, "線はワイヤー");
    Entity surface = wire;
    surface.kind = EntityKind::GuideSurface;
    Require(SectionForEntity(snapshot, surface) == ExplorerSection::Surfaces, "面は面");
    Entity part = wire;
    part.kind = EntityKind::Part;
    Require(SectionForEntity(snapshot, part) == ExplorerSection::Solids, "部品は立体");
    Entity model = wire;
    model.kind = EntityKind::FabricationModel;
    Require(SectionForEntity(snapshot, model) == ExplorerSection::Approximation, "近似モデルは近似");
    Entity plane = wire;
    plane.kind = EntityKind::WorkPlane;
    Require(SectionForEntity(snapshot, plane) == ExplorerSection::WorkPlanes, "作業面は作業面");
    // グループに入っていればグループの節(種類の節には出ない)。
    kachakacha::v2::document::Group group;
    group.id = ids.NextTyped<kachakacha::v2::base::IdKind::Group>();
    snapshot.groups.push_back(group);
    Entity grouped = wire;
    grouped.groupId = group.id;
    Require(SectionForEntity(snapshot, grouped) == ExplorerSection::Groups, "グループの中");
    // 固定で作ったものは生成物。
    kachakacha::v2::domain::Feature freeze;
    freeze.id = ids.NextTyped<kachakacha::v2::base::IdKind::Feature>();
    freeze.type = kachakacha::v2::domain::FeatureType::FreezeDerived;
    snapshot.features.push_back(freeze);
    Entity frozen = wire;
    frozen.createdBy = freeze.id;
    Require(SectionForEntity(snapshot, frozen) == ExplorerSection::Generated, "固定したものは生成物");
}

KACHA_V2_TEST(explorer, 生成物は生成元の近似モデルの下でグループが先で近似モデルが無ければ種類の節)
{
    kachakacha::v2::document::DocumentSnapshot snapshot;
    kachakacha::v2::base::DeterministicIdGenerator ids{7};
    Entity model;
    model.id = ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
    model.kind = EntityKind::FabricationModel;
    snapshot.entities.push_back(model);
    Entity wire;
    wire.id = ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
    wire.kind = EntityKind::Wire;
    wire.generatedFrom = model.id;
    Require(kachakacha::v2::app::GeneratingModelOf(snapshot, wire) == model.id, "生成元が分かる");
    Require(SectionForEntity(snapshot, wire) == ExplorerSection::Approximation,
        "近似の節(近似モデルの下)");
    // グループに入れたものはグループが先(ほかの物と同じ決まり)。
    kachakacha::v2::document::Group group;
    group.id = ids.NextTyped<kachakacha::v2::base::IdKind::Group>();
    snapshot.groups.push_back(group);
    Entity grouped = wire;
    grouped.groupId = group.id;
    Require(SectionForEntity(snapshot, grouped) == ExplorerSection::Groups, "グループの中");
    // 近似モデルがもう無ければ、ふつうの種類の節。
    Entity orphan = wire;
    orphan.generatedFrom = ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
    Require(!kachakacha::v2::app::GeneratingModelOf(snapshot, orphan).has_value(), "生成元は無い");
    Require(SectionForEntity(snapshot, orphan) == ExplorerSection::Wires, "ワイヤーの節");
    // 近似モデルでないものを指していても、生成物としては扱わない。
    Entity pointsAtWire = wire;
    pointsAtWire.generatedFrom = wire.id;
    snapshot.entities.push_back(wire);
    Require(!kachakacha::v2::app::GeneratingModelOf(snapshot, pointsAtWire).has_value(),
        "線は生成元にならない");
}

KACHA_V2_TEST(explorer_model, bulk_placement_preserves_section_precedence)
{
    using namespace kachakacha::v2;
    base::DeterministicIdGenerator ids;
    document::DocumentSnapshot snapshot;
    document::Group group;
    group.id = ids.NextTyped<base::IdKind::Group>();
    snapshot.groups.push_back(group);
    const auto append = [&](EntityKind kind) -> Entity& {
        Entity entity;
        entity.id = ids.NextTyped<base::IdKind::Entity>();
        entity.createdBy = ids.NextTyped<base::IdKind::Feature>();
        entity.kind = kind;
        snapshot.entities.push_back(entity);
        return snapshot.entities.back();
    };
    const auto model = append(EntityKind::FabricationModel).id;
    auto& plane = append(EntityKind::WorkPlane);
    domain::Feature origin;
    origin.id = plane.createdBy;
    origin.type = domain::FeatureType::CreateWorkPlane;
    domain::CreateWorkPlaneDefinition datum;
    datum.isOriginPlane = true;
    origin.definition = datum;
    snapshot.features.push_back(origin);
    append(EntityKind::Wire).generatedFrom = model;
    auto& grouped = append(EntityKind::Part);
    grouped.groupId = group.id;
    grouped.generatedFrom = model;
    auto& frozen = append(EntityKind::GuideSurface);
    domain::Feature freeze;
    freeze.id = frozen.createdBy;
    freeze.type = domain::FeatureType::FreezeDerived;
    snapshot.features.push_back(freeze);
    append(EntityKind::Image).groupId = group.id;
    append(EntityKind::Point).generatedFrom = ids.NextTyped<base::IdKind::Entity>();
    auto& self = append(EntityKind::FabricationModel);
    self.generatedFrom = self.id;

    const auto placements = app::BuildExplorerPlacements(snapshot);
    Require(placements.size() == snapshot.entities.size(), "one placement for each item");
    for (std::size_t i = 0; i < placements.size(); ++i) {
        const auto& entity = snapshot.entities[i];
        auto ungrouped = entity;
        ungrouped.groupId.reset();
        const auto section = app::SectionForEntity(snapshot, entity);
        Require(placements[i].section == section, "root section agrees with ordinary classification");
        Require(placements[i].groupSection == app::SectionForEntity(snapshot, ungrouped),
            "group section agrees when membership is removed");
        Require(placements[i].underGeneratingModel == (section == ExplorerSection::Approximation
            && app::GeneratingModelOf(snapshot, entity).has_value()), "the same items are nested under the source model");
    }
}

KACHA_V2_TEST(explorer_model, 同じ名前には番号を送る)
{
    using kachakacha::v2::app::UniqueDisplayName;
    kachakacha::v2::document::DocumentSnapshot snapshot;
    RequireEqual(UniqueDisplayName(snapshot, EntityKind::Part, "押し出し"), std::string("押し出し"),
        "最初は素の名前");
    kachakacha::v2::domain::Entity part;
    part.kind = EntityKind::Part;
    part.displayName = "押し出し";
    snapshot.entities.push_back(part);
    RequireEqual(UniqueDisplayName(snapshot, EntityKind::Part, "押し出し"), std::string("押し出し 2"),
        "2つ目は 2");
    part.displayName = "押し出し 2";
    snapshot.entities.push_back(part);
    RequireEqual(UniqueDisplayName(snapshot, EntityKind::Part, "押し出し"), std::string("押し出し 3"),
        "3つ目は 3");
    RequireEqual(UniqueDisplayName(snapshot, EntityKind::GuideSurface, "押し出し"),
        std::string("押し出し"), "種類が違えば数えない");
}

KACHA_V2_TEST_MAIN("explorer_model_tests")
