#include "kachakacha/occt/BodyExport.h"

#include <BRepBndLib.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <GeomAPI_PointsToBSplineSurface.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_Surface.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <GC_MakeSegment2d.hxx>
#include <STEPControl_StepModelType.hxx>
#include <STEPControl_Writer.hxx>
#include <StlAPI_Writer.hxx>
#include <NCollection_HArray1.hxx>
#include <NCollection_Array2.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeFix_Solid.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>
#include <unordered_set>

namespace kachakacha::occt {

namespace {

constexpr double kModelTolerance = 1.0e-6;
constexpr int kSectionSamples = 9;
constexpr int kCurveSamples = 25;
constexpr int kPlateSectionSamples = 9;
constexpr int kPlateCurveSamples = 25;
constexpr int kOpeningSamples = 32;

gp_Pnt ToPoint(geometry::Vector3 point)
{
    return {point.x, point.y, point.z};
}

occ::handle<Geom_BSplineCurve> InterpolateCurve(const std::vector<geometry::Vector3>& points)
{
    if (points.size() < 2) {
        throw std::invalid_argument("A body section needs at least two points.");
    }
    occ::handle<NCollection_HArray1<gp_Pnt>> values =
        new NCollection_HArray1<gp_Pnt>(1, static_cast<int>(points.size()));
    for (int index = 0; index < static_cast<int>(points.size()); ++index) {
        values->SetValue(index + 1, ToPoint(points[static_cast<std::size_t>(index)]));
    }
    GeomAPI_Interpolate interpolation(values, false, kModelTolerance);
    interpolation.Perform();
    if (!interpolation.IsDone()) {
        throw std::runtime_error("Could not interpolate a smooth jig section.");
    }
    return interpolation.Curve();
}

TopoDS_Wire BuildContactSectionWire(const model::Body& body, double v)
{
    std::vector<geometry::Vector3> contact;
    contact.reserve(kCurveSamples);
    for (int index = 0; index < kCurveSamples; ++index) {
        const double u = static_cast<double>(index) / static_cast<double>(kCurveSamples - 1);
        contact.push_back(body.Evaluate(u, v, 0.0));
    }

    const TopoDS_Edge contactEdge = BRepBuilderAPI_MakeEdge(InterpolateCurve(contact));
    BRepBuilderAPI_MakeWire builder;
    builder.Add(contactEdge);
    if (!builder.IsDone()) {
        throw std::runtime_error("Could not build a jig contact section.");
    }
    return builder.Wire();
}

TopoDS_Shape BuildBodyShape(const model::Body& body)
{
    BRepOffsetAPI_ThruSections loft(false, false, kModelTolerance);
    for (int index = 0; index < kSectionSamples; ++index) {
        const double v = static_cast<double>(index) / static_cast<double>(kSectionSamples - 1);
        loft.AddWire(BuildContactSectionWire(body, v));
    }
    loft.Build();
    if (!loft.IsDone()) {
        throw std::runtime_error("Could not build the jig contact surface.");
    }
    const TopoDS_Shape contactSurface = loft.Shape();
    if (contactSurface.IsNull()) {
        throw std::runtime_error("The generated jig contact surface is empty.");
    }

    BRepOffsetAPI_MakeThickSolid thickSolid;
    thickSolid.MakeThickSolidBySimple(
        contactSurface, body.BackingOffset() - body.ContactOffset());
    if (!thickSolid.IsDone() || thickSolid.Shape().IsNull()) {
        throw std::runtime_error("Could not add thickness to the jig contact surface.");
    }
    const TopoDS_Shape shape = thickSolid.Shape();
    TopoDS_Shape repaired;
    if (shape.ShapeType() == TopAbs_SOLID) {
        ShapeFix_Solid repair(TopoDS::Solid(shape));
        repair.SetPrecision(kModelTolerance);
        repair.Perform();
        repaired = repair.Solid();
    } else {
        ShapeFix_Shape repair(shape);
        repair.SetPrecision(kModelTolerance);
        repair.Perform();
        repaired = repair.Shape();
    }
    if (repaired.ShapeType() == TopAbs_SHELL) {
        BRepBuilderAPI_MakeSolid solidBuilder(TopoDS::Shell(repaired));
        if (solidBuilder.IsDone()) {
            TopoDS_Solid solid = solidBuilder.Solid();
            BRepLib::OrientClosedSolid(solid);
            return solid;
        }
    } else if (repaired.ShapeType() == TopAbs_SOLID) {
        TopoDS_Solid solid = TopoDS::Solid(repaired);
        BRepLib::OrientClosedSolid(solid);
        return solid;
    }
    return repaired;
}

TopoDS_Wire BuildPolylineLoop(const std::vector<geometry::Vector3>& points)
{
    if (points.size() < 3) {
        throw std::invalid_argument("A closed model loop needs at least three points.");
    }
    BRepBuilderAPI_MakePolygon builder;
    const bool repeatedEnd = geometry::AlmostEqual(points.front(), points.back(), kModelTolerance);
    const std::size_t count = repeatedEnd ? points.size() - 1 : points.size();
    for (std::size_t index = 0; index < count; ++index) {
        builder.Add(ToPoint(points[index]));
    }
    builder.Close();
    if (!builder.IsDone()) {
        throw std::runtime_error("Could not build a closed model loop.");
    }
    return builder.Wire();
}

TopoDS_Wire BuildPlateSectionWire(const model::Plate& plate, double v)
{
    std::vector<geometry::Vector3> minimumLayer;
    std::vector<geometry::Vector3> maximumLayer;
    minimumLayer.reserve(kPlateCurveSamples);
    maximumLayer.reserve(kPlateCurveSamples);
    for (int index = 0; index < kPlateCurveSamples; ++index) {
        const double u = static_cast<double>(index) / static_cast<double>(kPlateCurveSamples - 1);
        minimumLayer.push_back(plate.Evaluate(u, v, 0.0));
        maximumLayer.push_back(plate.Evaluate(u, v, 1.0));
    }
    std::reverse(maximumLayer.begin(), maximumLayer.end());

    BRepBuilderAPI_MakeWire builder;
    builder.Add(BRepBuilderAPI_MakeEdge(InterpolateCurve(minimumLayer)));
    builder.Add(BRepBuilderAPI_MakeEdge(
        ToPoint(minimumLayer.back()), ToPoint(maximumLayer.front())));
    builder.Add(BRepBuilderAPI_MakeEdge(InterpolateCurve(maximumLayer)));
    builder.Add(BRepBuilderAPI_MakeEdge(
        ToPoint(maximumLayer.back()), ToPoint(minimumLayer.front())));
    if (!builder.IsDone()) {
        throw std::runtime_error("Could not build a closed plate section.");
    }
    return builder.Wire();
}

std::vector<geometry::Vector3> OffsetOpeningPoints(
    const model::NamedWire& opening,
    const model::Plate& plate,
    bool maximumLayer,
    double margin)
{
    if (!opening.projection.has_value() || !opening.wire.IsClosed()) {
        throw std::invalid_argument("A plate opening must be a closed projected wire.");
    }
    std::vector<geometry::Vector3> points;
    points.reserve(kOpeningSamples + 1);
    for (int sample = 0; sample <= kOpeningSamples; ++sample) {
        const geometry::Vector3 point = opening.wire.Evaluate(
            static_cast<double>(sample) / static_cast<double>(kOpeningSamples));
        const model::SurfaceProjection projection = plate.SourceSurface().ProjectPointAlongDirection(
            point, opening.projection->direction);
        const double localV = (projection.v - plate.Range().minimumV)
            / (plate.Range().maximumV - plate.Range().minimumV);
        const double offset = (maximumLayer
                ? plate.MaximumOffset(localV) + margin
                : plate.MinimumOffset(localV) - margin);
        points.push_back(projection.point
            + plate.SourceSurface().Normal(projection.u, projection.v) * offset);
    }
    if (!points.empty()) {
        points.back() = points.front();
    }
    return points;
}

TopoDS_Shape BuildOpeningCutter(
    const model::NamedWire& opening,
    const model::Plate& plate)
{
    const double margin = std::max(0.25, plate.Thickness() * 0.5);
    BRepOffsetAPI_ThruSections loft(true, false, kModelTolerance);
    loft.AddWire(BuildPolylineLoop(OffsetOpeningPoints(
        opening, plate, false, margin)));
    loft.AddWire(BuildPolylineLoop(OffsetOpeningPoints(
        opening, plate, true, margin)));
    loft.Build();
    if (!loft.IsDone() || loft.Shape().IsNull()) {
        throw std::runtime_error("Could not build a cutter for plate opening: " + opening.name);
    }
    for (TopExp_Explorer explorer(loft.Shape(), TopAbs_SOLID); explorer.More(); explorer.Next()) {
        ShapeFix_Solid repair(TopoDS::Solid(explorer.Current()));
        repair.SetPrecision(kModelTolerance);
        repair.Perform();
        if (!repair.Solid().IsNull()) {
            return repair.Solid();
        }
    }
    throw std::runtime_error("Plate opening cutter is not a closed solid: " + opening.name);
}

const model::NamedWire& RequireOpeningWire(
    const model::Project& project,
    std::string_view name);

TopoDS_Wire BuildWireOnSurface(
    const occ::handle<Geom_Surface>& surface,
    const std::vector<gp_Pnt2d>& points,
    bool reverse)
{
    if (points.size() < 3) {
        throw std::invalid_argument("A surface loop needs at least three UV points.");
    }
    BRepBuilderAPI_MakeWire wireBuilder;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const gp_Pnt2d& first = points[index];
        const gp_Pnt2d& second = points[(index + 1) % points.size()];
        if (first.Distance(second) <= kModelTolerance) {
            continue;
        }
        const occ::handle<Geom2d_TrimmedCurve> segment = GC_MakeSegment2d(first, second);
        wireBuilder.Add(BRepBuilderAPI_MakeEdge(segment, surface));
    }
    if (!wireBuilder.IsDone()) {
        throw std::runtime_error("Could not build a UV loop on the plate surface.");
    }
    TopoDS_Wire wire = wireBuilder.Wire();
    if (reverse) {
        wire.Reverse();
    }
    BRepLib::BuildCurves3d(wire);
    return wire;
}

TopoDS_Shape BuildUniformCurvedPlateShape(
    const model::Project& project,
    const model::NamedPlate& namedPlate)
{
    constexpr int uSamples = 25;
    constexpr int vSamples = 13;
    NCollection_Array2<gp_Pnt> points(1, uSamples, 1, vSamples);
    for (int uIndex = 0; uIndex < uSamples; ++uIndex) {
        const double u = static_cast<double>(uIndex) / static_cast<double>(uSamples - 1);
        for (int vIndex = 0; vIndex < vSamples; ++vIndex) {
            const double v = static_cast<double>(vIndex) / static_cast<double>(vSamples - 1);
            points.SetValue(uIndex + 1, vIndex + 1, ToPoint(namedPlate.plate.Evaluate(u, v, 0.0)));
        }
    }
    GeomAPI_PointsToBSplineSurface approximation(
        points, 3, 8, GeomAbs_C2, 0.01);
    if (!approximation.IsDone() || approximation.Surface().IsNull()) {
        throw std::runtime_error("Could not fit a CAD surface for plate: " + namedPlate.name);
    }
    const occ::handle<Geom_Surface> surface(approximation.Surface());
    double surfaceUMinimum = 0.0;
    double surfaceUMaximum = 1.0;
    double surfaceVMinimum = 0.0;
    double surfaceVMaximum = 1.0;
    surface->Bounds(
        surfaceUMinimum, surfaceUMaximum, surfaceVMinimum, surfaceVMaximum);
    const TopoDS_Wire outer = BuildWireOnSurface(surface, {
        gp_Pnt2d{surfaceUMinimum, surfaceVMinimum},
        gp_Pnt2d{surfaceUMaximum, surfaceVMinimum},
        gp_Pnt2d{surfaceUMaximum, surfaceVMaximum},
        gp_Pnt2d{surfaceUMinimum, surfaceVMaximum}}, false);
    BRepBuilderAPI_MakeFace faceBuilder(surface, outer, true);
    if (!faceBuilder.IsDone()) {
        throw std::runtime_error("Could not build plate face: " + namedPlate.name);
    }

    for (const std::string& openingName : namedPlate.openingWireNames) {
        const model::NamedWire& opening = RequireOpeningWire(project, openingName);
        std::vector<gp_Pnt2d> uvPoints;
        uvPoints.reserve(kOpeningSamples);
        for (int sample = 0; sample < kOpeningSamples; ++sample) {
            const geometry::Vector3 point = opening.wire.Evaluate(
                static_cast<double>(sample) / static_cast<double>(kOpeningSamples));
            const model::SurfaceProjection projection = namedPlate.plate.SourceSurface()
                .ProjectPointAlongDirection(point, opening.projection->direction);
            const double localU = (projection.u - namedPlate.plate.Range().minimumU)
                / (namedPlate.plate.Range().maximumU - namedPlate.plate.Range().minimumU);
            const double localV = (projection.v - namedPlate.plate.Range().minimumV)
                / (namedPlate.plate.Range().maximumV - namedPlate.plate.Range().minimumV);
            uvPoints.emplace_back(
                surfaceUMinimum + localU * (surfaceUMaximum - surfaceUMinimum),
                surfaceVMinimum + localV * (surfaceVMaximum - surfaceVMinimum));
        }
        double signedArea = 0.0;
        for (std::size_t index = 0; index < uvPoints.size(); ++index) {
            const gp_Pnt2d& first = uvPoints[index];
            const gp_Pnt2d& second = uvPoints[(index + 1) % uvPoints.size()];
            signedArea += first.X() * second.Y() - second.X() * first.Y();
        }
        faceBuilder.Add(BuildWireOnSurface(surface, uvPoints, signedArea > 0.0));
    }
    faceBuilder.Build();
    if (!faceBuilder.IsDone() || faceBuilder.Face().IsNull()) {
        throw std::runtime_error("Could not add openings to plate face: " + namedPlate.name);
    }
    BRepOffsetAPI_MakeThickSolid thickSolid;
    thickSolid.MakeThickSolidBySimple(faceBuilder.Face(), namedPlate.plate.Thickness());
    if (!thickSolid.IsDone() || thickSolid.Shape().IsNull()) {
        throw std::runtime_error("Could not add thickness to opened plate: " + namedPlate.name);
    }
    return thickSolid.Shape();
}

const model::NamedWire& RequireOpeningWire(
    const model::Project& project,
    std::string_view name)
{
    const auto position = std::find_if(
        project.Wires().begin(), project.Wires().end(), [&](const model::NamedWire& wire) {
            return wire.name == name;
        });
    if (position == project.Wires().end()) {
        throw std::invalid_argument("Plate opening wire does not exist: " + std::string(name));
    }
    return *position;
}

TopoDS_Shape BuildVariablePlanarPlateShape(const model::NamedPlate& namedPlate)
{
    const model::Plate& plate = namedPlate.plate;
    const model::Surface& surface = plate.SourceSurface();
    const geometry::Vector3 normal = surface.Normal(0.5, 0.5);
    std::vector<geometry::Vector3> minimumLayer;
    std::vector<geometry::Vector3> maximumLayer;
    minimumLayer.reserve(129);
    maximumLayer.reserve(129);
    for (int index = 0; index <= 128; ++index) {
        const geometry::Vector3 sourcePoint = surface.FirstBoundary().Evaluate(
            static_cast<double>(index) / 128.0);
        const model::SurfaceProjection projection = surface.ProjectPointAlongDirection(
            sourcePoint + normal, normal * -1.0, kModelTolerance * 10.0);
        const double localV = (projection.v - plate.Range().minimumV)
            / (plate.Range().maximumV - plate.Range().minimumV);
        minimumLayer.push_back(sourcePoint + normal * plate.MinimumOffset(localV));
        maximumLayer.push_back(sourcePoint + normal * plate.MaximumOffset(localV));
    }
    minimumLayer.back() = minimumLayer.front();
    maximumLayer.back() = maximumLayer.front();

    BRepOffsetAPI_ThruSections loft(true, true, kModelTolerance);
    loft.AddWire(BuildPolylineLoop(minimumLayer));
    loft.AddWire(BuildPolylineLoop(maximumLayer));
    loft.Build();
    if (!loft.IsDone() || loft.Shape().IsNull()) {
        throw std::runtime_error("Could not build variable-thickness planar plate: " + namedPlate.name);
    }
    return loft.Shape();
}

TopoDS_Shape BuildPlateShape(
    const model::Project& project,
    const model::NamedPlate& namedPlate)
{
    const model::Plate& plate = namedPlate.plate;
    TopoDS_Shape shape;
    if (plate.SourceSurface().Kind() == model::SurfaceKind::Planar
        && plate.HasVariableThickness()) {
        shape = BuildVariablePlanarPlateShape(namedPlate);
    } else if (plate.SourceSurface().Kind() == model::SurfaceKind::Planar) {
        std::vector<geometry::Vector3> boundary;
        const model::Wire& sourceBoundary = plate.SourceSurface().FirstBoundary();
        const geometry::Vector3 normal = plate.SourceSurface().Normal(0.5, 0.5);
        if (sourceBoundary.Kind() == model::WireKind::Polyline) {
            boundary.reserve(sourceBoundary.ControlPoints().size());
            for (const geometry::Vector3 point : sourceBoundary.ControlPoints()) {
                boundary.push_back(point + normal * plate.MinimumOffset());
            }
        } else {
            boundary.reserve(129);
            for (int index = 0; index < 128; ++index) {
                boundary.push_back(sourceBoundary.Evaluate(static_cast<double>(index) / 128.0)
                    + normal * plate.MinimumOffset());
            }
        }
        if (!geometry::AlmostEqual(boundary.front(), boundary.back(), kModelTolerance)) {
            boundary.push_back(boundary.front());
        }
        const TopoDS_Face face = BRepBuilderAPI_MakeFace(BuildPolylineLoop(boundary));
        const geometry::Vector3 extrusion = normal * plate.Thickness();
        BRepPrimAPI_MakePrism prism(face, gp_Vec(extrusion.x, extrusion.y, extrusion.z));
        prism.Build();
        if (!prism.IsDone() || prism.Shape().IsNull()) {
            throw std::runtime_error("Could not add thickness to planar plate: " + namedPlate.name);
        }
        shape = prism.Shape();
    } else if (!plate.HasVariableThickness()) {
        shape = BuildUniformCurvedPlateShape(project, namedPlate);
    } else {
        if (!namedPlate.openingWireNames.empty()) {
            throw std::runtime_error(
                "Variable-thickness plate openings are not yet supported for 3D export: "
                + namedPlate.name);
        }
        BRepOffsetAPI_ThruSections loft(true, false, kModelTolerance);
        for (int index = 0; index < kPlateSectionSamples; ++index) {
            const double v = static_cast<double>(index)
                / static_cast<double>(kPlateSectionSamples - 1);
            loft.AddWire(BuildPlateSectionWire(plate, v));
        }
        loft.Build();
        if (!loft.IsDone() || loft.Shape().IsNull()) {
            throw std::runtime_error("Could not build closed plate solid: " + namedPlate.name);
        }
        shape = loft.Shape();
    }

    if (!namedPlate.openingWireNames.empty()
        && plate.SourceSurface().Kind() == model::SurfaceKind::Planar) {
        for (const std::string& openingName : namedPlate.openingWireNames) {
            BRepAlgoAPI_Cut cut(shape, BuildOpeningCutter(
                RequireOpeningWire(project, openingName), plate));
            cut.SetFuzzyValue(kModelTolerance * 10.0);
            cut.Build();
            if (!cut.IsDone() || cut.Shape().IsNull()) {
                throw std::runtime_error("Could not cut plate opening: " + openingName);
            }
            shape = cut.Shape();
        }
    }

    TopoDS_Shape largestSolid;
    double largestVolume = 0.0;
    for (TopExp_Explorer explorer(shape, TopAbs_SOLID); explorer.More(); explorer.Next()) {
        GProp_GProps properties;
        BRepGProp::VolumeProperties(explorer.Current(), properties);
        const double volume = std::abs(properties.Mass());
        if (volume > largestVolume) {
            largestVolume = volume;
            largestSolid = explorer.Current();
        }
    }
    if (largestSolid.IsNull()) {
        throw std::runtime_error("Plate did not produce a solid: " + namedPlate.name);
    }
    ShapeFix_Solid repair(TopoDS::Solid(largestSolid));
    repair.SetPrecision(kModelTolerance);
    repair.Perform();
    return repair.Solid();
}

struct BuiltModelShape {
    TopoDS_Shape shape;
    std::size_t plateCount = 0;
    std::size_t bodyCount = 0;
    double minimumWallMillimeters = 0.0;
};

BuiltModelShape BuildModelShape(
    const model::Project& project,
    const ModelShapeSelection& selection)
{
    if (selection.Empty()) {
        throw std::invalid_argument("Select at least one plate or jig for 3D export.");
    }
    BRep_Builder compoundBuilder;
    TopoDS_Compound compound;
    compoundBuilder.MakeCompound(compound);
    std::unordered_set<std::string> usedNames;
    double minimumWall = std::numeric_limits<double>::infinity();
    std::size_t plateCount = 0;
    std::size_t bodyCount = 0;

    for (const std::string& name : selection.plateNames) {
        if (!usedNames.insert("plate:" + name).second) {
            continue;
        }
        const auto position = std::find_if(
            project.Plates().begin(), project.Plates().end(), [&](const model::NamedPlate& plate) {
                return plate.name == name;
            });
        if (position == project.Plates().end()) {
            throw std::invalid_argument("3D export plate does not exist: " + name);
        }
        compoundBuilder.Add(compound, BuildPlateShape(project, *position));
        minimumWall = std::min(
            minimumWall, std::min(position->plate.Thickness(), position->plate.EndThickness()));
        ++plateCount;
    }
    for (const std::string& name : selection.bodyNames) {
        if (!usedNames.insert("body:" + name).second) {
            continue;
        }
        const auto position = std::find_if(
            project.Bodies().begin(), project.Bodies().end(), [&](const model::NamedBody& body) {
                return body.name == name;
            });
        if (position == project.Bodies().end()) {
            throw std::invalid_argument("3D export body does not exist: " + name);
        }
        compoundBuilder.Add(compound, BuildBodyShape(position->body));
        minimumWall = std::min(minimumWall, position->body.ThicknessMillimeters());
        ++bodyCount;
    }
    return {compound, plateCount, bodyCount, minimumWall};
}

ModelShapeAnalysis InspectModelShape(
    const BuiltModelShape& built,
    double requiredMinimumWallMillimeters)
{
    if (!std::isfinite(requiredMinimumWallMillimeters)
        || requiredMinimumWallMillimeters < 0.0) {
        throw std::invalid_argument("Required minimum wall must be non-negative.");
    }
    ModelShapeAnalysis analysis;
    analysis.plateCount = built.plateCount;
    analysis.bodyCount = built.bodyCount;
    analysis.partCount = built.plateCount + built.bodyCount;
    analysis.minimumWallMillimeters = built.minimumWallMillimeters;
    analysis.meetsMinimumWall = built.minimumWallMillimeters + kModelTolerance
        >= requiredMinimumWallMillimeters;
    analysis.validBRep = BRepCheck_Analyzer(built.shape, true).IsValid();
    bool shellsAreClosed = true;
    std::size_t solidCount = 0;
    for (TopExp_Explorer explorer(built.shape, TopAbs_SOLID); explorer.More(); explorer.Next()) {
        ++solidCount;
    }
    for (TopExp_Explorer explorer(built.shape, TopAbs_SHELL); explorer.More(); explorer.Next()) {
        shellsAreClosed = shellsAreClosed
            && BRep_Tool::IsClosed(TopoDS::Shell(explorer.Current()));
    }
    GProp_GProps properties;
    BRepGProp::VolumeProperties(built.shape, properties);
    analysis.volumeCubicMillimeters = std::abs(properties.Mass());
    analysis.closedSolid = solidCount >= analysis.partCount
        && shellsAreClosed && analysis.volumeCubicMillimeters > 1.0e-9;

    Bnd_Box bounds;
    BRepBndLib::Add(built.shape, bounds);
    bounds.Get(
        analysis.minimumBounds.x, analysis.minimumBounds.y, analysis.minimumBounds.z,
        analysis.maximumBounds.x, analysis.maximumBounds.y, analysis.maximumBounds.z);
    if (!analysis.validBRep) {
        analysis.message = "Selected 3D model contains invalid boundary geometry.";
    } else if (!analysis.closedSolid) {
        analysis.message = "Selected 3D model contains a shape that is not a closed solid.";
    } else if (!analysis.meetsMinimumWall) {
        analysis.message = "Selected 3D model is thinner than the required minimum wall.";
    } else {
        analysis.message = "Selected plates and jigs are valid closed solids.";
    }
    return analysis;
}

void RequireModelExportable(const BuiltModelShape& built)
{
    const ModelShapeAnalysis analysis = InspectModelShape(built, 0.0);
    if (!analysis.validBRep || !analysis.closedSolid) {
        throw std::runtime_error(analysis.message);
    }
}

BodyShapeAnalysis InspectShape(
    const TopoDS_Shape& shape,
    const model::Body& body,
    double requiredMinimumWallMillimeters)
{
    const auto printability = body.AnalyzePrintability(requiredMinimumWallMillimeters);
    BodyShapeAnalysis analysis;
    analysis.minimumWallMillimeters = printability.minimumWallMillimeters;
    analysis.meetsMinimumWall = printability.meetsMinimumWall;
    analysis.validBRep = BRepCheck_Analyzer(shape, true).IsValid();
    bool hasShell = false;
    bool shellsAreClosed = true;
    for (TopExp_Explorer explorer(shape, TopAbs_SHELL); explorer.More(); explorer.Next()) {
        hasShell = true;
        shellsAreClosed = shellsAreClosed
            && BRep_Tool::IsClosed(TopoDS::Shell(explorer.Current()));
    }
    analysis.closedSolid = shape.ShapeType() == TopAbs_SOLID && hasShell && shellsAreClosed;

    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    analysis.volumeCubicMillimeters = std::abs(properties.Mass());
    analysis.closedSolid = analysis.closedSolid && analysis.volumeCubicMillimeters > 1.0e-9;

    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    double xMinimum = 0.0;
    double yMinimum = 0.0;
    double zMinimum = 0.0;
    double xMaximum = 0.0;
    double yMaximum = 0.0;
    double zMaximum = 0.0;
    bounds.Get(xMinimum, yMinimum, zMinimum, xMaximum, yMaximum, zMaximum);
    analysis.minimumBounds = {xMinimum, yMinimum, zMinimum};
    analysis.maximumBounds = {xMaximum, yMaximum, zMaximum};

    if (!analysis.validBRep) {
        analysis.message = "The generated jig has invalid boundary geometry.";
    } else if (!analysis.closedSolid) {
        analysis.message = "The generated jig is not a closed solid (shape type "
            + std::to_string(static_cast<int>(shape.ShapeType()))
            + ", volume " + std::to_string(analysis.volumeCubicMillimeters) + ").";
    } else if (!analysis.meetsMinimumWall) {
        analysis.message = "The jig is thinner than the required minimum wall.";
    } else {
        analysis.message = "The jig is a valid closed solid and meets the minimum wall.";
    }
    return analysis;
}

std::filesystem::path TemporaryExportPath(std::string_view extension)
{
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path()
        / ("kachakacha_export_" + std::to_string(timestamp) + std::string(extension));
}

void PublishTemporaryFile(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination)
{
    if (destination.empty()) {
        throw std::invalid_argument("Export path must not be empty.");
    }
    if (destination.has_parent_path()) {
        std::filesystem::create_directories(destination.parent_path());
    }
    std::filesystem::copy_file(
        temporary, destination, std::filesystem::copy_options::overwrite_existing);
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
}

void RequireExportable(const TopoDS_Shape& shape, const model::Body& body)
{
    const BodyShapeAnalysis analysis = InspectShape(shape, body, 0.01);
    if (!analysis.validBRep || !analysis.closedSolid) {
        throw std::runtime_error(analysis.message);
    }
}

} // namespace

