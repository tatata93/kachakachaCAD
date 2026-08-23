#include "kachakacha/occt/BodyExport.h"

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <Geom_BSplineCurve.hxx>
#include <STEPControl_StepModelType.hxx>
#include <STEPControl_Writer.hxx>
#include <StlAPI_Writer.hxx>
#include <NCollection_HArray1.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeFix_Solid.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace kachakacha::occt {

namespace {

constexpr double kModelTolerance = 1.0e-6;
constexpr int kSectionSamples = 13;
constexpr int kCurveSamples = 33;

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

} // namespace kachakacha::occt
