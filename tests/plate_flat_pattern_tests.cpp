#include "kachakacha/io/PlateFlatPattern.h"
#include "kachakacha/io/ProjectScript.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using kachakacha::geometry::Vector2;
using kachakacha::io::BuildPlateFlatPattern;
using kachakacha::io::LoadProjectScript;
using kachakacha::io::PlateFlatPatternOptions;
using kachakacha::io::PlateFlatPattern;
using kachakacha::io::WritePlateFlatPatternDxf;
using kachakacha::io::WritePlateFlatPatternSvg;
using kachakacha::model::PlateDevelopability;
using kachakacha::model::PlateSplitAxis;
using kachakacha::model::PlateThicknessDirection;
using kachakacha::model::Project;
using kachakacha::model::Wire;

namespace {

constexpr double kPi = 3.14159265358979323846;

void Require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::pair<double, double> Extents(const PlateFlatPattern& pattern)
{
    double minimumX = pattern.outerBoundary.points.front().x;
    double maximumX = minimumX;
    double minimumY = pattern.outerBoundary.points.front().y;
    double maximumY = minimumY;
    for (const Vector2 point : pattern.outerBoundary.points) {
        minimumX = std::min(minimumX, point.x);
        maximumX = std::max(maximumX, point.x);
        minimumY = std::min(minimumY, point.y);
        maximumY = std::max(maximumY, point.y);
    }
    return {maximumX - minimumX, maximumY - minimumY};
}

Project MakePlanarProject()
{
    Project project;
    project.AddWire("outline", Wire::Polyline({
        {0.0, 0.0, 0.0},
        {20.0, 0.0, 0.0},
        {20.0, 10.0, 0.0},
        {0.0, 10.0, 0.0},
        {0.0, 0.0, 0.0},
    }));
    project.AddWire("light_plan", Wire::Circle(
        {10.0, 5.0, 5.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 2.0));
    project.AddPlanarSurface("panel", "outline");
    project.AddProjectedWire("light_on_panel", "light_plan", "panel", {0.0, 0.0, -1.0});
    project.AddPlate("panel_plate", "panel", 0.5, PlateThicknessDirection::Centered, "styrene");
    project.AddPlateOpening("panel_plate", "light_on_panel");
    return project;
}

Project MakeCylinderProject()
{
    Project project;
    project.AddWire("section_a", Wire::Circle(
        {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, 8.0));
    project.AddWire("section_b", Wire::Circle(
        {20.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, 8.0));
    project.AddWire("light_plan", Wire::Circle(
        {10.0, 0.0, 12.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 1.25));
    project.AddRuledSurface("shell", "section_a", "section_b");
    project.AddProjectedWire("light_on_shell", "light_plan", "shell", {0.0, 0.0, -1.0});
    project.AddPlate("shell_plate", "shell", 0.5, PlateThicknessDirection::Centered, "styrene");
    project.AddPlateOpening("shell_plate", "light_on_shell");
    return project;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const Project planarProject = MakePlanarProject();
        const PlateFlatPattern planar = BuildPlateFlatPattern(planarProject, planarProject.Plates().front());
        const auto [planarWidth, planarHeight] = Extents(planar);
        Require(std::abs(planarWidth - 20.0) <= 1.0e-9, "planar pattern preserves width");
        Require(std::abs(planarHeight - 10.0) <= 1.0e-9, "planar pattern preserves height");
        Require(planar.analysis.classification == PlateDevelopability::Planar, "planar pattern is exact");
        Require(planar.analysis.MaximumEstimatedErrorMillimeters() <= 1.0e-12, "planar pattern has zero estimated error");
        Require(planar.openings.size() == 1 && planar.openings.front().points.size() > 40,
            "planar light opening is exported");

        std::ostringstream svg;
        WritePlateFlatPatternSvg(svg, planar);
        Require(svg.str().find("width=\"30.000000mm\"") != std::string::npos, "flat SVG preserves 1:1 millimeter width");
        Require(svg.str().find("CUT_OUTER") != std::string::npos, "flat SVG has outer cutting layer");
        Require(svg.str().find("CUT_OPENING") != std::string::npos, "flat SVG has opening cutting layer");

        std::ostringstream dxf;
        WritePlateFlatPatternDxf(dxf, planar);
        Require(dxf.str().find("$INSUNITS\n70\n4") != std::string::npos, "flat DXF declares millimeters");
        Require(dxf.str().find("CUT_OUTER") != std::string::npos, "flat DXF has outer cutting layer");
        Require(dxf.str().find("CUT_OPENING") != std::string::npos, "flat DXF has opening cutting layer");

        Project cylinderProject = MakeCylinderProject();
        const PlateFlatPattern cylinder = BuildPlateFlatPattern(cylinderProject, cylinderProject.Plates().front());
        const auto [cylinderWidth, cylinderHeight] = Extents(cylinder);
        const double largerExtent = std::max(cylinderWidth, cylinderHeight);
        const double smallerExtent = std::min(cylinderWidth, cylinderHeight);
        Require(std::abs(largerExtent - 16.0 * kPi) < 0.02, "cylinder develops to its circumference");
        Require(std::abs(smallerExtent - 20.0) < 0.02, "cylinder develops to its axial length");
        Require(cylinder.analysis.classification == PlateDevelopability::Developable, "cylinder is classified as developable");
        Require(cylinder.analysis.maximumEdgeDistortionMillimeters < 1.0e-6, "cylinder development has negligible distortion");
        Require(cylinder.openings.size() == 1 && cylinder.openings.front().points.size() > 40,
            "projected cylinder opening is carried into pattern");

        cylinderProject.SplitPlate("shell_plate", PlateSplitAxis::U, 0.125, "shell_eighth", "shell_rest");
        const PlateFlatPattern eighth = BuildPlateFlatPattern(cylinderProject, cylinderProject.Plates().front());
        const auto [eighthWidth, eighthHeight] = Extents(eighth);
        const double eighthLarger = std::max(eighthWidth, eighthHeight);
        const double eighthSmaller = std::min(eighthWidth, eighthHeight);
        Require(std::abs(eighthLarger - 20.0) < 0.02, "split cylinder keeps axial length");
        Require(std::abs(eighthSmaller - 2.0 * kPi) < 0.02, "split cylinder keeps eighth circumference");
        Require(eighth.openings.empty(), "opening stays with the containing split piece");

        Project twisted;
        twisted.AddWire("first", Wire::Line({0.0, -5.0, 0.0}, {0.0, 5.0, 0.0}));
        twisted.AddWire("second", Wire::Line({10.0, -5.0, -4.0}, {10.0, 5.0, 4.0}));
        twisted.AddRuledSurface("twisted", "first", "second");
        twisted.AddPlate("twisted_plate", "twisted", 0.5, PlateThicknessDirection::Centered, "styrene");
        const PlateFlatPattern twistedPattern = BuildPlateFlatPattern(twisted, twisted.Plates().front());
        Require(twistedPattern.analysis.classification == PlateDevelopability::DoubleCurved,
            "double-curved sheet remains an explicit warning case");
        Require(std::isfinite(twistedPattern.analysis.MaximumEstimatedErrorMillimeters()),
            "double-curved development reports a finite error");

        if (argc >= 2) {
            std::ifstream sampleInput(argv[1]);
            Require(static_cast<bool>(sampleInput), "curved panel sample opens");
            const Project sample = LoadProjectScript(sampleInput, argv[1]);
            Require(sample.Plates().size() == 2, "curved panel sample contains two split plates");
            PlateFlatPatternOptions sampleOptions;
            sampleOptions.uSegments = 64;
            sampleOptions.vSegments = 24;
            sampleOptions.openingSamples = 64;
            for (const auto& plate : sample.Plates()) {
                const PlateFlatPattern pattern = BuildPlateFlatPattern(sample, plate, sampleOptions);
                Require(pattern.openings.size() == 1, "each sample plate keeps its light opening");
                Require(pattern.outerBoundary.points.size() > 100, "each sample plate has a usable developed boundary");
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "plate_flat_pattern_tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "plate flat-pattern tests passed\n";
    return EXIT_SUCCESS;
}
