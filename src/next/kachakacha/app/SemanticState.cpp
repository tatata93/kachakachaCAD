#include "kachakacha/app/SemanticState.h"

namespace kachakacha::v2::app {

const char* SemanticStateNameJa(SemanticState state) noexcept
{
    switch (state) {
    case SemanticState::Default:  return "通常";
    case SemanticState::Hover:    return "カーソルの下";
    case SemanticState::Selected: return "選択";
    case SemanticState::Snap:     return "吸着";
    case SemanticState::Preview:  return "途中経過";
    }
    return "不明";
}

SemanticState CurveSemanticState(const SelectionSet& selection, base::EntityId entityId,
    base::SegmentId segmentId, base::EntityId hoveredEntityId,
    base::SegmentId hoveredSegmentId)
{
    if (IsCurveSelected(selection, entityId, segmentId)) {
        return SemanticState::Selected;
    }
    if (hoveredEntityId.IsNil() || hoveredEntityId != entityId) {
        return SemanticState::Default;
    }
    // 線分まで分かっているなら、その線分だけを出す。分からないときは物体全体。
    if (!hoveredSegmentId.IsNil() && hoveredSegmentId != segmentId) {
        return SemanticState::Default;
    }
    return SemanticState::Hover;
}

SemanticState PointSemanticState(const SelectionSet& selection, base::EntityId entityId,
    base::EntityId hoveredEntityId)
{
    if (IsSelected(selection, entityId)) {
        return SemanticState::Selected;
    }
    if (!hoveredEntityId.IsNil() && hoveredEntityId == entityId) {
        return SemanticState::Hover;
    }
    return SemanticState::Default;
}

} // namespace kachakacha::v2::app