BodyShapeAnalysis AnalyzeBodyShape(
    const model::Body& body,
    double requiredMinimumWallMillimeters)
{
    return InspectShape(BuildBodyShape(body), body, requiredMinimumWallMillimeters);
}

void WriteBodyStl(
    const std::filesystem::path& path,
    const model::Body& body,
    double linearDeflectionMillimeters,
    double angularDeflectionRadians)
{
    if (!std::isfinite(linearDeflectionMillimeters) || linearDeflectionMillimeters <= 0.0
        || !std::isfinite(angularDeflectionRadians) || angularDeflectionRadians <= 0.0) {
        throw std::invalid_argument("STL mesh accuracy must be positive.");
    }
    TopoDS_Shape shape = BuildBodyShape(body);
    RequireExportable(shape, body);
    BRepMesh_IncrementalMesh mesh(
        shape, linearDeflectionMillimeters, false, angularDeflectionRadians, true);
    mesh.Perform();
    if (!mesh.IsDone()) {
        throw std::runtime_error("Could not generate the temporary STL mesh.");
    }

    const std::filesystem::path temporary = TemporaryExportPath(".stl");
    StlAPI_Writer writer;
    if (!writer.Write(shape, temporary.string().c_str())) {
        throw std::runtime_error("Could not write the STL file.");
    }
    PublishTemporaryFile(temporary, path);
}

