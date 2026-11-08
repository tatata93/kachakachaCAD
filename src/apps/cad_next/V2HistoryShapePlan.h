#pragma once

#include "kachakacha/app/ShapeRebuild.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace v2ui {
struct PartHistoryPlan {
    std::vector<std::string> removed;
    std::vector<kachakacha::v2::app::ShapeRebuildStep> added;
};

// UI-private optimization, not a geometry cache keyed by names or revisions.
// Unknown maps, modified existing geometry, guide/fabrication changes and
// tolerances all fall back to the ordinary complete rebuild.
[[nodiscard]] std::optional<PartHistoryPlan> PlanPartHistory(
    const kachakacha::v2::document::DocumentSnapshot& before,
    const kachakacha::v2::document::DocumentSnapshot& after,
    const std::map<std::string,kachakacha::v2::modeling::KernelShapeHandle>& parts,
    const std::map<std::string,kachakacha::v2::modeling::KernelShapeHandle>& guides);

void RemoveHistoryParts(const PartHistoryPlan&,
    std::map<std::string,kachakacha::v2::modeling::KernelShapeHandle>& parts,
    const std::map<std::string,kachakacha::v2::modeling::KernelShapeHandle>& guides);
}
