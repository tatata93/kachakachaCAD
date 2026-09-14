//! HO 流線形前頭部の見本を、**作り方** として文書に組み立てる。
//!
//! オーナー指示 2026-09-14 §14〜23。出来上がった形は書き込まない。
//! 作業平面 → 断面ワイヤー → 案内線 → 面 → 押し出し → 近似 の順で、
//! 利用者が実際にやるとおりの Feature を並べる。開いたら核が作り直す。
//!
//! まとまり(整理用フォルダ)の形は §10 の例に合わせる。

#include "kachakacha/app/RailwayNoseHoSample.h"

#include "kachakacha/domain/Feature.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kachakacha::v2::app {
namespace {

using base::DeterministicIdGenerator;
using base::EntityId;
using base::GroupId;
using base::IdKind;
using document::DocumentSnapshot;
using document::Group;
using domain::CreateFabricationModelDefinition;
using domain::CreateGuideSurfaceDefinition;
using domain::CreateWireDefinition;
using domain::CreateWorkPlaneDefinition;
using domain::EditPolicy;
using domain::Entity;
using domain::EntityKind;
using domain::ExtrudeDefinition;
using domain::Feature;
using domain::FeatureOutput;
using domain::FeatureType;
using domain::SegmentRef;
using domain::Visibility;
using domain::WireChainRef;
using geometry::CurveSegment;
using geometry::Vector3;

struct AddedWire {
    EntityId entityId;
    std::vector<base::SegmentId> segmentIds;
};

//! 見本を組み立てる道具。物を1つ作るたびに Feature と Entity を1組ずつ足す。
struct HoBuilder {
    DeterministicIdGenerator ids{8001};
    DocumentSnapshot snapshot;

    [[nodiscard]] GroupId AddGroup(const std::string& name,
        std::optional<GroupId> parent = std::nullopt)
    {
        Group group;
        group.id = ids.NextTyped<IdKind::Group>();
        group.displayName = name;
        group.parentId = parent;
        snapshot.groups.push_back(group);
        return group.id;
    }

    //! Feature と Entity の組を1つ足す。並べ方はどの物でも同じにする。
    [[nodiscard]] EntityId Adopt(Feature feature, Entity entity, const char* outputName,
        EntityKind kind)
    {
        entity.createdBy = feature.id;
        feature.outputs.push_back(FeatureOutput{outputName, entity.id, kind});
        const EntityId id = entity.id;
        snapshot.evaluationOrder.push_back(feature.id);
        snapshot.features.push_back(std::move(feature));
        snapshot.entities.push_back(std::move(entity));
        return id;
    }

    [[nodiscard]] EntityId AddWorkPlane(const std::string& name, double stationMm,
        const GroupId& groupId)
    {
        Feature feature;
        feature.id = ids.NextTyped<IdKind::Feature>();
        feature.type = FeatureType::CreateWorkPlane;
        feature.displayName = name;
        CreateWorkPlaneDefinition definition;
        // 前後方向が X。断面は YZ に平行な面へ引く。
        definition.origin = Vector3{stationMm, 0.0, 0.0};
        definition.normal = Vector3{1.0, 0.0, 0.0};
        definition.uDirection = Vector3{0.0, 1.0, 0.0};
        feature.definition = std::move(definition);
        Entity entity;
        entity.id = ids.NextTyped<IdKind::Entity>();
        entity.kind = EntityKind::WorkPlane;
        entity.displayName = name;
        entity.groupId = groupId;
        return Adopt(std::move(feature), std::move(entity), "plane",
            EntityKind::WorkPlane);
    }

    [[nodiscard]] AddedWire AddWire(const std::string& name,
        std::vector<CurveSegment> segments, const GroupId& groupId,
        Visibility visibility = Visibility::Visible)
    {
        Feature feature;
        feature.id = ids.NextTyped<IdKind::Feature>();
        feature.type = FeatureType::CreateWire;
        feature.displayName = name;
        CreateWireDefinition definition;
        definition.segments = std::move(segments);
        AddedWire added;
        for (std::size_t index = 0; index < definition.segments.size(); ++index) {
            definition.segmentIds.push_back(ids.NextTyped<IdKind::Segment>());
        }
        added.segmentIds = definition.segmentIds;
        feature.definition = std::move(definition);
        Entity entity;
        entity.id = ids.NextTyped<IdKind::Entity>();
        entity.kind = EntityKind::Wire;
        entity.displayName = name;
        entity.groupId = groupId;
        entity.visibility = visibility;
        added.entityId = entity.id;
        (void)Adopt(std::move(feature), std::move(entity), "wire", EntityKind::Wire);
        return added;
    }
};

//! 1本のワイヤー全体を、面の入力の「鎖」1つにする。
//!
//! **鎖の1つの指し先は「1本のワイヤー」である。曲線1本ではない。**
//! 役割表を作り直す側は、指し先ごとにそのワイヤーの曲線をまとめて読む。
//! だから曲線の数だけ指し先を並べると、同じワイヤーを何度も入れることになり、
//! 「そのワイヤーは、すでに別の行に入っています」(UI-R006)で断られる。
//! 実際そうなっていて、HO の見本の面が一度も作れていなかった。
//! V1 の見本(`RailwayNoseSample.cpp`)は最初からこう書いてある。
[[nodiscard]] WireChainRef ChainOf(const AddedWire& wire)
{
    WireChainRef chain;
    if (wire.segmentIds.empty()) {
        return chain;
    }
    chain.segments.push_back(SegmentRef{wire.entityId, wire.segmentIds.front(), 0.0, 1.0});
    chain.reversed.push_back(false);
    return chain;
}

} // namespace

