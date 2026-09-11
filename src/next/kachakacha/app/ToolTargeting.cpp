#include "kachakacha/app/ToolTargeting.h"

namespace kachakacha::v2::app {

using modeling::DrawingTool;

bool ToolNeedsTargetWire(DrawingTool tool) noexcept
{
    switch (tool) {
    case DrawingTool::Move:
    case DrawingTool::Copy:
    case DrawingTool::Mirror:
    case DrawingTool::Rotate:
        return true;
    default:
        return false;
    }
}

bool ClickPicksTarget(DrawingTool tool, bool hasSelection, int pointsPlaced) noexcept
{
    if (!ToolNeedsTargetWire(tool)) {
        return false;
    }
    // 点を置き始めた後に選び直させない。置いた点が無駄になる。
    return !hasSelection && pointsPlaced <= 0;
}

bool PredicateIsExact(SelectionPredicate predicate) noexcept
{
    switch (predicate) {
    case SelectionPredicate::OneWorkPlane:
    case SelectionPredicate::OnePlanarFaceOrWorkPlane:
    case SelectionPredicate::TwoWireChains:
    case SelectionPredicate::OneClosedProfile:
    case SelectionPredicate::OnePart:
    case SelectionPredicate::TwoParts:
    case SelectionPredicate::OneDerivedEntity:
    case SelectionPredicate::OneFabricationModel:
    case SelectionPredicate::OneFabricationPanel:
    case SelectionPredicate::OnePartOrSurface:
    case SelectionPredicate::OneGuideRow:
    case SelectionPredicate::OneGuideSurfaceAndOneWorkPlane:
        return true;
    default:
        // 「1つ以上」「2つ以上」は、そろっても足す余地がある。
        return false;
    }
}

bool PredicateCanBeSatisfiedBySelection(SelectionPredicate predicate) noexcept
{
    switch (predicate) {
    case SelectionPredicate::Always:
    case SelectionPredicate::HasDocument:
    case SelectionPredicate::HasUndo:
    case SelectionPredicate::HasRedo:
    case SelectionPredicate::HasVisibleGeometry:
        return false;
    default:
        return true;
    }
}

PendingAction PendingCommandAction(SelectionPredicate predicate, bool satisfied,
    bool confirmed) noexcept
{
    if (!satisfied) {
        return PendingAction::Wait;
    }
    if (confirmed || PredicateIsExact(predicate)) {
        return PendingAction::RunNow;
    }
    return PendingAction::NeedsConfirm;
}

} // namespace kachakacha::v2::app
