#pragma once

#include "kachakacha/model/Project.h"

#include <filesystem>
#include <string>
#include <vector>

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

struct ModelShapeSelection {
    std::vector<std::string> plateNames;
    std::vector<std::string> bodyNames;

    [[nodiscard]] bool Empty() const noexcept
    {
        return plateNames.empty() && bodyNames.empty();
    }
};

struct ModelShapeAnalysis {
    bool validBRep = false;
    bool closedSolid = false;
    bool meetsMinimumWall = false;
    std::size_t partCount = 0;
    std::size_t plateCount = 0;
    std::size_t bodyCount = 0;
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

[[nodiscard]] ModelShapeAnalysis AnalyzeModelShape(
    const model::Project& project,
    const ModelShapeSelection& selection,
    double requiredMinimumWallMillimeters);

void WriteModelStl(
    const std::filesystem::path& path,
    const model::Project& project,
    const ModelShapeSelection& selection,
    double linearDeflectionMillimeters = 0.08,
    double angularDeflectionRadians = 0.35);

void WriteModelStep(
    const std::filesystem::path& path,
    const model::Project& project,
    const ModelShapeSelection& selection);

} // namespace kachakacha::occt
