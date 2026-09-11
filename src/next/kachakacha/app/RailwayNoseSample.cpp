#include "kachakacha/app/RailwayNoseSample.h"

#include "kachakacha/domain/Feature.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace kachakacha::v2::app {
namespace {

using base::DeterministicIdGenerator;
using base::EntityId;
using base::IdKind;
using document::DocumentSnapshot;
using document::Group;
using domain::CreateFabricationModelDefinition;
using domain::CreateGuideSurfaceDefinition;
using domain::CreateWireDefinition;
using domain::EditPolicy;
using domain::Entity;
using domain::EntityKind;
using domain::Feature;
using domain::FeatureOutput;
using domain::FeatureType;
using domain::ManufacturingProperties;
using domain::PartRole;
using domain::SegmentRef;
using domain::ThickenSurfaceDefinition;
using domain::Visibility;
using domain::WireChainRef;
using geometry::CurveSegment;
using geometry::Vector3;

constexpr double kBezierCircle = 0.5522847498307936;

struct AddedWire {
    EntityId entityId;
    std::vector<base::SegmentId> segmentIds;
};

struct Builder {
    DeterministicIdGenerator ids{8701};
    DocumentSnapshot snapshot;

    [[nodiscard]] AddedWire AddWire(const std::string& name,
        std::vector<CurveSegment> segments, const base::GroupId& groupId,
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
        added.entityId = entity.id;
        entity.kind = EntityKind::Wire;
        entity.displayName = name;
        entity.createdBy = feature.id;
        entity.groupId = groupId;
        entity.visibility = visibility;
        feature.outputs.push_back(FeatureOutput{"wire", entity.id, EntityKind::Wire});
        snapshot.evaluationOrder.push_back(feature.id);
        snapshot.features.push_back(std::move(feature));
        snapshot.entities.push_back(std::move(entity));
        return added;
    }

    [[nodiscard]] EntityId AddGuide(const std::vector<AddedWire>& sections,
        const base::GroupId& groupId)
    {
        Feature feature;
        feature.id = ids.NextTyped<IdKind::Feature>();
        feature.type = FeatureType::CreateGuideSurface;
        feature.displayName = "前頭部ロフト面";
        CreateGuideSurfaceDefinition definition;
        definition.method = static_cast<int>(modeling::GuideSurfaceMethod::LoftSections);
        for (const AddedWire& section : sections) {
            WireChainRef chain;
            chain.segments.push_back(SegmentRef{section.entityId,
                section.segmentIds.front(), 0.0, 1.0});
            chain.reversed.push_back(false);
            definition.chains.push_back(std::move(chain));
            definition.roles.push_back(static_cast<int>(modeling::ChainRole::Section));
            feature.inputEntityIds.push_back(section.entityId);
        }
        feature.definition = std::move(definition);

        Entity entity;
        entity.id = ids.NextTyped<IdKind::Entity>();
        entity.kind = EntityKind::GuideSurface;
        entity.displayName = feature.displayName;
        entity.createdBy = feature.id;
        entity.groupId = groupId;
        entity.editPolicy = EditPolicy::Derived;
        feature.outputs.push_back(
            FeatureOutput{"surface", entity.id, EntityKind::GuideSurface});
        snapshot.evaluationOrder.push_back(feature.id);
        snapshot.features.push_back(std::move(feature));
        snapshot.entities.push_back(std::move(entity));
        return snapshot.entities.back().id;
    }

