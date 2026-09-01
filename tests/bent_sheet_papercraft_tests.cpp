#include "kachakacha/io/BentSheetPapercraft.h"
#include "kachakacha/io/ProjectScript.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>

using kachakacha::io::AddPlateAssemblyMotionModel;
using kachakacha::io::AddPlateFlatPatternModel;
using kachakacha::io::BuildBentSheetPapercraftMotion;
using kachakacha::io::BuildBentSheetPapercraftPattern;
using kachakacha::io::BuildBentSheetPapercraftPreview;
using kachakacha::io::LoadProjectScript;
using kachakacha::io::PapercraftCutDirection;
using kachakacha::io::PlateFlatPatternOptions;
using kachakacha::io::WritePlateFlatPatternSvg;
using kachakacha::model::Project;
using kachakacha::model::WorkPlane;

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

double MaximumPanelEdgeChange(
    const kachakacha::io::PlateAssemblyMotion& first,
    const kachakacha::io::PlateAssemblyMotion& second)
{
    Require(first.panels.size() == second.panels.size(),
        "assembly states keep the same panel topology");
    double maximum = 0.0;
    for (std::size_t panel = 0; panel < first.panels.size(); ++panel) {
        for (int edge = 0; edge < 3; ++edge) {
            const int next = (edge + 1) % 3;
            const double firstLength = (
                first.panels[panel][static_cast<std::size_t>(next)]
                - first.panels[panel][static_cast<std::size_t>(edge)]).Length();
            const double secondLength = (
                second.panels[panel][static_cast<std::size_t>(next)]
                - second.panels[panel][static_cast<std::size_t>(edge)]).Length();
            maximum = std::max(maximum, std::abs(firstLength - secondLength));
        }
    }
    return maximum;
}

using PointKey = std::tuple<long long, long long, long long>;
using EdgeKey = std::pair<PointKey, PointKey>;

PointKey QuantizedPoint(kachakacha::geometry::Vector3 point)
{
    constexpr double scale = 1.0e7;
    return {
        std::llround(point.x * scale),
        std::llround(point.y * scale),
        std::llround(point.z * scale),
    };
}

