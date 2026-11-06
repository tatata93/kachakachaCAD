#pragma once

#include "kachakacha/app/CommandAvailability.h"
#include <vector>

namespace kachakacha::v2::app::detail {

// Internal shared counting path. UI tool availability needs these counts, but
// must not construct planar regions on every Ctrl-click. Exact public facts and
// operation planners still evaluate their profile geometry when requested.
struct SelectionCounts {
    SelectionFacts facts;
    std::vector<base::EntityId> wireIds;
};

[[nodiscard]] SelectionCounts CountSelection(const SelectionSet& selection,
    const document::DocumentSnapshot& snapshot, const modeling::SnapScene& scene,
    const ExternalCounts& external, bool canUndo, bool canRedo);

} // namespace kachakacha::v2::app::detail
