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
    PlateFlatPatternAnalysis analysis;
};

[[nodiscard]] PlateFlatPattern BuildPlateFlatPattern(
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

} // namespace kachakacha::io
