#pragma once
#include "kachakacha/app/Selection.h"
#include "kachakacha/modeling/SnapEngine.h"

namespace v2ui {
bool SnapMatchesPointTarget(const kachakacha::v2::app::PickCandidate& target,
    const kachakacha::v2::modeling::SnapCandidate& candidate);
}
