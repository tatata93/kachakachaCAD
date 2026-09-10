// 形状ガイドの役割表を、選択から作る道と、保存した作り方から作り直す道(app/GuideTableBuild.h)。
//
// 開き直したときに、役割・向き・作り方が作ったときと同じに戻ることを押さえる。
// 断面だけを拾う作り直しだったので、外形U/外形V を持つ面が開き直すと消えていた。
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/document/Commands.h"

#include <string>

using kachakacha::v2::app::AppendSelectionToRow;
using kachakacha::v2::app::AutoSectionTable;
using kachakacha::v2::app::DefinitionFromGuideTable;
using kachakacha::v2::app::GuideSelectionOf;
using kachakacha::v2::app::GuideTableFromDefinition;
using kachakacha::v2::app::GuideTableInputIds;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::document::AddFeatureCommand;
using kachakacha::v2::document::Document;
using kachakacha::v2::domain::CreateWireDefinition;
using kachakacha::v2::domain::Entity;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::Feature;
using kachakacha::v2::domain::FeatureOutput;
using kachakacha::v2::domain::FeatureType;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::GuideSurfaceMethod;
using kachakacha::v2::modeling::GuideTable;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

struct Fixture {
    DeterministicIdGenerator ids{7};
    Document document{ids.NextTyped<kachakacha::v2::base::IdKind::Document>()};

    EntityId AddLine(Vector3 a, Vector3 b, const std::string& name)
    {
        Feature feature;
        feature.id = ids.NextTyped<kachakacha::v2::base::IdKind::Feature>();
        feature.type = FeatureType::CreateWire;
        feature.displayName = name;
        CreateWireDefinition definition;
        definition.segments.push_back(CurveSegment::MakeLine(a, b).Value());
        definition.segmentIds.push_back(ids.NextTyped<kachakacha::v2::base::IdKind::Segment>());
        feature.definition = definition;
        Entity entity;
        entity.id = ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
        entity.kind = EntityKind::Wire;
        entity.displayName = name;
        entity.createdBy = feature.id;
        feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});
        const auto result = document.Run(AddFeatureCommand(feature, {entity}, name));
        Require(result.committed, "線が入る: " + name);
        return entity.id;
    }

    [[nodiscard]] kachakacha::v2::modeling::SnapScene Scene()
    {
        return kachakacha::v2::app::BuildSceneFromDocument(document.Snapshot(), ids);
    }
};

} // namespace

KACHA_V2_TEST(guide_table_build, おまかせは2本でルールド3本でロフト)
{
    Fixture fixture;
    const auto a = fixture.AddLine({0, 0, 0}, {100, 0, 0}, "a");
    const auto b = fixture.AddLine({0, 50, 0}, {100, 50, 10}, "b");
    const auto c = fixture.AddLine({0, 100, 0}, {100, 100, 0}, "c");
    const auto scene = fixture.Scene();
    const auto two = AutoSectionTable(fixture.document, scene, {a, b});
    Require(two.diagnostics.empty(), "2本は通る");
    RequireEqual(std::to_string(two.sections), std::string("2"), "断面2");
    Require(two.table.method == GuideSurfaceMethod::RuledSections, "2本はルールド");
    const auto three = AutoSectionTable(fixture.document, scene, {a, b, c});
    RequireEqual(std::to_string(three.sections), std::string("3"), "断面3");
    Require(three.table.method == GuideSurfaceMethod::LoftSections, "3本はロフト");
}

