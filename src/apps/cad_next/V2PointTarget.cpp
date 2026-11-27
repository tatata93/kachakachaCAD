#include "V2PointTarget.h"
#include "V2Viewport.h"
#include "V2ToolSelection.h"
#include "V2TransformPreview.h"
#include "kachakacha/app/PlaneFocus.h"
#include "kachakacha/app/ToolTargeting.h"
#include "kachakacha/geometry/CurveIntersection.h"
#include <algorithm>
#include <cmath>

namespace v2ui {
bool SnapMatchesPointTarget(const kachakacha::v2::app::PickCandidate& target,
    const kachakacha::v2::modeling::SnapCandidate& candidate)
{
    using kachakacha::v2::modeling::SnapKind;
    // An explicitly chosen curve must not silently become its projection onto
    // a different plane. The real curve/plane root remains independently usable.
    if (candidate.kind==SnapKind::ProjectedOnPlane) return false;
    const auto matches=[&](const auto& entity,const auto& segment) {
        return entity==target.entityId && (target.segmentId.IsNil() || segment==target.segmentId);
    };
    return matches(candidate.entityId,candidate.segmentId)
        || matches(candidate.otherEntityId,candidate.otherSegmentId);
}

void KeepWorkPlanePointSources(std::vector<kachakacha::v2::app::PickCandidate>& candidates,
    const kachakacha::v2::modeling::SnapScene& scene,const kachakacha::v2::geometry::ScreenMapping& mapping,
    const kachakacha::v2::geometry::ScreenPoint& pointer,const kachakacha::v2::modeling::WorkPlaneFrame& plane,
    const kachakacha::v2::geometry::GeometryTolerance& tolerance)
{
    using namespace kachakacha::v2;
    std::vector<app::PickCandidate> kept;kept.reserve(candidates.size());
    for(auto candidate:candidates) {
        if(candidate.segmentId.IsNil()) {
            if(std::abs(plane.SignedDistance(candidate.hitPoint))<=app::kOnPlaneToleranceMm)kept.push_back(candidate);
            continue;
        }
        const auto source=std::find_if(scene.curves.begin(),scene.curves.end(),[&](const auto& curve) {
            return curve.entityId==candidate.entityId && curve.segmentId==candidate.segmentId;
        });
        if(source==scene.curves.end())continue;
        if(app::CurveLiesOnPlane(source->segment,plane)) {kept.push_back(candidate);continue;}
        const auto roots=geometry::IntersectCurveWithPlane(source->segment,plane.origin,plane.normal,tolerance);
        bool found=false;double nearest=tolerance.snapPickPx;
        for(const auto& root:roots) {
            const auto projected=mapping.Project(root.position);if(!projected)continue;
            const auto distance=std::hypot(projected->x-pointer.x,projected->y-pointer.y);
            if(distance>nearest || (found && distance==nearest))continue;
            candidate.hitPoint=root.position;candidate.curveParameter=root.parameter;candidate.distancePx=distance;
            nearest=distance;found=true;
        }
        if(found)kept.push_back(candidate);
    }
    // Keep point/curve group priority; distances now refer to the actual roots.
    for(auto first=kept.begin();first!=kept.end();) {
        const auto end=std::find_if(first,kept.end(),[&](const auto& item){return item.kind!=first->kind;});
        std::stable_sort(first,end,[](const auto& a,const auto& b){return a.distancePx<b.distancePx;});
        first=end;
    }
    candidates=std::move(kept);
}
}

bool V2Viewport::PointInputActive() const
{
    using kachakacha::v2::modeling::DrawingTool;
    if (ReferencePickPending() || V2ToolSelection::Active(*this)) return false;
    if (directionPick_) return directionPick_->plane==DirectionPickPlane::WorkPlane;
    if (pickHandler_) return true;
    if (toolPickActive_ || boxSelect_.active) return false;
    const auto tool=session_->CurrentTool();
    if (v2ui::TransformTool(tool) && property("transformTargetPicking").toBool()) return false;
    if (tool==DrawingTool::Measure) return measurePointCount_>0;
    const auto prompt=session_->Prompt();
    return (prompt.remainingPoints>0 || prompt.acceptsMorePoints)
        && !kachakacha::v2::app::ClickPicksTarget(tool,!selection_.entityIds.empty(),
        static_cast<int>(session_->PlacedPointCount()));
}

std::optional<kachakacha::v2::app::PickCandidate> V2Viewport::ExplicitPointTarget() const
{
    if (!cycle_.explicitInput || !PointInputActive()
        || snapSuppressedByKey_ || snapSuppressedBySetting_) return {};
    const auto target=CurrentCandidate();
    return target && EntityShown(target->entityId) ? target : std::nullopt;
}

void V2Viewport::AdoptPointTarget()
{
    if (!PointInputActive() || !CurrentCandidate()) { ReleasePointTarget(); return; }
    cycle_.explicitInput=true;
    if (const auto target=ExplicitPointTarget()) {
        session_->SetSnapCandidateFilter([target=*target](const auto& candidate) {
            return v2ui::SnapMatchesPointTarget(target,candidate);
        });
    } else ReleasePointTarget();
}

void V2Viewport::ReleasePointTarget()
{
    if (!cycle_.explicitInput) return;
    cycle_.explicitInput=false;
    cycle_.index=0;
    session_->SetSnapCandidateFilter({});
}

void V2Viewport::AlignPointHoverCandidate()
{
    if (!PointInputActive() || cycle_.explicitInput || !hover_.snap) return;
    const auto& snap=*hover_.snap;
    for (std::size_t i=0;i<cycle_.candidates.size();++i) {
        const auto& candidate=cycle_.candidates[i];
        if (candidate.entityId==snap.entityId && (snap.segmentId.IsNil()
                || candidate.segmentId==snap.segmentId)) {
            cycle_.index=i;return;
        }
    }
}
