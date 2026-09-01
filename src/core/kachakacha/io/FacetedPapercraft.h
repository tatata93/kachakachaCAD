#pragma once

#include "kachakacha/io/PlateFlatPattern.h"

namespace kachakacha::io {

struct FacetedPapercraftPreview {
    PlateAssemblyGuide guide;
    PlateAssemblyMotion motion;
};

// This generator is intentionally separate from the flexible-strip flat-pattern
// implementation so both results can be compared without changing the source plate.
[[nodiscard]] PlateFlatPattern BuildFacetedPapercraftPattern(
    const model::Project& project,
    const model::NamedPlate& plate,
    PlateFlatPatternOptions options = {});

[[nodiscard]] PlateAssemblyGuide BuildFacetedPapercraftGuide(
    const model::Project& project,
    const model::NamedPlate& plate,
    PlateFlatPatternOptions options = {});

[[nodiscard]] PlateAssemblyMotion BuildFacetedPapercraftMotion(
    const model::Project& project,
    const model::NamedPlate& plate,
    double progress,
    PlateFlatPatternOptions options = {});

[[nodiscard]] FacetedPapercraftPreview BuildFacetedPapercraftPreview(
    const model::Project& project,
    const model::NamedPlate& plate,
    double progress,
    PlateFlatPatternOptions options = {});

} // namespace kachakacha::io