KACHA_V2_TEST(guide_table_build, 役割と向きと作り方が作り直しで戻る)
{
    Fixture fixture;
    const auto u1 = fixture.AddLine({0, 0, 0}, {100, 0, 0}, "u1");
    const auto u2 = fixture.AddLine({0, 100, 0}, {100, 100, 0}, "u2");
    const auto s1 = fixture.AddLine({0, 0, 0}, {0, 100, 0}, "s1");
    const auto s2 = fixture.AddLine({100, 0, 0}, {100, 100, 0}, "s2");
    const auto scene = fixture.Scene();
    GuideTable table;
    table.method = GuideSurfaceMethod::GuidedLoft;
    const auto add = [&](ChainRole role, EntityId id) {
        const auto chosen = GuideSelectionOf(fixture.document, scene, id);
        Require(chosen.has_value(), "線が拾える");
        const auto added = kachakacha::v2::modeling::AddSelectionAsNewRow(table, role, *chosen);
        Require(added.HasValue(), "行が足せる");
        table = added.Value();
    };
    add(ChainRole::GuideU, u1);
    add(ChainRole::GuideU, u2);
    add(ChainRole::Section, s1);
    add(ChainRole::Section, s2);
    const auto reversed = kachakacha::v2::modeling::ReverseRow(table, 1);
    Require(reversed.HasValue(), "2行目を逆にできる");
    table = reversed.Value();

    const auto definition = DefinitionFromGuideTable(table);
    RequireEqual(std::to_string(definition.roles.size()), std::string("4"), "役割4つ");
    RequireEqual(std::to_string(definition.method),
        std::to_string(static_cast<int>(GuideSurfaceMethod::GuidedLoft)), "作り方");

    const auto rebuilt = GuideTableFromDefinition(fixture.document, scene, definition);
    Require(rebuilt.HasValue(), "作り直せる");
    const GuideTable& again = rebuilt.Value();
    Require(again.method == GuideSurfaceMethod::GuidedLoft, "作り方が戻る");
    RequireEqual(std::to_string(again.rows.size()), std::string("4"), "行数が戻る");
    Require(again.rows[0].role == ChainRole::GuideU, "1行目は外形U");
    Require(again.rows[2].role == ChainRole::Section, "3行目は断面");
    Require(again.rows[1].reversed, "2行目は逆のまま");
    Require(!again.rows[0].reversed, "1行目は正のまま");
    // 逆にした行は、線の向きも逆で戻る(始点が x=100 側)。
    Require(again.rows[1].segments.front().StartPoint().x > 99.0, "逆向きの線で戻る");
    Require(GuideTableInputIds(again).size() == 4, "入力の id が4つ");
}

KACHA_V2_TEST(guide_table_build, 元の線が消えていれば理由を出して断る)
{
    Fixture fixture;
    const auto a = fixture.AddLine({0, 0, 0}, {100, 0, 0}, "a");
    const auto b = fixture.AddLine({0, 50, 0}, {100, 50, 0}, "b");
    const auto scene = fixture.Scene();
    auto definition = DefinitionFromGuideTable(AutoSectionTable(fixture.document, scene,
        {a, b}).table);
    definition.chains[1].segments.front().entityId =
        fixture.ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
    const auto rebuilt = GuideTableFromDefinition(fixture.document, scene, definition);
    Require(!rebuilt.HasValue(), "消えた線では作り直せない");
    RequireEqual(rebuilt.Diagnostics().front().code, std::string("GEO-R002"), "理由の番号");
}

KACHA_V2_TEST(guide_table_build, 既存行への追加は逆向きの線も受ける)
{
    Fixture fixture;
    const auto a = fixture.AddLine({0, 0, 0}, {100, 0, 0}, "a");
    // b は a の終点から離れる向きではなく、終点へ向かう向きで描かれている。
    const auto b = fixture.AddLine({100, 100, 0}, {100, 0, 0}, "b");
    const auto scene = fixture.Scene();
    GuideTable table;
    table.method = GuideSurfaceMethod::GuidedLoft;
    const auto first = kachakacha::v2::modeling::AddSelectionAsNewRow(table, ChainRole::GuideU,
        *GuideSelectionOf(fixture.document, scene, a));
    table = first.Value();
    const auto& tolerance = fixture.document.Snapshot().settings.tolerance;
    const auto appended = AppendSelectionToRow(table, 0,
        *GuideSelectionOf(fixture.document, scene, b), tolerance);
    Require(appended.HasValue(), "逆向きでもつながる");
    RequireEqual(std::to_string(appended.Value().rows[0].segments.size()), std::string("2"),
        "線が2本になる");
    Require(appended.Value().rows[0].segments.back().EndPoint().y > 99.0, "終点が続く側");
}

KACHA_V2_TEST(guide_table_build, 離した面は元の面の行と距離を持つ)
{
    Fixture fixture;
    GuideTable table;
    table.method = GuideSurfaceMethod::OffsetGuide;
    table.offsetDistanceMm = 2.5;
    const EntityId surface = fixture.ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
    const auto added = kachakacha::v2::modeling::AddSourceSurfaceRow(table, surface, "面");
    Require(added.HasValue(), "元の面の行が入る");
    table = added.Value();
    const auto twice = kachakacha::v2::modeling::AddSourceSurfaceRow(table, surface, "面");
    Require(!twice.HasValue(), "2枚目は断る");
    RequireEqual(twice.Diagnostics().front().code, std::string("UI-R010"), "理由の番号");
    const auto request = kachakacha::v2::modeling::ToGuideSurfaceRequest(table,
        fixture.document.Snapshot().settings.tolerance);
    Require(request.HasValue(), "要求になる");
    Require(request.Value().offsetDistanceMm == 2.5, "距離が渡る");
    Require(request.Value().chains.front().role == ChainRole::SourceSurface, "役割が渡る");
    const auto definition = DefinitionFromGuideTable(table);
    Require(definition.offsetDistanceMm == 2.5, "距離が保存の形へ写る");
}

KACHA_V2_TEST_MAIN("guide_table_build")
