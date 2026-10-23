#include "V2DrawingSelection.h"
#include "V2Viewport.h"

using namespace kachakacha::v2;

V2DrawingSelection::V2DrawingSelection(QObject& parent) : QObject(&parent) {}

V2DrawingSelection& V2DrawingSelection::Of(QObject& viewport)
{
    for (auto* child : viewport.children())
        if (auto* state = dynamic_cast<V2DrawingSelection*>(child)) return *state;
    return *new V2DrawingSelection(viewport);
}

bool V2DrawingSelection::IsDrawing(modeling::DrawingTool tool)
{
    using T = modeling::DrawingTool;
    switch (tool) {
    case T::Point: case T::Line: case T::Polyline: case T::Rectangle:
    case T::Circle: case T::Arc: case T::Bezier: case T::Spline:
    case T::ConnectTwoPoints: return true;
    default: return false;
    }
}

bool V2DrawingSelection::CanSelect(const app::DrawingSession& session, const V2Viewport& viewport)
{
    return IsDrawing(session.CurrentTool()) && !viewport.PickPending()
        && !viewport.ReferencePickPending() && !viewport.ExtrudeHandleShown() && !viewport.ToolPickActive();
}

void V2DrawingSelection::SyncInputs(const app::DrawingSession& session)
{
    if (documentId_ != session.GetDocument().Snapshot().id) {
        documentId_ = session.GetDocument().Snapshot().id;
        deletions_.clear(); CancelInputs();
    }
    const auto count = session.PlacedPointCount();
    if (inputs_.size() > count) inputs_.resize(count);
    while (inputs_.size() < count) inputs_.push_back(++nextToken_);
}

void V2DrawingSelection::CancelInputs()
{
    inputs_.clear(); selecting_ = false;
}

V2DrawingSelection::Prefix V2DrawingSelection::Current(const app::DrawingSession& session) const
{
    return {session.CurrentTool(), inputs_.size(), inputs_.empty() ? 0 : inputs_.back()};
}

void V2DrawingSelection::Deleted(const app::DrawingSession& session, std::uint64_t before, bool hadUndo)
{
    SyncInputs(session);
    if (!hadUndo) deletions_.clear(); // save/new/reset history boundary
    if (IsDrawing(session.CurrentTool()))
        deletions_[{before, session.GetDocument().Revision()}] = Current(session);
    selecting_ = false;
}

bool V2DrawingSelection::PreferDocumentUndo(const app::DrawingSession& session)
{
    SyncInputs(session);
    const auto& doc = session.GetDocument();
    if (!IsDrawing(session.CurrentTool()) || !doc.CanUndo() || doc.UndoLabel() != DeleteLabel) return false;
    for (const auto& [revisions, prefix] : deletions_)
        if (revisions.second == doc.Revision() && prefix == Current(session)) return true;
    return false;
}

void V2DrawingSelection::HistoryMoved(const app::DrawingSession& session, bool undo, std::uint64_t before)
{
    SyncInputs(session);
    if (!undo) {
        // A known deletion may be redone after new point input. That deletion is
        // now newer than the input, and must be the next operation undone.
        const auto at = deletions_.find({before, session.GetDocument().Revision()});
        if (at != deletions_.end() && session.GetDocument().UndoLabel() == DeleteLabel)
            at->second = Current(session);
    }
    selecting_ = false;
}
