#pragma once

#include "kachakacha/geometry/Vector2.h"
#include "kachakacha/model/Plate.h"
#include "kachakacha/model/Project.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace kachakacha::io {

struct PlateFlatPatternOptions {
    int uSegments = 128;
    int vSegments = 40;
    int openingSamples = 192;
    double marginMillimeters = 5.0;
    bool includeOpenings = true;
    bool includeFoldLines = true;
    bool includeAutomaticReliefCuts = false;
    double foldSpacingMillimeters = 8.0;
    double minimumFoldAngleDegrees = 2.0;
    double reliefCutDepthRatio = 0.45;
};

struct PlateFlatPatternAnalysis {
    model::PlateDevelopability classification = model::PlateDevelopability::DoubleCurved;
    double maximumEdgeDistortionMillimeters = 0.0;
    double rootMeanSquareEdgeDistortionMillimeters = 0.0;
    double maximumBoundaryApproximationMillimeters = 0.0;

    [[nodiscard]] double MaximumEstimatedErrorMillimeters() const noexcept;
};

struct PlateFlatPatternPath {
    std::string name;
    std::vector<geometry::Vector2> points;
};

struct PlateFlatPattern {
    std::string plateName;
    PlateFlatPatternPath outerBoundary;
    std::vector<PlateFlatPatternPath> openings;
    std::vector<PlateFlatPatternPath> foldLines;
    std::vector<PlateFlatPatternPath> reliefCuts;
    PlateFlatPatternAnalysis analysis;
};

struct PlateAssemblyGuidePath {
    std::string name;
    std::vector<geometry::Vector3> points;
};

struct PlateAssemblyGuide {
    std::string plateName;
    std::vector<PlateAssemblyGuidePath> foldLines;
    std::vector<PlateAssemblyGuidePath> reliefCuts;
};

struct PlateFlatPatternModelResult {
    std::string workPlaneName;
    std::string outerWireName;
    std::string surfaceName;
    std::string plateName;
    std::vector<std::string> openingWireNames;
    std::vector<std::string> foldWireNames;
    std::vector<std::string> reliefCutWireNames;
};

[[nodiscard]] PlateFlatPattern BuildPlateFlatPattern(
    const model::Project& project,
    const model::NamedPlate& plate,
    PlateFlatPatternOptions options = {});

[[nodiscard]] PlateAssemblyGuide BuildPlateAssemblyGuide(
    const model::Project& project,
    const model::NamedPlate& plate,
    PlateFlatPatternOptions options = {});

void WritePlateFlatPatternSvg(
    std::ostream& output,
    const PlateFlatPattern& pattern,
    PlateFlatPatternOptions options = {});

void WritePlateFlatPatternDxf(
    std::ostream& output,
    const PlateFlatPattern& pattern);

[[nodiscard]] PlateFlatPatternModelResult AddPlateFlatPatternModel(
    model::Project& project,
    const model::NamedPlate& sourcePlate,
    const PlateFlatPattern& pattern,
    model::WorkPlane targetPlane,
    std::string namePrefix,
    double reliefCutWidthMillimeters = 0.2);

} // namespace kachakacha::io
