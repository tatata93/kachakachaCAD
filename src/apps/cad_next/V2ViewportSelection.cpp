#include "V2Viewport.h"
#include "V2DocumentTiming.h"
#include "V2ToolSelection.h"
#include "V2DrawingSelection.h"
#include "V2RenderSelection.h"
#include "V2TransformPreview.h"
#include <utility>

void V2Viewport::SetSelection(kachakacha::v2::app::SelectionSet selection)
{
    V2DocumentTiming timing("viewport-selection");timing.Mark("begin");
    if (elementPickHandler_ && !V2ToolSelection::Active(*this)) {
        for (const auto& ref : selection.ordered) {
            kachakacha::v2::app::PickCandidate pick;
            pick.entityId = ref.entityId; pick.kind = ref.kind;
            pick.pickedFaceIndex = ref.pickedFaceIndex; pick.hitPoint = ref.hitPoint;
            pick.segmentId = ref.segmentId.value_or(kachakacha::v2::base::SegmentId{}); pick.subshapeKey = ref.subshapeKey;
            if (AcceptElementPick(pick)) return;
        }
        for (const auto& id : selection.entityIds) {
            kachakacha::v2::app::PickCandidate pick; pick.entityId = id;
            if (AcceptElementPick(pick)) return;
        }
        return;
    }

    selection_ = std::move(selection);
    if(selection_.entityIds.empty())V2DrawingSelection::Of(*this).ResumeInput();
    V2RenderSelectionOf(*this).Update(selection_);
    timing.Mark("render-selection");
    v2ui::AppendTransformGhost(*this,*session_,selection_,hover_,cursorPanel_,cursorDelta_);
    timing.Mark("transform-ghost");
    ApplyVisibilityFilter();
    timing.Mark("visibility");
    if (selectionChangedCallback_) {
        selectionChangedCallback_();
    }
    timing.Mark("callback");
    update();
}