namespace {

//! §10 のまとまりの形。
struct HoGroups {
    GroupId root;
    GroupId sections;
    GroupId sectionWires;
    GroupId guides;
    GroupId sourceSurfaces;
    GroupId extrudeTests;
    GroupId approximation;
    GroupId generated;
};

[[nodiscard]] HoGroups MakeGroups(HoBuilder& builder)
{
    HoGroups groups;
    groups.root = builder.AddGroup("RailwayNose_HO");
    groups.sections = builder.AddGroup("Sections", groups.root);
    groups.sectionWires = builder.AddGroup("SectionWires", groups.root);
    groups.guides = builder.AddGroup("Guides", groups.root);
    groups.sourceSurfaces = builder.AddGroup("SourceSurfaces", groups.root);
    groups.extrudeTests = builder.AddGroup("ExtrudeTests", groups.root);
    groups.approximation = builder.AddGroup("Approximation", groups.root);
    groups.generated = builder.AddGroup("GeneratedExamples", groups.root);
    return groups;
}

//! 作業平面と断面ワイヤー。断面は同じ形の縮小コピーにしない(§19)。
struct HoSections {
    std::vector<EntityId> planes;
    std::vector<AddedWire> wires;
};

[[nodiscard]] HoSections MakeSections(HoBuilder& builder, const HoGroups& groups)
{
    HoSections made;
    for (const double station : HoNoseSectionStations()) {
        made.planes.push_back(builder.AddWorkPlane(HoNoseWorkPlaneName(station), station,
            groups.sections));
        made.wires.push_back(builder.AddWire(HoNoseSectionName(station),
            HoNoseSection(station), groups.sectionWires));
    }
    return made;
}

//! 案内線。面を作るときに実際に使う(§20)。飾りにしない。
struct HoGuides {
    //! 左の縁から右の縁まで、順に並べる。**並び順が面の縁を決める。**
    //! 曲線網は、最初と最後を面の縁として使い、間の線で内側の形を決める。
    std::vector<AddedWire> acrossNose;

    [[nodiscard]] const AddedWire& Roof() const { return acrossNose[3]; }
};

[[nodiscard]] HoGuides MakeGuides(HoBuilder& builder, const HoGroups& groups)
{
    HoGuides made;
    // 左の縁 → 裾 → 肩 → 屋根の中央 → 肩 → 右の縁。
    made.acrossNose.push_back(
        builder.AddWire("SkirtGuide_L", HoNoseSkirtGuide(true), groups.guides));
    made.acrossNose.push_back(
        builder.AddWire("LowerGuide", HoNoseLowerGuide(), groups.guides));
    made.acrossNose.push_back(
        builder.AddWire("ShoulderGuide_L", HoNoseShoulderGuide(true), groups.guides));
    made.acrossNose.push_back(
        builder.AddWire("RoofCenterGuide", HoNoseRoofCenterGuide(), groups.guides));
    made.acrossNose.push_back(
        builder.AddWire("ShoulderGuide_R", HoNoseShoulderGuide(false), groups.guides));
    made.acrossNose.push_back(
        builder.AddWire("SkirtGuide_R", HoNoseSkirtGuide(false), groups.guides));
    return made;
}

//! 断面と案内線から前頭部の面を作る。曲線網。
//!
//! 案内付きロフト(`MakePipeShell`)ではない。あれは **平らな断面** を
//! 背骨に沿って送る作りで、前面中央が前へ膨らむこの形の断面は平らではない。
//! 断面の上で x が動くので、送る作りでは受け取ってもらえない。
//!
//! 曲線網なら、前後に走る線と断面を **どちらも拘束として** 使う。
//! 前後の線の最初と最後(左右の裾の縁)、断面の最初と最後(前端と車体側)が
//! 面の縁になり、間の線が内側の形を決める。案内線が飾りにならない。
[[nodiscard]] EntityId MakeNoseSurface(HoBuilder& builder, const HoGroups& groups,
    const HoSections& sections, const HoGuides& guides)
{
    Feature feature;
    feature.id = builder.ids.NextTyped<IdKind::Feature>();
    feature.type = FeatureType::CreateGuideSurface;
    feature.displayName = "NoseSurface";
    CreateGuideSurfaceDefinition definition;
    definition.method = static_cast<int>(modeling::GuideSurfaceMethod::GordonNetwork);
    // 前後に走る線(U)。左の縁から右の縁まで、並び順のまま。
    for (const AddedWire& guide : guides.acrossNose) {
        definition.chains.push_back(ChainOf(guide));
        definition.roles.push_back(static_cast<int>(modeling::ChainRole::GuideU));
        feature.inputEntityIds.push_back(guide.entityId);
    }
    // 断面(V)。前端から車体側まで、並び順のまま。
    for (const AddedWire& section : sections.wires) {
        definition.chains.push_back(ChainOf(section));
        definition.roles.push_back(static_cast<int>(modeling::ChainRole::GuideV));
        feature.inputEntityIds.push_back(section.entityId);
    }
    feature.definition = std::move(definition);
    Entity entity;
    entity.id = builder.ids.NextTyped<IdKind::Entity>();
    entity.kind = EntityKind::GuideSurface;
    entity.displayName = "NoseSurface";
    entity.groupId = groups.sourceSurfaces;
    entity.editPolicy = EditPolicy::Derived;
    return builder.Adopt(std::move(feature), std::move(entity), "surface",
        EntityKind::GuideSurface);
}

} // namespace

