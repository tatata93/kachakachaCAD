#pragma once

#include "kachakacha/io/PlateFlatPattern.h"

namespace kachakacha::io {

struct BentSheetPapercraftPreview {
    PlateAssemblyGuide guide;
    PlateAssemblyMotion motion;
};

// Produces a predominantly continuous paper skin. The primary curvature is
// formed by smooth bending; slits and optional V notches release only the
// secondary curvature. This is intentionally independent from both the legacy
// strip generator and the rigid faceted generator.
[[nodiscard]] PlateFlatPattern BuildBentSheetPapercraftPattern(
    const model::Project& project,
    const model::NamedPlate& plate,
    PlateFlatPatternOptions options = {});

[[nodiscard]] PlateAssemblyGuide BuildBentSheetPapercraftGuide(
    const model::Project& project,
    const model::NamedPlate& plate,
    PlateFlatPatternOptions options = {});

[[nodiscard]] PlateAssemblyMotion BuildBentSheetPapercraftMotion(
    const model::Project& project,
    const model::NamedPlate& plate,
    double progress,
    PlateFlatPatternOptions options = {});

[[nodiscard]] BentSheetPapercraftPreview BuildBentSheetPapercraftPreview(
    const model::Project& project,
    const model::NamedPlate& plate,
    double progress,
    PlateFlatPatternOptions options = {});

} // namespace kachakacha::io
