#include "kachakacha/io/FabricationPanelPapercraft.h"
#include "kachakacha/io/ProjectScript.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

double Distance(
    kachakacha::geometry::Vector2 first,
    kachakacha::geometry::Vector2 second)
{
    return std::hypot(first.x - second.x, first.y - second.y);
}

std::pair<kachakacha::geometry::Vector2, kachakacha::geometry::Vector2>
PathBounds(const kachakacha::io::PlateFlatPatternPath& path)
{
    kachakacha::geometry::Vector2 minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    kachakacha::geometry::Vector2 maximum{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()};
    for (const auto point : path.points) {
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
    }
    return {minimum, maximum};
}

void VerifyEr2RoundCab(const char* path, const char* outputSvg)
{
    std::ifstream input(path);
    Require(static_cast<bool>(input), "ER1 / ER2 round-cab sample opens");
    const auto project = kachakacha::io::LoadProjectScript(input, path);
    const auto plate = std::find_if(
        project.Plates().begin(), project.Plates().end(), [](const auto& candidate) {
            return candidate.name == "er2_round_cab_shell";
        });
    Require(plate != project.Plates().end(), "ER1 / ER2 cab shell exists");

    kachakacha::io::PlateFlatPatternOptions options;
    options.papercraftFidelity = 8;
    options.uSegments = 64;
    options.vSegments = 32;
    options.openingSamples = 64;
    options.maximumShapeErrorMillimeters = 0.25;
    options.minimumPartWidthMillimeters = 2.0;
    options.maximumFabricationPanelCount = 8;
    options.panelPriority = kachakacha::io::PapercraftPanelPriority::Balanced;
    options.fabricationPanelDirection
        = kachakacha::io::FabricationPanelDirection::LongAlongU;

    const auto layout = kachakacha::io::BuildFabricationPanelLayout(
        project, *plate, options);
    Require(layout.longDirectionIsU,
        "ER1 / ER2 cab keeps broad horizontal fabrication panels");
    Require(!layout.panels.empty() && layout.panels.size() <= 8,
        "ER1 / ER2 cab uses a bounded number of large fabrication panels");
    for (std::size_t index = 1; index < layout.panels.size(); ++index) {
        Require(std::abs(
                    layout.panels[index - 1].range.maximumV
                    - layout.panels[index].range.minimumV) < 1.0e-8,
            "ER1 / ER2 fabrication panels form ordered horizontal bands");
    }

    const auto pattern
        = kachakacha::io::BuildFabricationPanelPapercraftPattern(
            project, *plate, options);
    Require(pattern.pieces.size() == layout.panels.size(),
        "ER1 / ER2 panel count matches physical unfolded parts");
    Require(pattern.openings.size() == 7,
        "ER1 / ER2 six windows and headlight remain openings");
    Require(std::all_of(
                pattern.pieces.begin(), pattern.pieces.end(),
                [](const auto& piece) {
                    return piece.outerBoundary.points.size() >= 64;
                }),
        "ER1 / ER2 output pieces keep smooth boundaries rather than triangle parts");
    double previousMaximumY = -std::numeric_limits<double>::infinity();
    for (const auto& piece : pattern.pieces) {
        const auto [minimum, maximum] = PathBounds(piece.outerBoundary);
        Require(std::abs(minimum.x) < 1.0e-8,
            "horizontal fabrication panels share a readable left alignment");
        Require(minimum.y > previousMaximumY,
            "horizontal fabrication panels are stacked without overlap");
        previousMaximumY = maximum.y;
    }
    if (outputSvg != nullptr) {
        std::ofstream svg(outputSvg);
        Require(static_cast<bool>(svg), "ER1 / ER2 panel-first SVG opens");
        kachakacha::io::WritePlateFlatPatternSvg(svg, pattern, options);
        Require(static_cast<bool>(svg), "ER1 / ER2 panel-first SVG is written");
    }

    const auto flat = kachakacha::io::BuildFabricationPanelPapercraftMotion(
        project, *plate, 0.0, options);
    const auto partial = kachakacha::io::BuildFabricationPanelPapercraftMotion(
        project, *plate, 0.3, options);
    const auto complete = kachakacha::io::BuildFabricationPanelPapercraftMotion(
        project, *plate, 1.0, options);
    Require(partial.continuousPieces.size() == layout.panels.size(),
        "ER1 / ER2 30 percent assembly keeps one continuous object per large part");
    const double areaScale = std::max(1.0, complete.materialAreaSquareMillimeters);
    Require(std::abs(flat.materialAreaSquareMillimeters
                - partial.materialAreaSquareMillimeters) / areaScale < 1.0e-8
            && std::abs(partial.materialAreaSquareMillimeters
                - complete.materialAreaSquareMillimeters) / areaScale < 1.0e-8,
        "ER1 / ER2 assembly slider preserves physical sheet area");

    std::size_t openingCount = 0;
    for (const auto& piece : partial.continuousPieces) {
        openingCount += piece.openingPaths.size();
    }
    Require(openingCount == 7,
        "ER1 / ER2 partial assembly keeps all openings in 3D output geometry");

    std::cout << " | ER1/ER2 pieces=" << layout.panels.size()
              << " max deviation=" << layout.maximumDeviationMillimeters
              << " mm tolerance=" << (layout.reachedRequestedTolerance ? "met" : "not-met");
    for (std::size_t index = 0; index < layout.panels.size(); ++index) {
        const auto& panel = layout.panels[index];
        std::cout << " | ER2-P" << index + 1 << " ["
                  << panel.range.minimumU << ',' << panel.range.maximumU
                  << "]x[" << panel.range.minimumV << ','
                  << panel.range.maximumV << "] err="
                  << panel.maximumDeviationMillimeters;
    }
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        Require(argc >= 2, "railway nose sample path is required");
        std::ifstream input(argv[1]);
        Require(static_cast<bool>(input), "railway nose sample opens");
        auto project = kachakacha::io::LoadProjectScript(input, argv[1]);
        const auto plate = std::find_if(
            project.Plates().begin(), project.Plates().end(), [](const auto& candidate) {
                return candidate.name == "nose_panel_front";
            });
        Require(plate != project.Plates().end(), "railway nose front plate exists");

        kachakacha::io::PlateFlatPatternOptions options;
        options.papercraftFidelity = 2;
        options.openingSamples = 96;
        options.maximumShapeErrorMillimeters = 0.35;
        options.minimumPartWidthMillimeters = 2.0;
        options.maximumFabricationPanelCount = 5;
        const auto layout = kachakacha::io::BuildFabricationPanelLayout(
            project, *plate, options);
        Require(!layout.panels.empty() && layout.panels.size() <= 5,
            "panel-first layout keeps a bounded number of large parts");
        for (std::size_t index = 1; index < layout.panels.size(); ++index) {
            const auto& previous = layout.panels[index - 1].range;
            const auto& current = layout.panels[index].range;
            Require(layout.longDirectionIsU
                    ? std::abs(previous.maximumV - current.minimumV) < 1.0e-8
                    : std::abs(previous.maximumU - current.minimumU) < 1.0e-8,
                "large fabrication panels form ordered continuous bands");
        }

        auto manuallySplitProject = project;
        std::vector<kachakacha::geometry::Vector3> manualSplitPoints;
        for (int sample = 0; sample <= 12; ++sample) {
            const double u = static_cast<double>(sample) / 12.0;
            manualSplitPoints.push_back(
                plate->plate.SourceSurface().Evaluate(
                    plate->plate.SourceU(u), plate->plate.SourceV(0.20)));
        }
        manuallySplitProject.AddWire(
            "manual_lower_band_drawing",
            kachakacha::model::Wire::Polyline(manualSplitPoints));
        manuallySplitProject.AddProjectedWire(
            "manual_lower_band_split", "manual_lower_band_drawing",
            "nose_skin", {0.0, 0.0, 1.0});
        manuallySplitProject.AddPlateSplitLine(
            "nose_panel_front", "manual_lower_band_split");
        const auto manualPlate = std::find_if(
            manuallySplitProject.Plates().begin(),
            manuallySplitProject.Plates().end(), [](const auto& candidate) {
                return candidate.name == "nose_panel_front";
            });
        const auto manualLayout = kachakacha::io::BuildFabricationPanelLayout(
            manuallySplitProject, *manualPlate, options);
        Require(manualLayout.panels.size() >= 2,
            "a user-selected split line becomes a fabrication-panel boundary");

        const auto pattern
            = kachakacha::io::BuildFabricationPanelPapercraftPattern(
                project, *plate, options);
        Require(pattern.pieces.size() == layout.panels.size()
                && pattern.analysis.pieceCount
                    == static_cast<int>(layout.panels.size()),
            "each approximation panel unfolds as one physical part");
        Require(pattern.openings.size() == plate->openingWireNames.size(),
            "windows and light holes remain physical openings");
        for (const auto& piece : pattern.pieces) {
            Require(piece.outerBoundary.points.size() >= 64,
                "fabrication part keeps a smooth cutting boundary");
            Require(Distance(
                        piece.outerBoundary.points.front(),
                        piece.outerBoundary.points.back()) < 1.0e-7,
                "each fabrication part is a closed cutting outline");
        }

        const auto preview
            = kachakacha::io::BuildFabricationPanelPapercraftPreview(
                project, *plate, 0.3, options);
        const auto& partial = preview.motion;
        Require(partial.pieceCount == static_cast<int>(layout.panels.size())
                && partial.continuousPieces.size() == layout.panels.size(),
            "assembly slider keeps one continuous curved model per large part");
        Require(std::all_of(
                    partial.continuousPieces.begin(),
                    partial.continuousPieces.end(),
                    [](const auto& piece) { return piece.sections.size() >= 3; }),
            "every large part has loft sections for 3D output");

        auto modelProject = project;
        const auto added = kachakacha::io::AddPlateAssemblyMotionModel(
            modelProject, *plate, partial, "panel_first_partial", 0);
        Require(added.plateNames.size() == 1,
            "a selected large part exports as one curved plate, not triangle plates");
        std::cout << "fabrication panel tests passed: pieces="
                  << layout.panels.size() << " max deviation="
                  << layout.maximumDeviationMillimeters << " mm";
        for (std::size_t index = 0; index < layout.panels.size(); ++index) {
            const auto& panel = layout.panels[index];
            std::cout << " | P" << index + 1 << " ["
                      << panel.range.minimumU << ',' << panel.range.maximumU
                      << "]x[" << panel.range.minimumV << ','
                      << panel.range.maximumV << "] err="
                      << panel.maximumDeviationMillimeters;
        }
        if (argc >= 3) {
            VerifyEr2RoundCab(argv[2], argc >= 4 ? argv[3] : nullptr);
        }
        std::cout << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "fabrication_panel_papercraft_tests failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
