// 選んだものだけを別の文書にする(app/SubDocument.h、工程4)。
//
// 選んだものを作った Feature から上流をたどり、要るものだけを残す。
// 選んでいないが参照されているものは残して名前で知らせ、関係ないものは入れない。
#include "kachakacha/app/SubDocument.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/io/DocumentFile.h"

#include <string>

using kachakacha::v2::app::DefinitionEntityReferences;
using kachakacha::v2::app::ExtractSubDocument;
using kachakacha::v2::app::KeptDependenciesSummaryJa;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::document::AddFeatureCommand;
using kachakacha::v2::document::Document;
using kachakacha::v2::domain::Entity;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::Feature;
using kachakacha::v2::domain::FeatureOutput;
using kachakacha::v2::domain::FeatureType;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

struct Fixture {
    DeterministicIdGenerator ids{11};
    Document document{ids.NextTyped<kachakacha::v2::base::IdKind::Document>()};

    EntityId Add(FeatureType type, EntityKind kind, const std::string& name,
        kachakacha::v2::domain::FeatureDefinition definition,
        std::vector<EntityId> inputs)
    {
        Feature feature;
        feature.id = ids.NextTyped<kachakacha::v2::base::IdKind::Feature>();
        feature.type = type;
        feature.displayName = name;
        feature.definition = std::move(definition);
        feature.inputEntityIds = std::move(inputs);
        Entity entity;
        entity.id = ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
        entity.kind = kind;
        entity.displayName = name;
        entity.createdBy = feature.id;
        feature.outputs.push_back(FeatureOutput{"out", entity.id, kind});
        const auto result = document.Run(AddFeatureCommand(feature, {entity}, name));
        Require(result.committed, "入る: " + name);
        return entity.id;
    }

    EntityId AddLine(const std::string& name, std::optional<EntityId> plane = std::nullopt)
    {
        kachakacha::v2::domain::CreateWireDefinition definition;
        definition.segments.push_back(
            CurveSegment::MakeLine({0, 0, 0}, {10, 0, 0}).Value());
        definition.segmentIds.push_back(ids.NextTyped<kachakacha::v2::base::IdKind::Segment>());
        definition.sourcePlaneId = plane;
        // 作業平面は inputEntityIds に入れない。作り方の中身だけが指している。
        return Add(FeatureType::CreateWire, EntityKind::Wire, name, definition, {});
    }
};

//! 作業平面1、線 a・b(平面の上)、線 c(無関係)、a と b から面、面に厚みで部品。
struct World {
    Fixture fixture;
    EntityId plane;
    EntityId a;
    EntityId b;
    EntityId c;
    EntityId surface;
    EntityId part;

    World()
    {
        kachakacha::v2::domain::CreateWorkPlaneDefinition planeDefinition;
        plane = fixture.Add(FeatureType::CreateWorkPlane, EntityKind::WorkPlane, "平面",
            planeDefinition, {});
        a = fixture.AddLine("a", plane);
        b = fixture.AddLine("b", plane);
        c = fixture.AddLine("c");
        kachakacha::v2::domain::CreateGuideSurfaceDefinition guide;
        surface = fixture.Add(FeatureType::CreateGuideSurface, EntityKind::GuideSurface, "面",
            guide, {a, b});
        kachakacha::v2::domain::ThickenSurfaceDefinition thicken;
        thicken.surface = surface;
        part = fixture.Add(FeatureType::ThickenSurface, EntityKind::Part, "部品", thicken,
            {surface});
    }

    [[nodiscard]] bool Has(const kachakacha::v2::document::DocumentSnapshot& snapshot,
        const EntityId& id) const
    {
        for (const auto& entity : snapshot.entities) {
            if (entity.id == id) {
                return true;
            }
        }
        return false;
    }
};

} // namespace

KACHA_V2_TEST(sub_document, 部品を選ぶと上流だけが残り無関係な線は入らない)
{
    World world;
    const auto made = ExtractSubDocument(world.fixture.document.Snapshot(), {world.part});
    Require(made.HasValue(), "作れる");
    const auto& snapshot = made.Value().snapshot;
    Require(world.Has(snapshot, world.part), "部品がある");
    Require(world.Has(snapshot, world.surface), "面が残る(部品の元)");
    Require(world.Has(snapshot, world.a) && world.Has(snapshot, world.b), "線 a b が残る");
    Require(world.Has(snapshot, world.plane), "作業平面が残る(線の中身が指している)");
    Require(!world.Has(snapshot, world.c), "無関係な線 c は入らない");
    RequireEqual(std::to_string(snapshot.features.size()), std::string("5"), "Feature は5つ");
    RequireEqual(std::to_string(snapshot.evaluationOrder.size()), std::string("5"),
        "評価順も5つ");
    RequireEqual(std::to_string(made.Value().selectedCount), std::string("1"), "選んだ数");
}

