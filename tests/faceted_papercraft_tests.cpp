#include "kachakacha/io/FacetedPapercraft.h"
#include "kachakacha/io/ProjectScript.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

using kachakacha::geometry::Vector3;
using kachakacha::io::AddPlateAssemblyMotionModel;
using kachakacha::io::AddPlateFlatPatternModel;
using kachakacha::io::BuildFacetedPapercraftMotion;
using kachakacha::io::BuildFacetedPapercraftPattern;
using kachakacha::io::LoadProjectScript;
using kachakacha::io::PapercraftCutDirection;
using kachakacha::io::PlateFlatPatternOptions;
using kachakacha::io::WritePlateFlatPatternSvg;
using kachakacha::model::PlateThicknessDirection;
using kachakacha::model::Project;
using kachakacha::model::Wire;
using kachakacha::model::WorkPlane;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Project MakeTwistedPlate()
{
    Project project;
    project.AddWire("first", Wire::Line(
        {0.0, -14.0, -2.0}, {0.0, 14.0, 2.0}));
    project.AddWire("second", Wire::Line(
        {42.0, -14.0, -10.0}, {42.0, 14.0, 10.0}));
    project.AddRuledSurface("twisted", "first", "second");
    project.AddPlate(
        "twisted_plate", "twisted", 0.5,
        PlateThicknessDirection::Centered, "styrene");
    return project;
}

double EdgeLength(const std::array<Vector3, 3>& panel, int edge)
{
    return (panel[static_cast<std::size_t>((edge + 1) % 3)]
        - panel[static_cast<std::size_t>(edge)]).Length();
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const Project source = MakeTwistedPlate();
        PlateFlatPatternOptions coarseOptions;
        coarseOptions.includeAutomaticReliefCuts = true;
        coarseOptions.cutDirection = PapercraftCutDirection::Both;
        coarseOptions.papercraftFidelity = 2;
        coarseOptions.minimumFoldAngleDegrees = 0.1;
        coarseOptions.allowAutomaticNotches = true;
        PlateFlatPatternOptions fineOptions = coarseOptions;
        fineOptions.papercraftFidelity = 8;

        const auto coarse = BuildFacetedPapercraftPattern(
            source, source.Plates().front(), coarseOptions);
        const auto fine = BuildFacetedPapercraftPattern(
            source, source.Plates().front(), fineOptions);
        Require(!coarse.pieces.empty() && !fine.pieces.empty(),
            "faceted generator creates physical net pieces");
        Require(coarse.plateName.find("faceted_papercraft") != std::string::npos,
            "new generator identifies its result separately from the legacy net");
        Require(fine.analysis.maximumBoundaryApproximationMillimeters
                <= coarse.analysis.maximumBoundaryApproximationMillimeters + 1.0e-8,
            "higher fidelity does not increase faceting deviation");
        Require(fine.analysis.maximumEdgeDistortionMillimeters < 1.0e-7,
            "faceted net preserves every panel edge length");
        for (const auto& piece : fine.pieces) {
            const auto closure = piece.outerBoundary.points.front()
                - piece.outerBoundary.points.back();
            Require(piece.outerBoundary.points.size() >= 4
                    && std::hypot(closure.x, closure.y) < 1.0e-7,
                "every faceted net piece has a closed cutting boundary");
        }

        const auto flat = BuildFacetedPapercraftMotion(
            source, source.Plates().front(), 0.0, fineOptions);
        const auto partial = BuildFacetedPapercraftMotion(
            source, source.Plates().front(), 0.3, fineOptions);
        const auto assembled = BuildFacetedPapercraftMotion(
            source, source.Plates().front(), 1.0, fineOptions);
        Require(flat.panels.size() == partial.panels.size()
                && partial.panels.size() == assembled.panels.size()
                && !flat.panels.empty(),
            "assembly slider keeps the derived approximation panels");
        Require(std::abs(partial.progress - 0.3) < 1.0e-12,
            "assembly slider preserves an arbitrary stopped state");
        for (std::size_t panel = 0; panel < flat.panels.size(); ++panel) {
            for (int edge = 0; edge < 3; ++edge) {
                Require(std::abs(EdgeLength(flat.panels[panel], edge)
                        - EdgeLength(partial.panels[panel], edge)) < 1.0e-7
                        && std::abs(EdgeLength(flat.panels[panel], edge)
                            - EdgeLength(assembled.panels[panel], edge)) < 1.0e-7,
                    "assembly motion rotates rigid facets without stretching them");
            }
        }
        Require(assembled.maximumTargetMismatchMillimeters < 1.0e-5,
            "fully assembled facets return to the independent approximation");

        Project derived = source;
        const std::size_t originalPlateCount = derived.Plates().size();
        const auto added = AddPlateAssemblyMotionModel(
            derived, source.Plates().front(), partial, "faceted_partial_30");
        Require(derived.Plates().size() == originalPlateCount + added.plateNames.size()
                && source.Plates().size() == originalPlateCount,
            "stopped assembly state is saved as separate objects without changing the source");

        if (argc >= 2) {
            std::ifstream input(argv[1]);
            Require(static_cast<bool>(input), "railway nose sample opens");
            const Project nose = LoadProjectScript(input, argv[1]);
            const auto plate = std::find_if(
                nose.Plates().begin(), nose.Plates().end(), [](const auto& candidate) {
                    return candidate.name == "nose_panel_front";
                });
            Require(plate != nose.Plates().end(), "railway nose front plate exists");
            PlateFlatPatternOptions openedOptions = fineOptions;
            openedOptions.papercraftFidelity = 6;
            const auto opened = BuildFacetedPapercraftMotion(
                nose, *plate, 1.0, openedOptions);
            const auto openedPattern = BuildFacetedPapercraftPattern(
                nose, *plate, openedOptions);
            if (argc >= 3) {
                std::ofstream output(argv[2]);
                Require(static_cast<bool>(output), "faceted nose SVG opens");
                WritePlateFlatPatternSvg(output, openedPattern, openedOptions);
                Require(static_cast<bool>(output), "faceted nose SVG is written");
            }
            PlateFlatPatternOptions closedOptions = openedOptions;
            closedOptions.includeOpenings = false;
            const auto closed = BuildFacetedPapercraftMotion(
                nose, *plate, 1.0, closedOptions);
            Require(opened.materialAreaSquareMillimeters
                    < closed.materialAreaSquareMillimeters,
                "windows and light holes remove real material from the new papercraft model");
            Require(opened.materialAreaSquareMillimeters
                    <= closed.materialAreaSquareMillimeters * 0.98,
                "opening area is large enough to be a physical cut rather than a display line");
            Project developed = nose;
            const auto flatObjects = AddPlateFlatPatternModel(
                developed,
                *plate,
                openedPattern,
                WorkPlane::FromPointNormal({0.0, 0.0, 80.0}, {0.0, 0.0, 1.0}),
                "faceted_nose_flat",
                0.2);
            Require(flatObjects.plateNames.size() == openedPattern.pieces.size()
                    && developed.Plates().size()
                        == nose.Plates().size() + flatObjects.plateNames.size(),
                "new net creates separate editable flat plate objects");
        }
    } catch (const std::exception& error) {
        std::cerr << "faceted_papercraft_tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "faceted papercraft tests passed\n";
    return EXIT_SUCCESS;
}
