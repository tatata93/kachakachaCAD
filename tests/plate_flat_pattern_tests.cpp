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
using kachakacha::geometry::Vector3;
using kachakacha::io::BuildPlateFlatPattern;
using kachakacha::io::BuildPlateAssemblyGuide;
using kachakacha::io::BuildPlateAssemblyApproximation;
using kachakacha::io::BuildPlateAssemblyMotion;
using kachakacha::io::AddPlateAssemblyMotionModel;
using kachakacha::io::AddPlateFlatPatternModel;
using kachakacha::io::LoadProjectScript;
using kachakacha::io::PapercraftCutDirection;
using kachakacha::io::PlateAssemblyStrategy;
using kachakacha::io::PlateFlatPatternOptions;
using kachakacha::io::PlateFlatPattern;
using kachakacha::io::ReliefNotchStyle;
using kachakacha::io::WritePlateFlatPatternDxf;
using kachakacha::io::WritePlateFlatPatternSvg;
using kachakacha::model::PlateDevelopability;
using kachakacha::model::PlateSplitAxis;
using kachakacha::model::PlateThicknessDirection;
using kachakacha::model::Project;
using kachakacha::model::Wire;
using kachakacha::model::WorkPlane;

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
    project.AddWire("relief_plan", Wire::Line({0.0, 5.0, 5.0}, {8.0, 5.0, 5.0}));
    project.AddPlanarSurface("panel", "outline");
    project.AddProjectedWire("light_on_panel", "light_plan", "panel", {0.0, 0.0, -1.0});
    project.AddProjectedWire("relief_on_panel", "relief_plan", "panel", {0.0, 0.0, -1.0});
    project.AddPlate("panel_plate", "panel", 0.5, PlateThicknessDirection::Centered, "styrene");
    project.AddPlateOpening("panel_plate", "light_on_panel");
    project.AddPlateReliefCut("panel_plate", "relief_on_panel");
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
        Require(planar.reliefCuts.size() == 1 && planar.reliefCuts.front().points.size() >= 2,
            "manual planar relief cut is exported");
        const auto planarAssemblyGuide = BuildPlateAssemblyGuide(
            planarProject, planarProject.Plates().front());
        Require(planarAssemblyGuide.foldLines.empty(),
            "planar plate has no assembled fold guides");
        Require(planarAssemblyGuide.reliefCuts.size() == 1,
            "manual relief cut appears on assembled plate");
        Require(std::abs(planarAssemblyGuide.reliefCuts.front().points.front().z - 0.25) <= 1.0e-9,
            "assembled relief guide is drawn on the plate outer face");

        std::ostringstream svg;
        WritePlateFlatPatternSvg(svg, planar);
        Require(svg.str().find("width=\"30.000000mm\"") != std::string::npos, "flat SVG preserves 1:1 millimeter width");
        Require(svg.str().find("CUT_OUTER") != std::string::npos, "flat SVG has outer cutting layer");
        Require(svg.str().find("CUT_OPENING") != std::string::npos, "flat SVG has opening cutting layer");
        Require(svg.str().find("RELIEF_CUT") != std::string::npos, "flat SVG has relief cutting layer");
        Require(svg.str().find("FOLD") != std::string::npos, "flat SVG has fold layer");

        std::ostringstream dxf;
        WritePlateFlatPatternDxf(dxf, planar);
        Require(dxf.str().find("$INSUNITS\n70\n4") != std::string::npos, "flat DXF declares millimeters");
        Require(dxf.str().find("CUT_OUTER") != std::string::npos, "flat DXF has outer cutting layer");
        Require(dxf.str().find("CUT_OPENING") != std::string::npos, "flat DXF has opening cutting layer");
        Require(dxf.str().find("RELIEF_CUT") != std::string::npos, "flat DXF has relief cutting layer");

        Project developedProject = planarProject;
        const auto flatModel = AddPlateFlatPatternModel(
            developedProject,
            planarProject.Plates().front(),
            planar,
            WorkPlane::FromPointNormal({0.0, 0.0, 20.0}, {0.0, 0.0, 1.0}),
            "developed_test",
            0.2);
        Require(developedProject.FindPlate(flatModel.plateName).has_value(),
            "flat pattern creates a 3D plate model");
        Require(!flatModel.reliefCutWireNames.empty(), "flat pattern creates editable relief-cut wires");
        Require(flatModel.openingWireNames.size() == 2,
            "flat pattern creates original opening and physical relief slot");

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

        cylinderProject.AddWire("manual_split_plan", Wire::Line(
            {0.0, 4.0, 12.0}, {20.0, 4.0, 12.0}));
        cylinderProject.AddProjectedWire(
            "manual_split_on_shell", "manual_split_plan", "shell", {0.0, 0.0, -1.0});
        cylinderProject.RemovePlateOpening("shell_plate", "light_on_shell");
        cylinderProject.AddPlateSplitLine("shell_plate", "manual_split_on_shell");
        PlateFlatPatternOptions manualCylinderOptions;
        manualCylinderOptions.papercraftFidelity = 10;
        const PlateFlatPattern manuallySplitCylinder = BuildPlateFlatPattern(
            cylinderProject, cylinderProject.Plates().front(), manualCylinderOptions);
        Require(manuallySplitCylinder.analysis.pieceCount == 2,
            "one full manual split line creates two pieces on a closed developable surface");
        cylinderProject.RemovePlateSplitLine("shell_plate", "manual_split_on_shell");
        cylinderProject.AddPlateOpening("shell_plate", "light_on_shell");

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
        twisted.AddWire("manual_split_plan", Wire::Line(
            {0.0, -4.0, 20.0}, {10.0, 4.0, 20.0}));
        twisted.AddProjectedWire(
            "manual_split_on_twisted",
            "manual_split_plan",
            "twisted",
            {0.0, 0.0, -1.0});
        twisted.AddPlate("twisted_plate", "twisted", 0.5, PlateThicknessDirection::Centered, "styrene");
        twisted.AddPlateSplitLine("twisted_plate", "manual_split_on_twisted");
        PlateFlatPatternOptions twistedOptions;
        twistedOptions.includeAutomaticReliefCuts = true;
        twistedOptions.foldSpacingMillimeters = 2.0;
        twistedOptions.minimumFoldAngleDegrees = 0.1;
        const PlateFlatPattern twistedPattern = BuildPlateFlatPattern(
            twisted, twisted.Plates().front(), twistedOptions);
        Require(twistedPattern.analysis.classification == PlateDevelopability::DoubleCurved,
            "double-curved sheet remains an explicit warning case");
        Require(std::isfinite(twistedPattern.analysis.MaximumEstimatedErrorMillimeters()),
            "double-curved development reports a finite error");
        Require(!twistedPattern.foldLines.empty(), "curved development creates fold wires");
        Require(twistedPattern.analysis.pieceCount > 1,
            "double-curved development creates separate papercraft pieces");
        Require(twisted.Plates().front().splitWireNames
                == std::vector<std::string>{"manual_split_on_twisted"},
            "projected wire remains an editable manual split relation");
        Require(twistedPattern.pieces.size()
                == static_cast<std::size_t>(twistedPattern.analysis.pieceCount),
            "papercraft piece count matches exported outer boundaries");
        Require(std::all_of(
            twistedPattern.pieces.begin(),
            twistedPattern.pieces.end(),
            [](const auto& piece) {
                return piece.outerBoundary.points.size() >= 4
                    && std::hypot(
                        piece.outerBoundary.points.front().x - piece.outerBoundary.points.back().x,
                        piece.outerBoundary.points.front().y - piece.outerBoundary.points.back().y) < 1.0e-9;
            }),
            "every papercraft piece has a closed cutting boundary");
        const auto twistedAssemblyGuide = BuildPlateAssemblyGuide(
            twisted, twisted.Plates().front(), twistedOptions);
        Require(!twistedAssemblyGuide.splitLines.empty(),
            "assembled preview shows full papercraft split lines");
        Require(std::all_of(
            twistedAssemblyGuide.foldLines.begin(),
            twistedAssemblyGuide.foldLines.end(),
            [](const auto& path) { return path.points.size() >= 3; }),
            "assembled fold guides contain usable 3D paths");

        PlateFlatPatternOptions coarseOptions = twistedOptions;
        coarseOptions.papercraftFidelity = 1;
        PlateFlatPatternOptions fineOptions = twistedOptions;
        fineOptions.papercraftFidelity = 10;
        const PlateFlatPattern coarsePattern = BuildPlateFlatPattern(
            twisted, twisted.Plates().front(), coarseOptions);
        const PlateFlatPattern finePattern = BuildPlateFlatPattern(
            twisted, twisted.Plates().front(), fineOptions);
        Require(finePattern.pieces.size() > coarsePattern.pieces.size(),
            "higher papercraft fidelity creates more, narrower pieces");
        Require(finePattern.analysis.MaximumEstimatedErrorMillimeters()
                < coarsePattern.analysis.MaximumEstimatedErrorMillimeters(),
            "higher papercraft fidelity lowers the faceting estimate");
        Require(twistedPattern.analysis.automaticNotchCount > 0,
            "split papercraft pieces receive local automatic V notches");
        const auto coarseAssembly = BuildPlateAssemblyApproximation(
            twisted, twisted.Plates().front(), coarseOptions);
        const auto fineAssembly = BuildPlateAssemblyApproximation(
            twisted, twisted.Plates().front(), fineOptions);
        Require(fineAssembly.panels.size() > coarseAssembly.panels.size(),
            "higher fidelity creates more assembled approximation panels");
        Require(fineAssembly.maximumDeviationMillimeters
                < coarseAssembly.maximumDeviationMillimeters,
            "higher fidelity lowers assembled approximation deviation");

        Project notchedTwisted = twisted;
        notchedTwisted.RemovePlateSplitLine("twisted_plate", "manual_split_on_twisted");
        PlateFlatPatternOptions notchOptions = twistedOptions;
        notchOptions.assemblyStrategy = PlateAssemblyStrategy::SingleSheet;
        notchOptions.cutDirection = PapercraftCutDirection::Vertical;
        notchOptions.notchStyle = ReliefNotchStyle::CurvedV;
        notchOptions.papercraftFidelity = 9;
        notchOptions.reliefCutDepthRatio = 0.55;
        notchOptions.reliefNotchAngleDegrees = 20.0;
        notchOptions.reliefNotchCurveStrength = 1.0;
        const PlateFlatPattern curvedNotchPattern = BuildPlateFlatPattern(
            notchedTwisted, notchedTwisted.Plates().front(), notchOptions);
        Require(curvedNotchPattern.pieces.size() > 1,
            "an impossible double-curved single sheet falls back to buildable pieces");
        Require(curvedNotchPattern.analysis.maximumEdgeDistortionMillimeters < 1.0e-7,
            "fallback pieces preserve every faceted edge length");
        Require(curvedNotchPattern.analysis.automaticNotchCount > 0,
            "curved V notch development creates automatic edge notches");
        PlateFlatPatternOptions notchPreviewOptions = notchOptions;
        notchPreviewOptions.includeOpenings = false;
        const PlateFlatPattern notchPreviewPattern = BuildPlateFlatPattern(
            notchedTwisted, notchedTwisted.Plates().front(), notchPreviewOptions);
        Require(notchPreviewPattern.analysis.automaticNotchCount
                == curvedNotchPattern.analysis.automaticNotchCount,
            "preview-only opening visibility does not change protected notch placement");
        Require(std::all_of(
            curvedNotchPattern.reliefCuts.begin(),
            curvedNotchPattern.reliefCuts.end(),
            [](const auto& path) {
                return path.incorporatedInOuterBoundary && path.points.size() > 3;
            }),
            "shape-following V arms are curved and incorporated into the cutting boundary");
        std::ostringstream curvedNotchSvg;
        WritePlateFlatPatternSvg(curvedNotchSvg, curvedNotchPattern, notchOptions);
        const std::string curvedSvgText = curvedNotchSvg.str();
        const std::size_t reliefLayer = curvedSvgText.find("id=\"RELIEF_CUT\"");
        const std::size_t reliefLayerEnd = curvedSvgText.find("</g>", reliefLayer);
        Require(reliefLayer != std::string::npos && reliefLayerEnd != std::string::npos
                && curvedSvgText.find("<polyline", reliefLayer) > reliefLayerEnd,
            "outer-boundary V notches are not duplicated on the SVG relief layer");
        std::ostringstream curvedNotchDxf;
        WritePlateFlatPatternDxf(curvedNotchDxf, curvedNotchPattern);
        Require(curvedNotchDxf.str().find("RELIEF_CUT") == std::string::npos,
            "outer-boundary V notches are not duplicated in the DXF relief layer");

        PlateFlatPatternOptions plainNotchOptions = notchOptions;
        plainNotchOptions.notchStyle = ReliefNotchStyle::SharpV;
        const PlateFlatPattern plainNotchPattern = BuildPlateFlatPattern(
            notchedTwisted, notchedTwisted.Plates().front(), plainNotchOptions);
        Require(std::all_of(
            plainNotchPattern.reliefCuts.begin(),
            plainNotchPattern.reliefCuts.end(),
            [](const auto& path) { return path.points.size() == 3; }),
            "plain V notches retain a sharp three-point cutting profile");

        PlateFlatPatternOptions denseNotchOptions = notchOptions;
        denseNotchOptions.papercraftFidelity = 10;
        const PlateFlatPattern denseNotchPattern = BuildPlateFlatPattern(
            notchedTwisted, notchedTwisted.Plates().front(), denseNotchOptions);
        Require(denseNotchPattern.analysis.automaticNotchCount
                >= curvedNotchPattern.analysis.automaticNotchCount,
            "higher fidelity never reduces curved-corner reproduction density");

        const auto notchedAssemblyGuide = BuildPlateAssemblyGuide(
            notchedTwisted, notchedTwisted.Plates().front(), notchOptions);
        Require(!notchedAssemblyGuide.splitLines.empty()
                && notchedAssemblyGuide.reliefCuts.size()
                    == static_cast<std::size_t>(curvedNotchPattern.analysis.automaticNotchCount),
            "assembled preview distinguishes required split lines from edge notches");

        Project notchedDeveloped = notchedTwisted;
        const auto notchedFlatModel = AddPlateFlatPatternModel(
            notchedDeveloped,
            notchedTwisted.Plates().front(),
            curvedNotchPattern,
            WorkPlane::FromPointNormal({0.0, 0.0, 45.0}, {0.0, 0.0, 1.0}),
            "curved_notch_developed");
        Require(notchedFlatModel.plateNames.size() == curvedNotchPattern.pieces.size(),
            "fallback development creates every editable 3D plate piece");
        Require(notchedFlatModel.reliefCutWireNames.size()
                == static_cast<std::size_t>(curvedNotchPattern.analysis.automaticNotchCount),
            "curved V notch edges remain editable CAD wires");
        Require(notchedFlatModel.openingWireNames.size() == curvedNotchPattern.openings.size(),
            "outer-boundary V notches are not duplicated as invalid internal slots");

        Project twistedDeveloped = twisted;
        const auto twistedFlatModel = AddPlateFlatPatternModel(
            twistedDeveloped,
            twisted.Plates().front(),
            twistedPattern,
            WorkPlane::FromPointNormal({0.0, 0.0, 30.0}, {0.0, 0.0, 1.0}),
            "twisted_developed");
        Require(twistedFlatModel.plateNames.size() == twistedPattern.pieces.size(),
            "each papercraft piece creates an independent editable 3D plate");

        PlateFlatPatternOptions verticalOptions = notchOptions;
        verticalOptions.assemblyStrategy = PlateAssemblyStrategy::SplitPieces;
        verticalOptions.allowAutomaticNotches = false;
        verticalOptions.cutDirection = PapercraftCutDirection::Vertical;
        PlateFlatPatternOptions horizontalOptions = verticalOptions;
        horizontalOptions.cutDirection = PapercraftCutDirection::Horizontal;
        PlateFlatPatternOptions bothOptions = verticalOptions;
        bothOptions.cutDirection = PapercraftCutDirection::Both;
        const auto verticalPattern = BuildPlateFlatPattern(
            notchedTwisted, notchedTwisted.Plates().front(), verticalOptions);
        const auto horizontalPattern = BuildPlateFlatPattern(
            notchedTwisted, notchedTwisted.Plates().front(), horizontalOptions);
        const auto bothPattern = BuildPlateFlatPattern(
            notchedTwisted, notchedTwisted.Plates().front(), bothOptions);
        Require(verticalPattern.pieces.size() > 1 && horizontalPattern.pieces.size() > 1,
            "vertical-only and horizontal-only cut layouts both create real pieces");
        Require(bothPattern.pieces.size() > verticalPattern.pieces.size()
                && bothPattern.pieces.size() > horizontalPattern.pieces.size(),
            "combined vertical-horizontal cuts create the highest fidelity tile layout");
        Require(verticalPattern.analysis.maximumEdgeDistortionMillimeters < 1.0e-7
                && horizontalPattern.analysis.maximumEdgeDistortionMillimeters < 1.0e-7
                && bothPattern.analysis.maximumEdgeDistortionMillimeters < 1.0e-7,
            "all three cut directions unfold without stretching triangle edges");

        const auto flatMotion = BuildPlateAssemblyMotion(
            notchedTwisted, notchedTwisted.Plates().front(), 0.0, verticalOptions);
        const auto halfMotion = BuildPlateAssemblyMotion(
            notchedTwisted, notchedTwisted.Plates().front(), 0.5, verticalOptions);
        const auto assembledMotion = BuildPlateAssemblyMotion(
            notchedTwisted, notchedTwisted.Plates().front(), 1.0, verticalOptions);
        Require(flatMotion.panels.size() == halfMotion.panels.size()
                && halfMotion.panels.size() == assembledMotion.panels.size()
                && flatMotion.panels.size()
                    == flatMotion.panelThicknessMillimeters.size()
                && flatMotion.panels.size()
                    == flatMotion.panelDeviationMillimeters.size()
                && flatMotion.pieceCount == verticalPattern.analysis.pieceCount,
            "assembly slider keeps the same physical panels and piece count");
        Require(flatMotion.maximumTargetMismatchMillimeters > 0.1
                && assembledMotion.maximumTargetMismatchMillimeters < 1.0e-6,
            "assembly slider starts flat and closes exactly onto the faceted source model");
        const auto edgeLength = [](const std::array<Vector3, 3>& panel, int edge) {
            return (panel[static_cast<std::size_t>((edge + 1) % 3)]
                - panel[static_cast<std::size_t>(edge)]).Length();
        };
        for (std::size_t panel = 0; panel < flatMotion.panels.size(); ++panel) {
            for (int edge = 0; edge < 3; ++edge) {
                Require(std::abs(edgeLength(flatMotion.panels[panel], edge)
                        - edgeLength(halfMotion.panels[panel], edge)) < 1.0e-7
                        && std::abs(edgeLength(flatMotion.panels[panel], edge)
                            - edgeLength(assembledMotion.panels[panel], edge)) < 1.0e-7,
                    "folding animation preserves rigid panel edge lengths");
            }
        }

        Project allMotionModel = notchedTwisted;
        const auto allMotionResult = AddPlateAssemblyMotionModel(
            allMotionModel,
            notchedTwisted.Plates().front(),
            halfMotion,
            "half_fold_all");
        Require(allMotionResult.plateNames.size() == halfMotion.panels.size(),
            "arbitrary slider state creates one editable solid plate per rigid panel");
        const int chosenPiece = halfMotion.pieceIndices.front();
        Project selectedMotionModel = notchedTwisted;
        const auto selectedMotionResult = AddPlateAssemblyMotionModel(
            selectedMotionModel,
            notchedTwisted.Plates().front(),
            halfMotion,
            "half_fold_selected",
            chosenPiece);
        Require(!selectedMotionResult.plateNames.empty()
                && selectedMotionResult.plateNames.size() < allMotionResult.plateNames.size()
                && std::all_of(
                    selectedMotionResult.pieceIndices.begin(),
                    selectedMotionResult.pieceIndices.end(),
                    [&](int piece) { return piece == chosenPiece; }),
            "a selected split piece creates and exports only its own rigid panels");

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
        if (argc >= 3) {
            std::ifstream noseInput(argv[2]);
            Require(static_cast<bool>(noseInput), "railway nose sample opens");
            const Project nose = LoadProjectScript(noseInput, argv[2]);
            const auto nosePlate = std::find_if(
                nose.Plates().begin(), nose.Plates().end(), [](const auto& plate) {
                    return plate.name == "nose_panel_front";
                });
            Require(nosePlate != nose.Plates().end(), "railway nose sample has front plate");
            PlateFlatPatternOptions noseOptions;
            noseOptions.includeAutomaticReliefCuts = true;
            noseOptions.assemblyStrategy = PlateAssemblyStrategy::SingleSheet;
            noseOptions.cutDirection = PapercraftCutDirection::Vertical;
            noseOptions.allowAutomaticNotches = true;
            noseOptions.notchStyle = ReliefNotchStyle::CurvedV;
            noseOptions.reliefCutDepthRatio = 0.55;
            const PlateFlatPattern oneSheetNose = BuildPlateFlatPattern(
                nose, *nosePlate, noseOptions);
            Require(oneSheetNose.analysis.automaticNotchCount > 0,
                "railway nose edge fillets create shape-following curved cuts");
            noseOptions.assemblyStrategy = PlateAssemblyStrategy::SplitPieces;
            const PlateFlatPattern splitNose = BuildPlateFlatPattern(
                nose, *nosePlate, noseOptions);
            Require(splitNose.analysis.pieceCount > 1,
                "railway nose can be developed as separate glued pieces");
            Require(splitNose.analysis.automaticNotchCount > 0,
                "railway nose split pieces retain needed curved cuts");
            Require(splitNose.openings.size() >= nosePlate->openingWireNames.size(),
                "railway nose openings are retained or divided across split pieces");
            noseOptions.cutDirection = PapercraftCutDirection::Both;
            noseOptions.allowAutomaticNotches = false;
            const PlateFlatPattern tiledNose = BuildPlateFlatPattern(
                nose, *nosePlate, noseOptions);
            Require(tiledNose.analysis.pieceCount > splitNose.analysis.pieceCount,
                "railway nose supports combined vertical-horizontal panel division");
            Require(tiledNose.openings.size() >= nosePlate->openingWireNames.size(),
                "windows remain present when they cross a two-direction panel grid");
            const auto openedMotion = BuildPlateAssemblyMotion(
                nose, *nosePlate, 0.65, noseOptions);
            PlateFlatPatternOptions closedMotionOptions = noseOptions;
            closedMotionOptions.includeOpenings = false;
            const auto uncutMotion = BuildPlateAssemblyMotion(
                nose, *nosePlate, 0.65, closedMotionOptions);
            Require(openedMotion.panels.size() < uncutMotion.panels.size(),
                "assembly-state 3D output removes panels covered by light and window openings");
        }
    } catch (const std::exception& error) {
        std::cerr << "plate_flat_pattern_tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "plate flat-pattern tests passed\n";
    return EXIT_SUCCESS;
}
