#pragma once
#include "kachakacha/app/DrawingSession.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/CursorInput.h"
class V2Viewport;
class QMouseEvent;
namespace v2ui {
inline bool TransformTool(kachakacha::v2::modeling::DrawingTool tool){
    using T=kachakacha::v2::modeling::DrawingTool;
    return tool==T::Move||tool==T::Copy||tool==T::Rotate||tool==T::Mirror||tool==T::Scale;
}
void AppendTransformGhost(const V2Viewport&,const kachakacha::v2::app::DrawingSession&,
    const kachakacha::v2::app::SelectionSet&,kachakacha::v2::app::HoverResult&,
    const kachakacha::v2::app::CursorInputPanel&,kachakacha::v2::geometry::Vector3 cursorDelta);
void FilterTransformCandidates(const kachakacha::v2::app::DrawingSession&,
    std::vector<kachakacha::v2::app::PickCandidate>&,bool enabled);
bool TransformTargetPick(const V2Viewport&,const kachakacha::v2::app::DrawingSession&,const QMouseEvent&);
}
