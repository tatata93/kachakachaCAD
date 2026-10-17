#pragma once

#include "kachakacha/app/Selection.h"
#include <algorithm>

// Wire corner tools must not consume boundary overlays owned by parts/surfaces.
inline kachakacha::v2::app::SelectionSet CornerWireSelection(
    kachakacha::v2::app::SelectionSet selection,
    const kachakacha::v2::document::Document& document, bool wholeWire)
{
    const auto isWire = [&document](auto id) {
        const auto* entity = document.FindEntity(id);
        return entity && entity->kind == kachakacha::v2::domain::EntityKind::Wire;
    };
    std::erase_if(selection.entityIds, [&](auto id) { return !isWire(id); });
    std::erase_if(selection.ordered, [&](const auto& ref) { return !isWire(ref.entityId); });
    if (wholeWire) selection.ordered.clear();
    return selection;
}
