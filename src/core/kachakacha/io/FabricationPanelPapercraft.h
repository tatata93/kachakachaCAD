#pragma once

#include "kachakacha/io/PlateFlatPattern.h"

namespace kachakacha::io {

enum class FabricationPanelFit {
    Plane,
    DevelopableStrip,
    ReliefAssistedStrip,
};

struct FabricationPanel {
    model::PlateSurfaceRange range;
    FabricationPanelFit fit = FabricationPanelFit::DevelopableStrip;
    double maximumDeviationMillimeters = 0.0;
    double rootMeanSquareDeviationMillimeters = 0.0;
};

struct FabricationPanelLayout {
    std::string plateName;
    bool longDirectionIsU = true;
    std::vector<FabricationPanel> panels;
    double maximumDeviationMillimeters = 0.0;
    double rootMeanSquareDeviationMillimeters = 0.0;
    bool reachedRequestedTolerance = false;
};

struct FabricationPanelPapercraftPreview {
    FabricationPanelLayout layout;
    PlateAssemblyGuide guide;
    PlateAssemblyMotion motion;
};

[[nodiscard]] FabricationPanelLayout BuildFabricationPanelLayout(
    const model::Project& project,
    const model::NamedPlate& plate,
    PlateFlatPatternOptions options = {});

[[nodiscard]] PlateFlatPattern BuildFabricationPanelPapercraftPattern(
    const model::Project& project,
    const model::NamedPlate& plate,
    PlateFlatPatternOptions options = {});

[[nodiscard]] PlateAssemblyGuide BuildFabricationPanelPapercraftGuide(
    const model::Project& project,
    const model::NamedPlate& plate,
    PlateFlatPatternOptions options = {});

[[nodiscard]] PlateAssemblyMotion BuildFabricationPanelPapercraftMotion(
    const model::Project& project,
    const model::NamedPlate& plate,
    double progress,
    PlateFlatPatternOptions options = {});

[[nodiscard]] FabricationPanelPapercraftPreview
BuildFabricationPanelPapercraftPreview(
    const model::Project& project,
    const model::NamedPlate& plate,
    double progress,
    PlateFlatPatternOptions options = {});

} // namespace kachakacha::io
