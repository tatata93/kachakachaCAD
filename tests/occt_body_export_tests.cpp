#include "kachakacha/occt/BodyExport.h"
#include "kachakacha/io/BentSheetPapercraft.h"
#include "kachakacha/io/FacetedPapercraft.h"
#include "kachakacha/io/PlateFlatPattern.h"
#include "kachakacha/io/ProjectScript.h"
#include "kachakacha/model/Body.h"
#include "kachakacha/model/Surface.h"
#include "kachakacha/model/Wire.h"

#include <BRepCheck_Analyzer.hxx>
#include <STEPControl_Reader.hxx>
#include <StlAPI_Reader.hxx>
#include <TopoDS_Shape.hxx>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using kachakacha::geometry::Vector3;
using kachakacha::model::Body;
using kachakacha::model::JigSide;
using kachakacha::model::PlateThicknessDirection;
using kachakacha::model::Project;
using kachakacha::model::Surface;
using kachakacha::model::Wire;
using kachakacha::model::WorkPlane;

namespace {

void VerifyExports(
    const std::filesystem::path& outputDirectory,
    const std::string& prefix,
    const Body& body)
{
    const auto analysis = kachakacha::occt::AnalyzeBodyShape(body, 1.2);
    if (!analysis.validBRep || !analysis.closedSolid || !analysis.meetsMinimumWall
        || analysis.volumeCubicMillimeters <= 1.0) {
        throw std::runtime_error(
            prefix + " OCCT jig analysis did not produce a printable solid: " + analysis.message);
    }

    const std::filesystem::path stlPath = outputDirectory / (prefix + ".stl");
    const std::filesystem::path stepPath = outputDirectory / (prefix + ".step");
    kachakacha::occt::WriteBodyStl(stlPath, body);
    kachakacha::occt::WriteBodyStep(stepPath, body);
    if (!std::filesystem::exists(stlPath) || std::filesystem::file_size(stlPath) < 1024
        || !std::filesystem::exists(stepPath) || std::filesystem::file_size(stepPath) < 1024) {
        throw std::runtime_error(prefix + " OCCT body exports are unexpectedly small.");
    }

    TopoDS_Shape stlShape;
    StlAPI_Reader stlReader;
    stlReader.Read(stlShape, stlPath.string().c_str());
    if (stlShape.IsNull()) {
        throw std::runtime_error(prefix + " exported STL cannot be read back.");
    }
    STEPControl_Reader stepReader;
    if (stepReader.ReadFile(stepPath.string().c_str()) != IFSelect_RetDone
        || stepReader.TransferRoots() <= 0) {
        throw std::runtime_error(prefix + " exported STEP cannot be read back.");
    }
    const TopoDS_Shape stepShape = stepReader.OneShape();
    if (stepShape.IsNull() || !BRepCheck_Analyzer(stepShape, true).IsValid()) {
        throw std::runtime_error(prefix + " exported STEP did not preserve valid boundary geometry.");
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "expected output directory and acceptance project\n";
        return 2;
    }
    try {
        const Surface surface = Surface::Loft({
            Wire::CubicBSpline({
                Vector3{-16.0, 0.0, -7.0}, Vector3{-12.0, 0.0, 4.0},
                Vector3{0.0, 0.0, 11.0}, Vector3{12.0, 0.0, 4.0}, Vector3{16.0, 0.0, -7.0}}),
            Wire::CubicBSpline({
                Vector3{-15.0, 8.0, -6.0}, Vector3{-11.0, 8.0, 5.0},
                Vector3{0.0, 8.0, 12.5}, Vector3{11.0, 8.0, 5.0}, Vector3{15.0, 8.0, -6.0}}),
            Wire::CubicBSpline({
                Vector3{-13.0, 18.0, -4.0}, Vector3{-9.0, 18.0, 5.0},
                Vector3{0.0, 18.0, 10.0}, Vector3{9.0, 18.0, 5.0}, Vector3{13.0, 18.0, -4.0}}),
        });
        const Body body = Body::SurfaceJig(surface, {}, JigSide::Negative, 0.15, 3.0);
        const std::filesystem::path outputDirectory = argv[1];
        std::filesystem::create_directories(outputDirectory);

        const Body positiveBody = Body::SurfaceJig(
            surface, {}, JigSide::Positive, 0.2, 2.5);
        const auto positiveAnalysis = kachakacha::occt::AnalyzeBodyShape(positiveBody, 1.2);
        if (!positiveAnalysis.validBRep || !positiveAnalysis.closedSolid
            || !positiveAnalysis.meetsMinimumWall || positiveAnalysis.volumeCubicMillimeters <= 1.0) {
            throw std::runtime_error(
                "Positive-side jig did not produce a printable solid: " + positiveAnalysis.message);
        }

        Project variablePlateProject;
        variablePlateProject.AddWire("section_a", Wire::Line(
            {0.0, -5.0, 0.0}, {0.0, 5.0, 0.0}));
        variablePlateProject.AddWire("section_b", Wire::Line(
            {12.0, -5.0, 2.0}, {12.0, 5.0, 2.0}));
        variablePlateProject.AddRuledSurface("panel", "section_a", "section_b");
        variablePlateProject.AddPlate(
            "variable_panel", "panel", 0.4, 1.0,
            PlateThicknessDirection::Positive, "styrene");
        const auto variablePlateAnalysis = kachakacha::occt::AnalyzeModelShape(
            variablePlateProject, {{"variable_panel"}, {}}, 0.35);
        if (!variablePlateAnalysis.validBRep || !variablePlateAnalysis.closedSolid
            || !variablePlateAnalysis.meetsMinimumWall
            || variablePlateAnalysis.plateCount != 1) {
            throw std::runtime_error(
                "Variable-thickness panel did not produce a closed solid: "
                + variablePlateAnalysis.message);
        }

        Project flatPlateProject;
        flatPlateProject.AddWire("flat_outline", Wire::Polyline({
            {0.0, 0.0, 0.0}, {20.0, 0.0, 0.0}, {20.0, 10.0, 0.0},
            {0.0, 10.0, 0.0}, {0.0, 0.0, 0.0},
        }));
        flatPlateProject.AddPlanarSurface("flat_surface", "flat_outline");
        flatPlateProject.AddWire("flat_hole_plan", Wire::Circle(
            {10.0, 5.0, 2.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 2.0));
        flatPlateProject.AddProjectedWire(
            "flat_hole", "flat_hole_plan", "flat_surface", {0.0, 0.0, -1.0});
        flatPlateProject.AddWire("flat_relief_plan", Wire::Line(
            {0.0, 5.0, 2.0}, {8.0, 5.0, 2.0}));
        flatPlateProject.AddProjectedWire(
            "flat_relief", "flat_relief_plan", "flat_surface", {0.0, 0.0, -1.0});
        flatPlateProject.AddPlate(
            "flat_plate", "flat_surface", 0.8, 1.2,
            PlateThicknessDirection::Positive, "styrene");
        flatPlateProject.AddPlateOpening("flat_plate", "flat_hole");
        flatPlateProject.AddPlateReliefCut("flat_plate", "flat_relief");
        const auto flatPlateAnalysis = kachakacha::occt::AnalyzeModelShape(
            flatPlateProject, {{"flat_plate"}, {}}, 0.5);
        if (!flatPlateAnalysis.validBRep || !flatPlateAnalysis.closedSolid
            || !flatPlateAnalysis.meetsMinimumWall
            || flatPlateAnalysis.plateCount != 1
            || !flatPlateProject.Plates().front().plate.HasVariableThickness()) {
            throw std::runtime_error(
                "Opened planar plate did not produce a closed solid: "
                + flatPlateAnalysis.message);
        }

        const auto flatPattern = kachakacha::io::BuildPlateFlatPattern(
            flatPlateProject, flatPlateProject.Plates().front());
        const auto developedResult = kachakacha::io::AddPlateFlatPatternModel(
            flatPlateProject,
            flatPlateProject.Plates().front(),
            flatPattern,
            WorkPlane::FromPointNormal({0.0, 0.0, 20.0}, {0.0, 0.0, 1.0}),
            "developed_flat",
            0.2);
        const auto developedAnalysis = kachakacha::occt::AnalyzeModelShape(
            flatPlateProject, {{developedResult.plateName}, {}}, 0.2);
        if (!developedAnalysis.validBRep || !developedAnalysis.closedSolid
            || developedAnalysis.plateCount != 1 || developedAnalysis.volumeCubicMillimeters <= 1.0) {
            throw std::runtime_error(
                "Developed plate with a relief slot did not produce a closed solid: "
                + developedAnalysis.message);
        }

        kachakacha::io::PlateFlatPattern notchedPattern;
        notchedPattern.plateName = "notched_test";
        notchedPattern.outerBoundary = {
            "notched_test_outer",
            {
                {0.0, 0.0}, {8.0, 0.0}, {10.0, 4.0}, {12.0, 0.0},
                {20.0, 0.0}, {20.0, 10.0}, {0.0, 10.0}, {0.0, 0.0},
            },
        };
        notchedPattern.reliefCuts.push_back({
            "notched_test_v",
            {{8.0, 0.0}, {10.0, 4.0}, {12.0, 0.0}},
            true,
        });
        notchedPattern.pieces.push_back({
            "notched_test_piece",
            notchedPattern.outerBoundary,
            {},
            {},
            notchedPattern.reliefCuts,
        });
        const auto notchedResult = kachakacha::io::AddPlateFlatPatternModel(
            flatPlateProject,
            flatPlateProject.Plates().front(),
            notchedPattern,
            WorkPlane::FromPointNormal({0.0, 0.0, 40.0}, {0.0, 0.0, 1.0}),
            "developed_notched",
            0.2);
        const auto notchedAnalysis = kachakacha::occt::AnalyzeModelShape(
            flatPlateProject, {{notchedResult.plateName}, {}}, 0.2);
        if (!notchedAnalysis.validBRep || !notchedAnalysis.closedSolid
            || notchedAnalysis.plateCount != 1 || notchedAnalysis.volumeCubicMillimeters <= 1.0) {
            throw std::runtime_error(
                "Developed plate with an outer-boundary V notch did not produce a closed solid: "
                + notchedAnalysis.message);
        }

        std::ifstream acceptanceInput(argv[2]);
        if (!acceptanceInput) {
            throw std::runtime_error("Could not open the railway nose acceptance project.");
        }
        const auto acceptance = kachakacha::io::LoadProjectScript(
            acceptanceInput, "railway nose acceptance project");
        if (acceptance.Bodies().size() != 1) {
            throw std::runtime_error("Acceptance project must contain one forming jig.");
        }
        if (acceptance.Plates().size() != 2
            || acceptance.Plates().front().openingWireNames.size() != 3) {
            throw std::runtime_error(
                "Acceptance front panel must contain both lights and the windscreen opening.");
        }
        const kachakacha::occt::ModelShapeSelection frontPanelSelection{{"nose_panel_front"}, {}};
        const auto panelAnalysis = kachakacha::occt::AnalyzeModelShape(
            acceptance, frontPanelSelection, 0.4);
        if (!panelAnalysis.validBRep || !panelAnalysis.closedSolid
            || !panelAnalysis.meetsMinimumWall || panelAnalysis.plateCount != 1
            || panelAnalysis.bodyCount != 0 || panelAnalysis.volumeCubicMillimeters <= 1.0) {
            throw std::runtime_error(
                "Acceptance front panel did not produce an exportable opened solid: "
                + panelAnalysis.message);
        }

        kachakacha::io::PlateFlatPatternOptions papercraftOptions;
        papercraftOptions.includeAutomaticReliefCuts = true;
        papercraftOptions.assemblyStrategy
            = kachakacha::io::PlateAssemblyStrategy::SingleSheet;
        papercraftOptions.cutDirection
            = kachakacha::io::PapercraftCutDirection::Vertical;
        papercraftOptions.papercraftFidelity = 5;
        const auto papercraftPattern = kachakacha::io::BuildPlateFlatPattern(
            acceptance, acceptance.Plates().front(), papercraftOptions);
        Project papercraftProject = acceptance;
        const auto papercraftResult = kachakacha::io::AddPlateFlatPatternModel(
            papercraftProject,
            acceptance.Plates().front(),
            papercraftPattern,
            WorkPlane::FromPointNormal(
                {0.0, 0.0, 40.0}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}),
            "nose_papercraft",
            0.2);
        const kachakacha::occt::ModelShapeSelection papercraftSelection{
            papercraftResult.plateNames, {}};
        const auto papercraftAnalysis = kachakacha::occt::AnalyzeModelShape(
            papercraftProject, papercraftSelection, 0.2);
        for (const std::string& plateName : papercraftResult.plateNames) {
            const auto pieceAnalysis = kachakacha::occt::AnalyzeModelShape(
                papercraftProject, {{plateName}, {}}, 0.2);
            if (!pieceAnalysis.validBRep || !pieceAnalysis.closedSolid
                || pieceAnalysis.plateCount != 1) {
                throw std::runtime_error(
                    "Papercraft piece is not an exportable solid: " + plateName
                    + ": " + pieceAnalysis.message);
            }
        }
        if (!papercraftAnalysis.validBRep || !papercraftAnalysis.closedSolid
            || papercraftAnalysis.plateCount != papercraftResult.plateNames.size()
            || papercraftResult.plateNames.size() != papercraftPattern.pieces.size()) {
            throw std::runtime_error(
                "Papercraft nose pieces with window and light cutouts are not exportable solids: "
                + papercraftAnalysis.message);
        }
        const std::filesystem::path panelStl = outputDirectory / "railway-nose-front-panel.stl";
        const std::filesystem::path panelStep = outputDirectory / "railway-nose-front-panel.step";
        kachakacha::occt::WriteModelStl(panelStl, acceptance, frontPanelSelection);
        kachakacha::occt::WriteModelStep(panelStep, acceptance, frontPanelSelection);

        kachakacha::io::PlateFlatPatternOptions foldingOptions;
        foldingOptions.includeAutomaticReliefCuts = true;
        foldingOptions.cutDirection = kachakacha::io::PapercraftCutDirection::Vertical;
        foldingOptions.papercraftFidelity = 3;
        const auto foldingMotion = kachakacha::io::BuildPlateAssemblyMotion(
            acceptance, acceptance.Plates().front(), 0.45, foldingOptions);
        Project foldingProject = acceptance;
        const int selectedPiece = foldingMotion.pieceIndices.front();
        const auto foldingResult = kachakacha::io::AddPlateAssemblyMotionModel(
            foldingProject,
            acceptance.Plates().front(),
            foldingMotion,
            "nose_fold_45",
            selectedPiece);
        const kachakacha::occt::ModelShapeSelection foldingSelection{
            foldingResult.plateNames, {}};
        const auto foldingAnalysis = kachakacha::occt::AnalyzeModelShape(
            foldingProject, foldingSelection, 0.4);
        if (!foldingAnalysis.validBRep || !foldingAnalysis.closedSolid
            || foldingAnalysis.plateCount != foldingResult.plateNames.size()) {
            throw std::runtime_error(
                "Selected folded piece did not produce exportable solids: "
                + foldingAnalysis.message);
        }
        kachakacha::occt::WriteModelStl(
            outputDirectory / "railway-nose-fold-45-piece.stl",
            foldingProject,
            foldingSelection);
        kachakacha::occt::WriteModelStep(
            outputDirectory / "railway-nose-fold-45-piece.step",
            foldingProject,
            foldingSelection);

        kachakacha::io::PlateFlatPatternOptions facetedOptions;
        facetedOptions.includeAutomaticReliefCuts = true;
        facetedOptions.includeOpenings = true;
        facetedOptions.cutDirection = kachakacha::io::PapercraftCutDirection::Both;
        facetedOptions.papercraftFidelity = 2;
        const auto facetedMotion = kachakacha::io::BuildFacetedPapercraftMotion(
            acceptance, acceptance.Plates().front(), 0.45, facetedOptions);
        Project facetedProject = acceptance;
        const int facetedPiece = facetedMotion.pieceIndices.front();
        const auto facetedResult = kachakacha::io::AddPlateAssemblyMotionModel(
            facetedProject,
            acceptance.Plates().front(),
            facetedMotion,
            "nose_faceted_fold_45",
            facetedPiece);
        const kachakacha::occt::ModelShapeSelection facetedSelection{
            facetedResult.plateNames, {}};
        const auto facetedAnalysis = kachakacha::occt::AnalyzeModelShape(
            facetedProject, facetedSelection, 0.4);
        if (!facetedAnalysis.validBRep || !facetedAnalysis.closedSolid
            || facetedAnalysis.plateCount != facetedResult.plateNames.size()) {
            throw std::runtime_error(
                "Selected new-mode faceted piece did not produce exportable solids: "
                + facetedAnalysis.message);
        }
        kachakacha::occt::WriteModelStl(
            outputDirectory / "railway-nose-faceted-fold-45-piece.stl",
            facetedProject,
            facetedSelection);
        kachakacha::occt::WriteModelStep(
            outputDirectory / "railway-nose-faceted-fold-45-piece.step",
            facetedProject,
            facetedSelection);

        kachakacha::io::PlateFlatPatternOptions bentSheetOptions;
        bentSheetOptions.includeAutomaticReliefCuts = true;
        bentSheetOptions.includeOpenings = true;
        bentSheetOptions.cutDirection
            = kachakacha::io::PapercraftCutDirection::Both;
        bentSheetOptions.papercraftFidelity = 1;
        bentSheetOptions.minimumFoldAngleDegrees = 0.5;
        const auto bentSheetMotion
            = kachakacha::io::BuildBentSheetPapercraftMotion(
                acceptance,
                acceptance.Plates().front(),
                0.30,
                bentSheetOptions);
        Project bentSheetProject = acceptance;
        const auto bentSheetResult
            = kachakacha::io::AddPlateAssemblyMotionModel(
                bentSheetProject,
                acceptance.Plates().front(),
                bentSheetMotion,
                "nose_bent_sheet_30",
                0);
        const kachakacha::occt::ModelShapeSelection bentSheetSelection{
            bentSheetResult.plateNames, {}};
        const auto bentSheetAnalysis = kachakacha::occt::AnalyzeModelShape(
            bentSheetProject, bentSheetSelection, 0.4);
        if (!bentSheetAnalysis.validBRep || !bentSheetAnalysis.closedSolid
            || bentSheetAnalysis.plateCount
                != bentSheetResult.plateNames.size()) {
            throw std::runtime_error(
                "Selected 30 percent bent-sheet state did not produce exportable solids: "
                + bentSheetAnalysis.message);
        }
        kachakacha::occt::WriteModelStl(
            outputDirectory / "railway-nose-bent-sheet-30-piece.stl",
            bentSheetProject,
            bentSheetSelection);
        kachakacha::occt::WriteModelStep(
            outputDirectory / "railway-nose-bent-sheet-30-piece.step",
            bentSheetProject,
            bentSheetSelection);
        const auto bentStlPath
            = outputDirectory / "railway-nose-bent-sheet-30-piece.stl";
        const auto bentStepPath
            = outputDirectory / "railway-nose-bent-sheet-30-piece.step";
        if (!std::filesystem::exists(bentStlPath)
            || std::filesystem::file_size(bentStlPath) < 1024
            || !std::filesystem::exists(bentStepPath)
            || std::filesystem::file_size(bentStepPath) < 1024) {
            throw std::runtime_error(
                "30 percent bent-sheet STL/STEP exports are unexpectedly small.");
        }
        TopoDS_Shape bentStlShape;
        StlAPI_Reader bentStlReader;
        bentStlReader.Read(bentStlShape, bentStlPath.string().c_str());
        if (bentStlShape.IsNull()) {
            throw std::runtime_error(
                "30 percent bent-sheet STL cannot be read back.");
        }
        STEPControl_Reader bentStepReader;
        if (bentStepReader.ReadFile(bentStepPath.string().c_str())
                != IFSelect_RetDone
            || bentStepReader.TransferRoots() <= 0) {
            throw std::runtime_error(
                "30 percent bent-sheet STEP cannot be read back.");
        }
        const TopoDS_Shape bentStepShape = bentStepReader.OneShape();
        if (bentStepShape.IsNull()
            || !BRepCheck_Analyzer(bentStepShape, true).IsValid()) {
            throw std::runtime_error(
                "30 percent bent-sheet STEP has invalid boundary geometry.");
        }

        VerifyExports(outputDirectory, "jig-test", body);
        const auto acceptanceJigAnalysis = kachakacha::occt::AnalyzeBodyShape(
            acceptance.Bodies().front().body, 1.2);
        if (!acceptanceJigAnalysis.validBRep || !acceptanceJigAnalysis.closedSolid) {
            throw std::runtime_error(
                "Acceptance forming jig is not a valid solid: " + acceptanceJigAnalysis.message);
        }

        const kachakacha::occt::ModelShapeSelection completeSelection{
            {"nose_panel_front", "nose_panel_rear"}, {"nose_forming_jig"}};
        const auto completeAnalysis = kachakacha::occt::AnalyzeModelShape(
            acceptance, completeSelection, 0.4);
        if (!completeAnalysis.validBRep || !completeAnalysis.closedSolid
            || completeAnalysis.partCount != 3 || completeAnalysis.plateCount != 2
            || completeAnalysis.bodyCount != 1) {
            throw std::runtime_error(
                "Mixed plate and jig selection did not produce an exportable model: "
                + completeAnalysis.message);
        }

        std::cout << "occt body export tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
