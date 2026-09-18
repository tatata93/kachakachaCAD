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
    Require(sections.size() == 8 && sections.front() == ExplorerSection::Origin, "原点が先頭");
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