    [[nodiscard]] EntityId AddPart(const EntityId& guideId, const base::GroupId& groupId)
    {
        Feature feature;
        feature.id = ids.NextTyped<IdKind::Feature>();
        feature.type = FeatureType::ThickenSurface;
        feature.displayName = "前頭部外板 0.20mm";
        feature.inputEntityIds = {guideId};
        ThickenSurfaceDefinition definition;
        definition.surface = guideId;
        definition.thickness = {"0.20", 0.20, geometry::QuantityKind::Length};
        definition.placement = 1;
        feature.definition = std::move(definition);

        Entity entity;
        entity.id = ids.NextTyped<IdKind::Entity>();
        entity.kind = EntityKind::Part;
        entity.displayName = feature.displayName;
        entity.createdBy = feature.id;
        entity.groupId = groupId;
        entity.editPolicy = EditPolicy::Derived;
        entity.partRole = PartRole::FinishedModel;
        ManufacturingProperties properties;
        properties.materialName = "プラ板";
        properties.processName = "曲げ成形";
        properties.nominalThicknessMm = 0.20;
        properties.scaleDenominator = kRailwayNoseScaleDenominator;
        properties.note = "ER1 / 初期ER2を参考にした試験用近似形状";
        entity.manufacturing = std::move(properties);
        feature.outputs.push_back(FeatureOutput{"part", entity.id, EntityKind::Part});
        snapshot.evaluationOrder.push_back(feature.id);
        snapshot.features.push_back(std::move(feature));
        snapshot.entities.push_back(std::move(entity));
        return snapshot.entities.back().id;
    }

