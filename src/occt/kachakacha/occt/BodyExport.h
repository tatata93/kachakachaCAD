#pragma once

#include "kachakacha/model/Body.h"

#include <filesystem>
#include <string>

namespace kachakacha::occt {

struct BodyShapeAnalysis {
    bool validBRep = false;
    bool closedSolid = false;
    bool meetsMinimumWall = false;
    double minimumWallMillimeters = 0.0;
    double volumeCubicMillimeters = 0.0;
    geometry::Vector3 minimumBounds;
    geometry::Vector3 maximumBounds;
    std::string message;
};

[[nodiscard]] BodyShapeAnalysis AnalyzeBodyShape(
    const model::Body& body,
    double requiredMinimumWallMillimeters);

void WriteBodyStl(
    const std::filesystem::path& path,
    const model::Body& body,
    double linearDeflectionMillimeters = 0.08,
    double angularDeflectionRadians = 0.35);

void WriteBodyStep(
    const std::filesystem::path& path,
    const model::Body& body);

} // namespace kachakacha::occt