double MaximumInteriorDihedralDegrees(
    const kachakacha::io::PlateAssemblyMotion& motion)
{
    constexpr double pi = 3.14159265358979323846;
    std::map<EdgeKey, kachakacha::geometry::Vector3> firstNormalByEdge;
    double maximum = 0.0;
    for (const auto& panel : motion.panels) {
        const auto normal = kachakacha::geometry::Cross(
            panel[1] - panel[0], panel[2] - panel[0]).Normalized();
        for (int edge = 0; edge < 3; ++edge) {
            PointKey first = QuantizedPoint(panel[static_cast<std::size_t>(edge)]);
            PointKey second = QuantizedPoint(
                panel[static_cast<std::size_t>((edge + 1) % 3)]);
            if (second < first) {
                std::swap(first, second);
            }
            const EdgeKey key{first, second};
            const auto [position, inserted]
                = firstNormalByEdge.emplace(key, normal);
            if (!inserted) {
                const double angle = std::acos(std::clamp(
                    kachakacha::geometry::Dot(position->second, normal),
                    -1.0,
                    1.0)) * 180.0 / pi;
                maximum = std::max(maximum, angle);
            }
        }
    }
    return maximum;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        Require(argc >= 2, "railway nose sample path is required");
        std::ifstream input(argv[1]);
        Require(static_cast<bool>(input), "railway nose sample opens");
        const Project nose = LoadProjectScript(input, argv[1]);
        const auto plate = std::find_if(
            nose.Plates().begin(), nose.Plates().end(), [](const auto& candidate) {
                return candidate.name == "nose_panel_front";
            });
        Require(plate != nose.Plates().end(), "railway nose front plate exists");

        PlateFlatPatternOptions options;
        options.includeAutomaticReliefCuts = true;
        options.includeOpenings = true;
        options.cutDirection = PapercraftCutDirection::Both;
        options.papercraftFidelity = 3;
        options.minimumFoldAngleDegrees = 0.5;
        options.allowAutomaticNotches = true;
        const auto pattern = BuildBentSheetPapercraftPattern(
            nose, *plate, options);
        Require(pattern.plateName.find("bent_sheet_papercraft") != std::string::npos,
            "bent-sheet result is separate from legacy and faceted results");
        Require(pattern.pieces.size() == 1 && pattern.analysis.pieceCount == 1,
            "automatic curved-paper result remains one connected paper piece");
        Require(pattern.pieces.front().outerBoundary.points.size() >= 128,
            "continuous paper piece keeps a smooth curved outer boundary");
        Require(Distance(
                    pattern.pieces.front().outerBoundary.points.front(),
                    pattern.pieces.front().outerBoundary.points.back()) < 1.0e-7,
            "continuous paper cutting boundary is closed");
        Require(pattern.pieces.front().openings.size()
                == plate->openingWireNames.size(),
            "all railway windows and light holes remain real openings");
        for (const auto& opening : pattern.pieces.front().openings) {
            Require(opening.points.size() >= 192,
                "opening uses the projected curve instead of coarse polygon cells");
            Require(Distance(opening.points.front(), opening.points.back()) < 1.0e-7,
                "each curved opening remains closed");
        }
        Require(!pattern.reliefCuts.empty(),
            "double curvature creates slits or V reliefs");
        Require(pattern.analysis.separatingSeamCount == 0
                && pattern.analysis.nonSeparatingReliefCutCount
                    == static_cast<int>(pattern.reliefCuts.size())
                && pattern.analysis.reliefCutLengthMillimeters > 0.0,
            "automatic relief cuts remain distinct from part-separating seams");
        Require(std::all_of(
                    pattern.reliefCuts.begin(), pattern.reliefCuts.end(),
                    [](const auto& cut) {
                        return cut.cutKind
                            == kachakacha::io::PapercraftCutKind::NonSeparatingReliefCut;
                    }),
            "automatic cuts carry explicit non-separating semantics");
        Require(std::any_of(
                    pattern.reliefCuts.begin(),
                    pattern.reliefCuts.end(),
                    [](const auto& cut) {
                        return cut.incorporatedInOuterBoundary;
                    }),
            "curved-V mode places a physical boundary notch when space permits");

        PlateFlatPatternOptions slitOptions = options;
        slitOptions.allowAutomaticNotches = false;
        const auto slitPattern = BuildBentSheetPapercraftPattern(
            nose, *plate, slitOptions);
        Require(slitPattern.pieces.size() == 1
                && std::all_of(
                    slitPattern.reliefCuts.begin(), slitPattern.reliefCuts.end(),
                    [](const auto& cut) { return !cut.incorporatedInOuterBoundary; }),
            "line-slit mode leaves bridges and does not split the paper into strips");

        const auto flat = BuildBentSheetPapercraftMotion(
            nose, *plate, 0.0, options);
        const auto partial = BuildBentSheetPapercraftMotion(
            nose, *plate, 0.3, options);
        const auto assembled = BuildBentSheetPapercraftMotion(
            nose, *plate, 1.0, options);
        PlateFlatPatternOptions fineOptions = options;
        fineOptions.papercraftFidelity = 10;
        const auto finePartial = BuildBentSheetPapercraftMotion(
            nose, *plate, 0.3, fineOptions);
        PlateFlatPatternOptions fineCompletedOptions = options;
        fineCompletedOptions.papercraftFidelity = 7;
        const auto fineAssembled = BuildBentSheetPapercraftMotion(
            nose, *plate, 1.0, fineCompletedOptions);
        Require(!flat.panels.empty()
                && flat.panels.size() == partial.panels.size()
                && partial.panels.size() == assembled.panels.size(),
            "smooth-bend slider keeps the same connected paper skin");
        Require(std::abs(partial.progress - 0.3) < 1.0e-12,
            "arbitrary smooth-bend state is preserved");
        std::cout << "bend-state material error: edge-change partial="
                  << MaximumPanelEdgeChange(flat, partial)
                  << " mm, complete="
                  << MaximumPanelEdgeChange(flat, assembled)
                  << " mm, reported partial="
                  << partial.maximumMaterialEdgeErrorMillimeters
                  << " mm, complete="
                  << assembled.maximumMaterialEdgeErrorMillimeters
                  << " mm, dihedral partial="
                  << MaximumInteriorDihedralDegrees(partial)
                  << " deg, fidelity-10="
                  << MaximumInteriorDihedralDegrees(finePartial)
                  << " deg, flat="
                  << MaximumInteriorDihedralDegrees(flat)
                  << " deg, fidelity-10 material="
                  << finePartial.maximumMaterialEdgeErrorMillimeters
                  << " mm\n";
        Require(MaximumPanelEdgeChange(flat, partial) < 0.06
                && MaximumPanelEdgeChange(flat, assembled) < 0.11,
            "paper edge-length error remains below the working tolerance");
        Require(partial.maximumMaterialEdgeErrorMillimeters < 0.06
                && assembled.maximumMaterialEdgeErrorMillimeters < 0.11,
            "reported material error measures the generated assembly geometry");
        Require(finePartial.maximumMaterialEdgeErrorMillimeters < 0.11,
            "high-fidelity intermediate bend preserves material dimensions");
        Require(MaximumInteriorDihedralDegrees(finePartial) < 45.0,
            "high-fidelity intermediate bend contains no mesh-scale wrinkles");
        Require(std::abs(flat.materialAreaSquareMillimeters
                    - partial.materialAreaSquareMillimeters) < 1.0e-4
                && std::abs(flat.materialAreaSquareMillimeters
                    - assembled.materialAreaSquareMillimeters) < 1.0e-4,
            "paper material area remains unchanged while bending");
        Require(std::all_of(
                    assembled.pieceIndices.begin(), assembled.pieceIndices.end(),
                    [](int piece) { return piece == 0; }),
            "all generated panels belong to one physical paper part");
        Require(assembled.maximumSeamMismatchMillimeters < 1.0e-7,
            "paired V-cut edges meet in the assembled state");
        Require(assembled.maximumPanelConnectionMismatchMillimeters < 1.0e-7,
            "the assembled paper remains connected across computational panels");
        Require(assembled.maximumTargetMismatchMillimeters > 0.0
                && assembled.rootMeanSquareTargetMismatchMillimeters > 0.0,
            "shape error is measured from the generated geometry");
        std::cout << "completed fidelity comparison: coarse max="
                  << assembled.maximumTargetMismatchMillimeters
                  << " rms=" << assembled.rootMeanSquareTargetMismatchMillimeters
                  << ", fine max="
                  << fineAssembled.maximumTargetMismatchMillimeters
                  << " rms="
                  << fineAssembled.rootMeanSquareTargetMismatchMillimeters
                  << "\n";
        Require(fineAssembled.maximumTargetMismatchMillimeters
                    < assembled.maximumTargetMismatchMillimeters * 1.10
                && fineAssembled.rootMeanSquareTargetMismatchMillimeters
                    < assembled.rootMeanSquareTargetMismatchMillimeters * 1.50,
            "analysis density does not destabilize measured assembly error");
        Require(std::abs(
                    pattern.analysis.maximumReconstructedDeviationMillimeters
                    - assembled.maximumTargetMismatchMillimeters) < 1.0e-9
                && std::abs(
                    pattern.analysis.maximumMaterialEdgeErrorMillimeters
                    - assembled.maximumMaterialEdgeErrorMillimeters) < 1.0e-9,
            "flat-pattern analysis reports actual reconstructed geometry errors");

        const auto completedPreview = BuildBentSheetPapercraftPreview(
            nose, *plate, 1.0, options);
        for (const auto& cut : completedPreview.guide.reliefCuts) {
            if (cut.name.find("_v_") == std::string::npos
                || cut.points.size() < 5) {
                continue;
            }
            std::size_t tip = cut.points.size() / 2;
            const std::size_t pairCount = std::min(
                tip + 1, cut.points.size() - tip);
            for (std::size_t pair = 0; pair < pairCount; ++pair) {
                Require((cut.points[tip - pair] - cut.points[tip + pair]).Length()
                            < 1.0e-7,
                    "completed V-cut guide shows matching paired edges");
            }
        }

        PlateFlatPatternOptions closedOptions = options;
        closedOptions.includeOpenings = false;
        const auto closed = BuildBentSheetPapercraftMotion(
            nose, *plate, 1.0, closedOptions);
        Require(assembled.materialAreaSquareMillimeters
                < closed.materialAreaSquareMillimeters * 0.98,
            "smooth windows and lights remove physical material");

        Project flatProject = nose;
        const auto flatObjects = AddPlateFlatPatternModel(
            flatProject,
            *plate,
            pattern,
            WorkPlane::FromPointNormal({0.0, 0.0, 80.0}, {0.0, 0.0, 1.0}),
            "bent_sheet_nose_flat",
            0.2);
        Require(flatObjects.plateNames.size() == 1,
            "editable flat output is one connected plate instead of loose strips");

        Project stoppedProject = nose;
        const std::size_t sourcePlateCount = stoppedProject.Plates().size();
        PlateFlatPatternOptions savedStateOptions = options;
        savedStateOptions.papercraftFidelity = 1;
        const auto savedPartial = BuildBentSheetPapercraftMotion(
            nose, *plate, 0.3, savedStateOptions);
        const auto stopped = AddPlateAssemblyMotionModel(
            stoppedProject, *plate, savedPartial, "bent_sheet_partial_30");
        Require(stopped.plateNames.size() == 1,
            "continuous bend state becomes one editable curved plate, not analysis triangles");
        const auto stoppedPlate = std::find_if(
            stoppedProject.Plates().begin(), stoppedProject.Plates().end(),
            [&](const auto& candidate) {
                return candidate.name == stopped.plateNames.front();
            });
        Require(stoppedPlate != stoppedProject.Plates().end()
                && stoppedPlate->openingWireNames.size()
                    == plate->openingWireNames.size(),
            "continuous bend-state plate preserves window and light openings");
        Require(stoppedProject.Plates().size()
                == sourcePlateCount + stopped.plateNames.size()
                && nose.Plates().size() == sourcePlateCount,
            "30 percent bend state is saved separately without changing the source");

        if (argc >= 3) {
            PlateFlatPatternOptions presentationOptions = options;
            presentationOptions.papercraftFidelity = 5;
            const auto presentationPattern = BuildBentSheetPapercraftPattern(
                nose, *plate, presentationOptions);
            std::ofstream output(argv[2]);
            Require(static_cast<bool>(output), "bent-sheet SVG opens");
            WritePlateFlatPatternSvg(
                output, presentationPattern, presentationOptions);
            Require(static_cast<bool>(output), "bent-sheet SVG is written");
        }
        std::cout << "bent-sheet geometry: max3d="
                  << assembled.maximumTargetMismatchMillimeters
                  << " mm, rms3d="
                  << assembled.rootMeanSquareTargetMismatchMillimeters
                  << " mm, material="
                  << assembled.maximumMaterialEdgeErrorMillimeters
                  << " mm, seam="
                  << assembled.maximumSeamMismatchMillimeters
                  << " mm, connection="
                  << assembled.maximumPanelConnectionMismatchMillimeters
                  << " mm\n";
    } catch (const std::exception& error) {
        std::cerr << "bent_sheet_papercraft_tests failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "bent-sheet papercraft tests passed\n";
    return EXIT_SUCCESS;
}