    void AddFabricationModel(const EntityId& guideId,
        const std::vector<AddedWire>& openings, const base::GroupId& groupId)
    {
        Feature feature;
        feature.id = ids.NextTyped<IdKind::Feature>();
        feature.type = FeatureType::CreateFabricationModel;
        feature.displayName = "前頭部 帯近似";
        CreateFabricationModelDefinition definition;
        definition.parts = {guideId};
        definition.materialThickness = {"0.20", 0.20, geometry::QuantityKind::Length};
        definition.targetMaxDeviation = {"0.11", 0.11, geometry::QuantityKind::Length};
        definition.fidelity = 6;
        definition.method = 1;
        definition.splitAxis = 2;
        definition.maximumPartCount = 24;
        definition.minimumPartWidthMm = 2.0;
        definition.masterPercent = 100.0;
        feature.inputEntityIds.push_back(guideId);
        for (const AddedWire& opening : openings) {
            definition.openingWires.push_back(opening.entityId);
            feature.inputEntityIds.push_back(opening.entityId);
        }
        feature.definition = std::move(definition);

        Entity entity;
        entity.id = ids.NextTyped<IdKind::Entity>();
        entity.kind = EntityKind::FabricationModel;
        entity.displayName = feature.displayName;
        entity.createdBy = feature.id;
        entity.groupId = groupId;
        entity.editPolicy = EditPolicy::Derived;
        feature.outputs.push_back(
            FeatureOutput{"fabrication", entity.id, EntityKind::FabricationModel});
        snapshot.evaluationOrder.push_back(feature.id);
        snapshot.features.push_back(std::move(feature));
        snapshot.entities.push_back(std::move(entity));
    }
};

[[nodiscard]] CurveSegment BezierOnSurface(const std::array<std::array<double, 2>, 4>& uv)
{
    std::vector<Vector3> controls;
    for (const auto& point : uv) {
        controls.push_back(RailwayNosePoint(point[0], point[1]));
    }
    return CurveSegment::MakeCubicBezier(std::move(controls)).Value();
}

[[nodiscard]] std::vector<CurveSegment> RoundedRectangleOnSurface(double left,
    double bottom, double right, double top, double radius)
{
    const double k = radius * kBezierCircle;
    const auto straight = [](double a, double b) {
        return std::array<double, 4>{a, a + (b - a) / 3.0, a + 2.0 * (b - a) / 3.0, b};
    };
    const auto bx = straight(left + radius, right - radius);
    const auto by = straight(bottom + radius, top - radius);
    std::vector<CurveSegment> result;
    result.push_back(BezierOnSurface({{{bx[0], bottom}, {bx[1], bottom},
        {bx[2], bottom}, {bx[3], bottom}}}));
    result.push_back(BezierOnSurface({{{right - radius, bottom}, {right - radius + k, bottom},
        {right, bottom + radius - k}, {right, bottom + radius}}}));
    result.push_back(BezierOnSurface({{{right, by[0]}, {right, by[1]},
        {right, by[2]}, {right, by[3]}}}));
    result.push_back(BezierOnSurface({{{right, top - radius}, {right, top - radius + k},
        {right - radius + k, top}, {right - radius, top}}}));
    result.push_back(BezierOnSurface({{{bx[3], top}, {bx[2], top},
        {bx[1], top}, {bx[0], top}}}));
    result.push_back(BezierOnSurface({{{left + radius, top}, {left + radius - k, top},
        {left, top - radius + k}, {left, top - radius}}}));
    result.push_back(BezierOnSurface({{{left, by[3]}, {left, by[2]},
        {left, by[1]}, {left, by[0]}}}));
    result.push_back(BezierOnSurface({{{left, bottom + radius}, {left, bottom + radius - k},
        {left + radius - k, bottom}, {left + radius, bottom}}}));
    return result;
}

[[nodiscard]] std::vector<CurveSegment> EllipseOnSurface(double centerU, double centerV,
    double radiusU, double radiusV)
{
    const double ku = radiusU * kBezierCircle;
    const double kv = radiusV * kBezierCircle;
    return {
        BezierOnSurface({{{centerU + radiusU, centerV}, {centerU + radiusU, centerV + kv},
            {centerU + ku, centerV + radiusV}, {centerU, centerV + radiusV}}}),
        BezierOnSurface({{{centerU, centerV + radiusV}, {centerU - ku, centerV + radiusV},
            {centerU - radiusU, centerV + kv}, {centerU - radiusU, centerV}}}),
        BezierOnSurface({{{centerU - radiusU, centerV}, {centerU - radiusU, centerV - kv},
            {centerU - ku, centerV - radiusV}, {centerU, centerV - radiusV}}}),
        BezierOnSurface({{{centerU, centerV - radiusV}, {centerU + ku, centerV - radiusV},
            {centerU + radiusU, centerV - kv}, {centerU + radiusU, centerV}}}),
    };
}

[[nodiscard]] std::vector<CurveSegment> SectionAt(double v)
{
    const Vector3 first = RailwayNosePoint(0.0, v);
    const Vector3 last = RailwayNosePoint(1.0, v);
    const double roofBlend = v > 0.7 ? (v - 0.7) / 0.3 : 0.0;
    const double curvature = kRailwayNosePlanBulgeMm
        + kRailwayNoseShoulderRoundMm * roofBlend * roofBlend;
    const Vector3 firstDerivative{kRailwayNoseWidthMm, 4.0 * curvature, 0.0};
    const Vector3 lastDerivative{kRailwayNoseWidthMm, -4.0 * curvature, 0.0};
    return {CurveSegment::MakeCubicBezier({first, first + firstDerivative * (1.0 / 3.0),
        last - lastDerivative * (1.0 / 3.0), last}).Value()};
}

} // namespace

Vector3 RailwayNosePoint(double u, double v) noexcept
{
    const double across = 2.0 * (u - 0.5);
    const double x = across * kRailwayNoseWidthMm * 0.5;
    const double bulge = kRailwayNosePlanBulgeMm * (1.0 - across * across);
    const double roofBlend = v > 0.7 ? (v - 0.7) / 0.3 : 0.0;
    const double shoulder = across * across;
    const double setback = kRailwayNoseShoulderRoundMm
        * roofBlend * roofBlend * shoulder;
    return Vector3{x, bulge - setback, v * kRailwayNoseHeightMm};
}

