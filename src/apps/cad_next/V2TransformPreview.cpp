#include "V2TransformPreview.h"
#include "V2Viewport.h"
#include "V2WireSource.h"
#include "V2ToolSelection.h"
#include <QMouseEvent>
#include <QVariant>
#include "kachakacha/geometry/WireEdit.h"
#include <unordered_set>
#include <algorithm>
namespace v2ui {
bool TransformTargetPick(const V2Viewport& view,const kachakacha::v2::app::DrawingSession& session,const QMouseEvent& event){
    return event.button()==Qt::LeftButton&&TransformTool(session.CurrentTool())&&!V2ToolSelection::Active(view)
        &&!view.PickPending()&&!view.ReferencePickPending()
        &&(view.property("transformTargetPicking").toBool()||(event.modifiers()&Qt::ControlModifier));
}
void FilterTransformCandidates(const kachakacha::v2::app::DrawingSession& session,
    std::vector<kachakacha::v2::app::PickCandidate>& candidates,bool enabled){
    using namespace kachakacha::v2;
    if(!enabled||!TransformTool(session.CurrentTool()))return;
    std::erase_if(candidates,[&](const auto& c){const auto* e=session.GetDocument().FindEntity(c.entityId);
        return !e||(e->kind!=domain::EntityKind::Wire&&e->kind!=domain::EntityKind::Part);});
}
void AppendTransformGhost(const V2Viewport& view,const kachakacha::v2::app::DrawingSession& session,
    const kachakacha::v2::app::SelectionSet& selection,kachakacha::v2::app::HoverResult& hover,
    const kachakacha::v2::app::CursorInputPanel& panel,kachakacha::v2::geometry::Vector3 cursorDelta){
    using namespace kachakacha::v2;using T=modeling::DrawingTool;
    if(session.CurrentTool()!=T::Copy&&session.CurrentTool()!=T::Move)return;
    hover.preview.clear();
    if(session.PlacedPointCount()!=1||!hover.position)return;
    auto delta=*hover.position-session.ConstraintAnchor();
    if(panel.active){const auto solved=app::SolveDelta(panel,cursorDelta);if(!solved.HasValue())return;
        delta=panel.onWorkPlane?view.WorkPlane().uAxis*solved.Value().x+view.WorkPlane().vAxis*solved.Value().y:solved.Value();}
    // ApplyTransformPlan acts on entire entity IDs, including a partially picked wire.
    std::unordered_set<base::EntityId> selected(selection.entityIds.begin(),selection.entityIds.end());
    for(const auto& id:selection.entityIds)for(const auto& curve:SourceWireCurves(session.GetDocument(),session.Scene(),id))
        hover.preview.push_back(geometry::TranslateCurve(curve.segment,delta));
    for(const auto& shape:view.ShapeViews())if(selected.contains(shape.entityId))for(const auto& edge:shape.mesh.edges){
        if(edge.size()<2)continue;auto points=edge;for(auto& p:points)p=p+delta;
        for(std::size_t i=1;i<points.size();++i){const auto curve=geometry::CurveSegment::MakeLine(points[i-1],points[i]);
            if(curve.HasValue())hover.preview.push_back(curve.Value());}}
}
}