KACHA_V2_TEST(sub_document, 残した依存は名前で知らせる)
{
    World world;
    const auto made = ExtractSubDocument(world.fixture.document.Snapshot(), {world.surface});
    Require(made.HasValue(), "作れる");
    const auto& kept = made.Value().keptDependencyNames;
    // a, b, 平面 の3つ。面そのものは選んだので入らない。
    RequireEqual(std::to_string(kept.size()), std::string("3"), "残した数");
    const std::string summary = KeptDependenciesSummaryJa(made.Value());
    Require(summary.find("平面") != std::string::npos, "作業平面の名前が出る");
    Require(summary.find("面") != std::string::npos, "一文になる");
    Require(!world.Has(made.Value().snapshot, world.part), "下流の部品は入らない");
}

KACHA_V2_TEST(sub_document, 出来た文書は保存して読み直せる)
{
    World world;
    const auto made = ExtractSubDocument(world.fixture.document.Snapshot(), {world.part});
    Require(made.HasValue(), "作れる");
    kachakacha::v2::io::DocumentFile file;
    file.snapshot = made.Value().snapshot;
    const std::string written = kachakacha::v2::io::WriteDocumentJson(file);
    Require(!written.empty(), "書ける");
    const auto read = kachakacha::v2::io::ReadDocumentJson(written);
    Require(read.HasValue(), "読める");
    RequireEqual(std::to_string(read.Value().snapshot.entities.size()),
        std::to_string(made.Value().snapshot.entities.size()), "同じ数が戻る");
    Require(Document::Validate(read.Value().snapshot).empty(), "壊れていない");
}

KACHA_V2_TEST(sub_document, 何も選んでいなければ断る)
{
    World world;
    const auto none = ExtractSubDocument(world.fixture.document.Snapshot(), {});
    Require(!none.HasValue(), "空は断る");
    RequireEqual(none.Diagnostics().front().code, std::string("EXP-S001"), "理由");
    const auto unknown = ExtractSubDocument(world.fixture.document.Snapshot(),
        {world.fixture.ids.NextTyped<kachakacha::v2::base::IdKind::Entity>()});
    Require(!unknown.HasValue(), "無いものは断る");
    RequireEqual(unknown.Diagnostics().front().code, std::string("EXP-S002"), "理由");
}

KACHA_V2_TEST(sub_document, 作り方の中身が指すものを全部拾う)
{
    // inputEntityIds に無い参照(押し出しの相手、近似の開口など)を落とすと、
    // 開き直したときに「無いものを指している」で作り直せない。
    DeterministicIdGenerator ids{3};
    kachakacha::v2::domain::ExtrudeDefinition extrude;
    extrude.profiles.push_back(ids.NextTyped<kachakacha::v2::base::IdKind::Entity>());
    extrude.targets.push_back(ids.NextTyped<kachakacha::v2::base::IdKind::Entity>());
    RequireEqual(std::to_string(DefinitionEntityReferences(extrude).size()), std::string("2"),
        "押し出しは輪郭と相手");
    kachakacha::v2::domain::CreateFabricationModelDefinition fabrication;
    fabrication.parts.push_back(ids.NextTyped<kachakacha::v2::base::IdKind::Entity>());
    fabrication.openingWires.push_back(ids.NextTyped<kachakacha::v2::base::IdKind::Entity>());
    fabrication.foldWires.push_back(ids.NextTyped<kachakacha::v2::base::IdKind::Entity>());
    fabrication.connectionWires.push_back(
        ids.NextTyped<kachakacha::v2::base::IdKind::Entity>());
    RequireEqual(std::to_string(DefinitionEntityReferences(fabrication).size()),
        std::string("4"), "近似は元と開口と折り線と接続");
    kachakacha::v2::domain::ProjectWireDefinition project;
    project.inputs.push_back(ids.NextTyped<kachakacha::v2::base::IdKind::Entity>());
    project.targetPlaneId = ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
    RequireEqual(std::to_string(DefinitionEntityReferences(project).size()), std::string("2"),
        "投影は線と相手の面");
}

KACHA_V2_TEST_MAIN("sub_document")