fabrication::SurfacePatchSamples BuildRailwayNoseSurfaceSamples(std::size_t rows,
    std::size_t columns)
{
    fabrication::SurfacePatchSamples samples;
    if (rows < 2 || columns < 2) {
        return samples;
    }
    samples.rowCount = rows;
    samples.columnCount = columns;
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < columns; ++column) {
            samples.points.push_back(RailwayNosePoint(
                static_cast<double>(column) / static_cast<double>(columns - 1),
                static_cast<double>(row) / static_cast<double>(rows - 1)));
        }
    }
    return samples;
}

FabricationMarkings BuildRailwayNoseOpenings()
{
    FabricationMarkings markings;
    const std::array<std::array<double, 2>, 6> windows{{
        {0.055, 0.165}, {0.185, 0.325}, {0.345, 0.49},
        {0.51, 0.655}, {0.675, 0.815}, {0.835, 0.945},
    }};
    for (const auto& span : windows) {
        markings.openings.push_back(
            RoundedRectangleOnSurface(span[0], 0.45, span[1], 0.63, 0.018));
    }
    markings.openings.push_back(EllipseOnSurface(0.5, 0.83, 0.055, 0.055));
    return markings;
}

FabricationSource BuildRailwayNoseFabricationSource()
{
    FabricationSource source;
    source.name = "流線形前頭部";
    source.samples = BuildRailwayNoseSurfaceSamples();
    return source;
}

io::DocumentFile BuildRailwayNoseSampleDocument()
{
    Builder builder;
    builder.snapshot.id = base::DocumentId::Parse(
        "00000000-0000-4000-8000-000000008701").value();
    builder.snapshot.revision = 1;

    Group sections;
    sections.id = builder.ids.NextTyped<IdKind::Group>();
    sections.displayName = "断面ワイヤー";
    builder.snapshot.groups.push_back(sections);
    Group openings;
    openings.id = builder.ids.NextTyped<IdKind::Group>();
    openings.displayName = "窓と前照灯";
    builder.snapshot.groups.push_back(openings);
    Group derived;
    derived.id = builder.ids.NextTyped<IdKind::Group>();
    derived.displayName = "前頭部モデル";
    builder.snapshot.groups.push_back(derived);

    std::vector<AddedWire> sectionWires;
    const std::array<double, 7> levels{0.0, 0.25, 0.45, 0.63, 0.72, 0.86, 1.0};
    for (std::size_t index = 0; index < levels.size(); ++index) {
        sectionWires.push_back(builder.AddWire("水平断面" + std::to_string(index + 1),
            SectionAt(levels[index]), sections.id, Visibility::Reference));
    }
    const EntityId guideId = builder.AddGuide(sectionWires, derived.id);
    (void)builder.AddPart(guideId, derived.id);

    std::vector<AddedWire> openingWires;
    const FabricationMarkings curves = BuildRailwayNoseOpenings();
    for (std::size_t index = 0; index < 6; ++index) {
        openingWires.push_back(builder.AddWire("前面窓" + std::to_string(index + 1),
            curves.openings[index], openings.id));
    }
    openingWires.push_back(builder.AddWire(
        "中央上部前照灯", curves.openings.back(), openings.id));
    builder.AddFabricationModel(guideId, openingWires, derived.id);

    io::DocumentFile file;
    file.snapshot = std::move(builder.snapshot);
    file.metadata.title = "流線形鉄道車両 前頭部 1/87 実用試験";
    file.metadata.author = "kachakachaCAD";
    file.metadata.description =
        "ER1 / 初期ER2の丸形前頭部を参考にした試験用近似形状。幅3520mmのみを"
        "1/87に換算し、奥行き・曲率・窓寸法は実車寸法ではない。7断面のロフト、"
        "滑らかな6枚窓、中央上部前照灯、0.20mm外板、帯近似製作モデルを含む。";
    return file;
}

base::Result<std::string> BuildRailwayNoseSampleArchive()
{
    return io::SaveDocument(BuildRailwayNoseSampleDocument());
}

std::string_view RailwayNoseSampleVersion() noexcept
{
    return "1";
}

} // namespace kachakacha::v2::app