void WriteBodyStep(
    const std::filesystem::path& path,
    const model::Body& body)
{
    const TopoDS_Shape shape = BuildBodyShape(body);
    RequireExportable(shape, body);

    STEPControl_Writer writer;
    if (writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone) {
        throw std::runtime_error("Could not transfer the jig to STEP.");
    }
    const std::filesystem::path temporary = TemporaryExportPath(".step");
    if (writer.Write(temporary.string().c_str()) != IFSelect_RetDone) {
        throw std::runtime_error("Could not write the STEP file.");
    }
    PublishTemporaryFile(temporary, path);
}

ModelShapeAnalysis AnalyzeModelShape(
    const model::Project& project,
    const ModelShapeSelection& selection,
    double requiredMinimumWallMillimeters)
{
    return InspectModelShape(
        BuildModelShape(project, selection), requiredMinimumWallMillimeters);
}

void WriteModelStl(
    const std::filesystem::path& path,
    const model::Project& project,
    const ModelShapeSelection& selection,
    double linearDeflectionMillimeters,
    double angularDeflectionRadians)
{
    if (!std::isfinite(linearDeflectionMillimeters) || linearDeflectionMillimeters <= 0.0
        || !std::isfinite(angularDeflectionRadians) || angularDeflectionRadians <= 0.0) {
        throw std::invalid_argument("STL mesh accuracy must be positive.");
    }
    const BuiltModelShape built = BuildModelShape(project, selection);
    RequireModelExportable(built);
    BRepMesh_IncrementalMesh mesh(
        built.shape, linearDeflectionMillimeters, false, angularDeflectionRadians, true);
    mesh.Perform();
    if (!mesh.IsDone()) {
        throw std::runtime_error("Could not generate the temporary STL mesh.");
    }
    const std::filesystem::path temporary = TemporaryExportPath(".stl");
    StlAPI_Writer writer;
    if (!writer.Write(built.shape, temporary.string().c_str())) {
        throw std::runtime_error("Could not write the STL file.");
    }
    PublishTemporaryFile(temporary, path);
}

void WriteModelStep(
    const std::filesystem::path& path,
    const model::Project& project,
    const ModelShapeSelection& selection)
{
    const BuiltModelShape built = BuildModelShape(project, selection);
    RequireModelExportable(built);
    STEPControl_Writer writer;
    if (writer.Transfer(built.shape, STEPControl_AsIs) != IFSelect_RetDone) {
        throw std::runtime_error("Could not transfer the selected 3D model to STEP.");
    }
    const std::filesystem::path temporary = TemporaryExportPath(".step");
    if (writer.Write(temporary.string().c_str()) != IFSelect_RetDone) {
        throw std::runtime_error("Could not write the STEP file.");
    }
    PublishTemporaryFile(temporary, path);
}

} // namespace kachakacha::occt