namespace {

//! 四隅を丸めた長方形。窓と前照灯の輪郭に使う。閉じている。
[[nodiscard]] std::vector<CurveSegment> RoundedRectangle(double centreY, double centreZ,
    double halfWidth, double halfHeight, double radius, double x)
{
    const double r = std::min(radius, std::min(halfWidth, halfHeight) * 0.9);
    const double y0 = centreY - halfWidth;
    const double y1 = centreY + halfWidth;
    const double z0 = centreZ - halfHeight;
    const double z1 = centreZ + halfHeight;
    const auto at = [x](double y, double z) { return Vector3{x, y, z}; };
    std::vector<CurveSegment> loop;
    const auto line = [&](const Vector3& from, const Vector3& to) {
        auto made = CurveSegment::MakeLine(from, to);
        if (made.HasValue()) {
            loop.push_back(made.Value());
        }
    };
    // 角は円弧のまま持つ。折れ線へ落とすと、型紙にしたときに角が多角形になる。
    // 面は X を向いているので、円弧の法線は X、基準は +Y。
    const Vector3 normal{1.0, 0.0, 0.0};
    const Vector3 reference{0.0, 1.0, 0.0};
    constexpr double kQuarter = 1.5707963267948966;
    const auto corner = [&](double cy, double cz, double startAngleRad) {
        auto made = CurveSegment::MakeCircularArc(at(cy, cz), normal, reference, r,
            startAngleRad, kQuarter);
        if (made.HasValue()) {
            loop.push_back(made.Value());
        }
    };
    line(at(y0 + r, z0), at(y1 - r, z0));
    corner(y1 - r, z0 + r, -kQuarter);
    line(at(y1, z0 + r), at(y1, z1 - r));
    corner(y1 - r, z1 - r, 0.0);
    line(at(y1 - r, z1), at(y0 + r, z1));
    corner(y0 + r, z1 - r, kQuarter);
    line(at(y0, z1 - r), at(y0, z0 + r));
    corner(y0 + r, z0 + r, 2.0 * kQuarter);
    return loop;
}

//! 押し出し試験の材料(§23)。
//!   A 閉じたワイヤー → 押し出し → 新しい立体(FloorSolid)
//!   B 立体 + ワイヤー → 切削(WindowProfile)
//!   C 立体の面 → 押し引き(画面から行う。ここでは相手の立体を用意する)
void MakeExtrudeTests(HoBuilder& builder, const HoGroups& groups)
{
    // 床板。前頭部の下に敷く 0.5mm 厚の板。HO の工作として妥当な厚み。
    const double halfWidth = kHoNoseWidthMm * 0.5;
    const AddedWire floorProfile = builder.AddWire("FloorProfile",
        RoundedRectangle(0.0, kHoNoseDepthMm * 0.5, halfWidth * 0.92,
            kHoNoseDepthMm * 0.5, 2.0, 0.0),
        groups.extrudeTests);
    Feature feature;
    feature.id = builder.ids.NextTyped<IdKind::Feature>();
    feature.type = FeatureType::Extrude;
    feature.displayName = "FloorSolid";
    feature.inputEntityIds = {floorProfile.entityId};
    ExtrudeDefinition definition;
    definition.profiles = {floorProfile.entityId};
    // 床板は前後方向ではなく上下へ薄く押す。X を向いた面に引いた輪郭なので、
    // 押す向きは X。厚みは 0.5mm(0.3〜1.5mm の範囲、§23)。
    definition.direction = Vector3{1.0, 0.0, 0.0};
    definition.distance = {"0.5", 0.5, geometry::QuantityKind::Length};
    definition.extentMode = 0;
    definition.booleanMode = 0;
    feature.definition = std::move(definition);
    Entity entity;
    entity.id = builder.ids.NextTyped<IdKind::Entity>();
    entity.kind = EntityKind::Part;
    entity.displayName = "FloorSolid";
    entity.groupId = groups.extrudeTests;
    entity.editPolicy = EditPolicy::Derived;
    (void)builder.Adopt(std::move(feature), std::move(entity), "part", EntityKind::Part);

    // 切削用の窓と前照灯。画面から「立体 + 輪郭 → 切削」で使う。
    (void)builder.AddWire("WindowProfile",
        RoundedRectangle(0.0, kHoNoseHeightMm * 0.66, kHoNoseWidthMm * 0.26,
            kHoNoseHeightMm * 0.12, 1.2, 0.0),
        groups.extrudeTests);
    (void)builder.AddWire("HeadlightProfile",
        RoundedRectangle(0.0, kHoNoseHeightMm * 0.86, 1.8, 1.8, 1.7, 0.0),
        groups.extrudeTests);
}

//! 板材近似。面を少数の連続した部材へ近似する(§24〜26)。
void MakeApproximation(HoBuilder& builder, const HoGroups& groups,
    const EntityId& surfaceId)
{
    Feature feature;
    feature.id = builder.ids.NextTyped<IdKind::Feature>();
    feature.type = FeatureType::CreateFabricationModel;
    feature.displayName = "NoseApproximation";
    feature.inputEntityIds = {surfaceId};
    CreateFabricationModelDefinition definition;
    definition.parts = {surfaceId};
    // HO のプラ板。0.3mm 厚は模型工作でよく使う。
    definition.materialThickness = {"0.3", 0.3, geometry::QuantityKind::Length};
    // 誤差は **模型実寸 mm** で持つ(§25)。実車換算ではない。
    definition.targetMaxDeviation = {"0.15", 0.15, geometry::QuantityKind::Length};
    definition.fidelity = 6;
    // V1 方式(帯へ近似し直す)。二重曲率を含む前頭部を、少数の帯へ通せる。
    definition.method = 1;
    definition.splitAxis = 2;
    definition.maximumPartCount = 8;
    definition.minimumPartWidthMm = 2.0;
    definition.masterPercent = 100.0;
    feature.definition = std::move(definition);
    Entity entity;
    entity.id = builder.ids.NextTyped<IdKind::Entity>();
    entity.kind = EntityKind::FabricationModel;
    entity.displayName = "NoseApproximation";
    entity.groupId = groups.approximation;
    entity.editPolicy = EditPolicy::Derived;
    (void)builder.Adopt(std::move(feature), std::move(entity), "fabrication",
        EntityKind::FabricationModel);
}

} // namespace

io::DocumentFile BuildRailwayNoseHoSampleDocument()
{
    HoBuilder builder;
    builder.snapshot.id = base::DocumentId::Parse(
        "00000000-0000-4000-8000-000000008001").value();
    builder.snapshot.revision = 1;

    const HoGroups groups = MakeGroups(builder);
    const HoSections sections = MakeSections(builder, groups);
    const HoGuides guides = MakeGuides(builder, groups);
    const EntityId surface = MakeNoseSurface(builder, groups, sections, guides);
    MakeExtrudeTests(builder, groups);
    MakeApproximation(builder, groups, surface);

    io::DocumentFile file;
    file.snapshot = std::move(builder.snapshot);
    file.metadata.title = "流線形前頭部 日本型HO 1/80 総合試験";
    file.metadata.author = "kachakachaCAD";
    file.metadata.description =
        "Scale: Japanese HO 1/80 / Gauge: 16.5mm / Units: model millimeters。"
        "模型実寸で作ってある(幅 35.0mm、高さ 45.0mm、前頭部の奥行き 18.0mm)。"
        "実車寸法で作って縮めたものではない。特定の実車の写しでもない。"
        "作業平面6枚、断面ワイヤー6本、案内線4本、案内付きロフト面、"
        "押し出し試験(床板・窓・前照灯)、板材近似を含む。"
        "完成形を直接書き込まず、作り方(Feature)として持っている。";
    return file;
}

base::Result<std::string> BuildRailwayNoseHoSampleArchive()
{
    return io::SaveDocument(BuildRailwayNoseHoSampleDocument());
}

std::string_view RailwayNoseHoSampleVersion() noexcept
{
    return "1";
}

} // namespace kachakacha::v2::app
