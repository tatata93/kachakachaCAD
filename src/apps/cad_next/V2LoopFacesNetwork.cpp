#include "V2LoopFacesTool.h"
#include "V2MainWindow.h"
#include "kachakacha/kernel/OcctSurfaceRegion.h"
#include "kachakacha/app/GroupTree.h"
#include "kachakacha/app/SurfaceNetwork.h"
#include "kachakacha/app/LoopGraph.h"
#include "kachakacha/geometry/CurveSampling.h"
#include <algorithm>
#include <cmath>
#include <QString>

namespace {
using namespace kachakacha::v2;
bool WithinSamplingBudget(const geometry::CurveSegment& curve)
{
    return curve.Kind()!=geometry::CurveKind::CubicBSpline||curve.ControlPoints().size()<=4098;
}
// Only equal parameterisations qualify as complete duplicates. Seed every
// spline span: five global samples can miss an intervening bend.
bool SameInterval(const geometry::CurveSegment& a,const geometry::CurveSegment& b,double epsilon,double joinTolerance)
{
    const auto& ac=a.ControlPoints();const auto& bc=b.ControlPoints();
    if(a.Kind()==b.Kind()&&!ac.empty()&&ac.size()==bc.size()){
        bool same=true,reverse=true;
        for(std::size_t i=0;i<ac.size();++i){
            same=same&&geometry::Distance(ac[i],bc[i])<=epsilon;
            reverse=reverse&&geometry::Distance(ac[i],bc[bc.size()-1-i])<=epsilon;
        }
        if(same||reverse)return true; // Convex hull property, with the same basis.
    }
    bool forward=geometry::Distance(a.StartPoint(),b.StartPoint())<=epsilon&&geometry::Distance(a.EndPoint(),b.EndPoint())<=epsilon;
    bool backward=geometry::Distance(a.StartPoint(),b.EndPoint())<=epsilon&&geometry::Distance(a.EndPoint(),b.StartPoint())<=epsilon;
    const auto check=[&](double t){
        forward=forward&&geometry::Distance(a.Evaluate(t),b.Evaluate(t))<=joinTolerance;
        backward=backward&&geometry::Distance(a.Evaluate(t),b.Evaluate(1-t))<=joinTolerance;
    };
    for(const double t:{0.,.25,.5,.75,1.})check(t);
    if(!forward&&!backward)return false;
    for(const auto* curve:{&a,&b}){
        if(!WithinSamplingBudget(*curve))return false;
        const auto samples=geometry::SampleCurve(*curve,joinTolerance*.125,16384);
        if(samples.size()>=16384)return false;
        for(const auto& p:samples)check(p.parameter);
    }
    return forward||backward;
}
double ChordDistance(geometry::Vector3 p,geometry::Vector3 a,geometry::Vector3 b)
{
    const auto d=b-a;const auto length=d.LengthSquared();
    return geometry::Distance(p,a+d*(length>0?std::clamp(geometry::Dot(p-a,d)/length,0.,1.):0.));
}
// A branch can be omitted only when its entire sampled chords are covered by
// retained intervals within the existing user join tolerance. Test both ends
// against the SAME reference chord, recursively splitting as needed; isolated
// point matches must not hide a divider or a still-open inner boundary.
bool CoveredChord(const geometry::CurveSegment& curve,double first,double last,
    const std::vector<std::vector<geometry::CurvePoint>>& retained,double tolerance,int depth=0)
{
    const auto a=curve.Evaluate(first),b=curve.Evaluate(last);
    for(const auto& polyline:retained)for(std::size_t i=1;i<polyline.size();++i)
        if(ChordDistance(a,polyline[i-1].position,polyline[i].position)<=tolerance*.75
            &&ChordDistance(b,polyline[i-1].position,polyline[i].position)<=tolerance*.75)return true;
    if(depth>=16)return false;
    const auto middle=(first+last)*.5;
    return CoveredChord(curve,first,middle,retained,tolerance,depth+1)
        &&CoveredChord(curve,middle,last,retained,tolerance,depth+1);
}
// Work on exact original curve values. A duplicate and a dangling overlap tail
// must not turn an otherwise closed window into a branched chain. This does not
// join near endpoints, refit splines, edit source wires, or raise the cycle limit.
std::vector<kachakacha::v2::app::RoleWire> ClosedBoundaryInputs(
    const std::vector<kachakacha::v2::app::RoleWire>& source,double epsilon,double joinTolerance,std::size_t& omitted)
{
    using namespace kachakacha::v2;
    struct Input {base::EntityId id;geometry::CurveSegment curve;};
    std::vector<Input> unique;
    for(const auto& wire:source)for(const auto& curve:wire.segments){
        const bool duplicate=std::any_of(unique.begin(),unique.end(),[&](const auto& prior){
            return SameInterval(curve,prior.curve,epsilon,joinTolerance);
        });
        if(duplicate)++omitted;else unique.push_back({wire.id,curve});
    }
    app::LoopGraph graph(epsilon);std::vector<app::LoopEdge> edges;
    for(std::size_t i=0;i<unique.size();++i){
        app::LoopEdge edge;edge.selection=i;edge.from=graph.NodeOf(unique[i].curve.StartPoint());
        edge.to=graph.NodeOf(unique[i].curve.EndPoint());edges.push_back(edge);
    }
    app::PruneDeadEnds(edges,graph.NodeCount());
    const bool branches=std::any_of(edges.begin(),edges.end(),[](const auto& edge){return !edge.alive;});
    if(branches){
        std::vector<std::vector<geometry::CurvePoint>> retained;
        for(std::size_t i=0;i<unique.size();++i)if(edges[i].alive){
            if(!WithinSamplingBudget(unique[i].curve))return {};
            auto points=geometry::SampleCurve(unique[i].curve,joinTolerance*.125,16384);
            if(points.size()>=16384)return {};
            retained.push_back(std::move(points));
        }
        if(retained.empty())return {};
        for(std::size_t i=0;i<unique.size();++i)if(!edges[i].alive){
            if(!WithinSamplingBudget(unique[i].curve))return {};
            const auto points=geometry::SampleCurve(unique[i].curve,joinTolerance*.125,16384);
            if(points.size()>=16384)return {};
            for(std::size_t j=1;j<points.size();++j)
                if(!CoveredChord(unique[i].curve,points[j-1].parameter,points[j].parameter,retained,joinTolerance))return {};
        }
    }
    std::vector<app::RoleWire> result;
    for(const auto& wire:source){
        app::RoleWire kept;kept.id=wire.id;
        for(std::size_t i=0;i<unique.size();++i)if(unique[i].id==wire.id){
            if(edges[i].alive)kept.segments.push_back(unique[i].curve);else ++omitted;
        }
        if(!kept.segments.empty())result.push_back(std::move(kept));
    }
    // Do not discard an entire input (for example a still-open outer boundary
    // alongside a closed window). Leave it to the normal gap/intersection UI.
    return result.size()==source.size()?result:std::vector<app::RoleWire>{};
}
}

bool V2LoopFacesTool::TryNetworkPlan()
{
    using namespace kachakacha::v2;
    const auto tolerance = ToleranceNow();
    bool compound = false;
    std::vector<app::RoleWire> wires;
    for (const auto& selection : selections_) {
        wires.push_back({selection.sourceWireId, selection.segments});
        compound = compound || selection.segments.size() > 1;
    }
    std::size_t omitted=0;
    const auto closed=ClosedBoundaryInputs(wires,tolerance.modelLinearMm,tolerance.interactiveJoinMm,omitted);
    // Keep the intersection solver for networks whose only loops require an
    // internal crossing. Normalise complete cycles first when available.
    if(!closed.empty())wires=closed;
    // Network welding is useful for exact intersections, but a merely near
    // endpoint needs the ordinary gap UI and its explicit correction first.
    std::vector<geometry::Vector3> ends;
    for(const auto& wire:wires)for(const auto& curve:wire.segments){ends.push_back(curve.StartPoint());ends.push_back(curve.EndPoint());}
    for(std::size_t i=0;i<ends.size();++i) {
        bool exact=false,near=false;
        for(std::size_t j=0;j<ends.size();++j)if(i!=j) {
            const auto d=geometry::Distance(ends[i],ends[j]);
            exact=exact||d<=tolerance.modelLinearMm;near=near||d<=tolerance.interactiveJoinMm;
        }
        if(near&&!exact)return false;
    }
    auto network = app::PlanSurfaceNetwork(wires, tolerance);
    if (network.tables.empty()) return false;
    const bool holes=std::any_of(network.tables.begin(),network.tables.end(),[](const auto& table){
        return std::any_of(table.rows.begin(),table.rows.end(),[](const auto& row){return row.role==modeling::ChainRole::HoleBoundary;});
    });
    if (!compound&&!holes) return false;
    networkTables_ = std::move(network.tables);
    if(omitted&&!closed.empty())networkUnusedJa_=QStringLiteral("重複の余り %1区間（許容 %2 mm以内。元の線と曲線形式は保持）")
        .arg(static_cast<int>(omitted)).arg(tolerance.interactiveJoinMm,0,'f',3);
    for(auto& table:networkTables_) AttachHoleSupports(table);
    for (const auto& selection : selections_) {
        const auto count = std::count_if(network.unusedSegments.begin(), network.unusedSegments.end(),
            [&](const auto& part) { return part.sourceWireId == selection.sourceWireId; });
        if (count == 0) continue;
        if (!networkUnusedJa_.isEmpty()) networkUnusedJa_ += QStringLiteral("、");
        networkUnusedJa_ += QString::fromStdString(selection.label) + QStringLiteral("（%1区間）").arg(static_cast<int>(count));
    }
    app::LoopFacePlan plan;
    for (const auto& table : networkTables_) {
        app::LoopFace face;
        face.method = table.method == modeling::GuideSurfaceMethod::PlanarBoundary ? app::LoopFaceMethod::Planar
            : table.method == modeling::GuideSurfaceMethod::FourEdgePatch ? app::LoopFaceMethod::FourEdge
            : app::LoopFaceMethod::BoundaryFill;
        face.sideCount = table.rows.size();
        for (const auto& row : table.rows)
            face.previewLines.push_back(geometry::SampleChain(row.segments, tolerance.interactiveJoinMm*0.5));
        plan.faces.push_back(std::move(face));
    }
    plan.summaryJa = "接続区画 " + std::to_string(plan.faces.size()) + "（曲面は近似）。元の線の区間を使い、線を変更しません";
    const auto unused=network.unusedSegments.size()+(closed.empty()?0:omitted);
    if (unused)
        plan.summaryJa += "。輪に使わない区間 " + std::to_string(unused) + "（元の線は保持）";
    plan_ = std::move(plan);
    styles_.assign(plan_->faces.size(),modeling::FourEdgeStyle::Coons);
    methodOverride_.assign(plan_->faces.size(), std::nullopt);
    make_.assign(plan_->faces.size(), true);
    continuity_.assign(plan_->faces.size(), {});
    leaveGap_.clear();
    return true;
}

void V2LoopFacesTool::AttachHoleSupports(kachakacha::v2::modeling::GuideTable& table)
{
    using namespace kachakacha::v2;
    const auto tolerance=ToleranceNow();
        if(table.method!=modeling::GuideSurfaceMethod::BoundaryFill)return;
        std::vector<std::vector<geometry::CurveSegment>> loops(1);
        for(const auto& row:table.rows)if(row.role==modeling::ChainRole::BoundarySide)
            loops.front().insert(loops.front().end(),row.segments.begin(),row.segments.end());
        for(const auto& row:table.rows)if(row.role==modeling::ChainRole::HoleBoundary)loops.push_back(row.segments);
        if(loops.size()<2)return;
        const auto& snapshot=window_.session_->GetDocument().Snapshot();
        bool found=false;
        // Prefer visible supports, but hiding a source must not turn a curved
        // projected window into an unrelated refilled surface.
        for(const bool visible:{true,false}) {
        for(const auto* shapes:{&window_.guideShapes_,&window_.partShapes_}) {
            for(const auto& [key,handle]:*shapes) {
                const auto id=base::EntityId::Parse(key);if(!id)continue;
                const auto* entity=window_.session_->GetDocument().FindEntity(*id);
                if(!entity||(entity->kind!=domain::EntityKind::Part&&entity->kind!=domain::EntityKind::GuideSurface)
                    ||app::EntityEffectivelyVisible(snapshot,*entity)!=visible)continue;
                const auto cut=kernel::TrimSurfaceRegion({handle},loops,tolerance,tolerance.interactiveJoinMm);
                if(!cut.HasValue())continue;
                kernel::ReleaseShape(cut.Value());
                modeling::GuideTableRow source;source.role=modeling::ChainRole::SourceSurface;
                source.sourceWireIds={*id};source.sourceLabels={entity->displayName};table.rows.push_back(source);found=true;break;
            }
            if(found)break;
        }
        if(found)break;
        }
        if(!found) {
            // A spatial hole can cross several adjacent support faces. Do not
            // refill its outer wire as an unrelated surface when no single
            // entity contains the complete boundary.
            for(const bool visible:{true,false}) {
                std::vector<modeling::KernelShapeHandle> handles;
                std::vector<base::EntityId> ids;
                for(const auto* shapes:{&window_.guideShapes_,&window_.partShapes_})for(const auto& [key,handle]:*shapes) {
                    const auto id=base::EntityId::Parse(key);if(!id)continue;
                    const auto* entity=window_.session_->GetDocument().FindEntity(*id);
                    if(!entity||(visible&&!app::EntityEffectivelyVisible(snapshot,*entity)))continue;
                    handles.push_back(handle);ids.push_back(*id);
                }
                if(handles.size()<2)continue;
                const auto cut=kernel::TrimSurfaceRegion(handles,loops,tolerance,tolerance.interactiveJoinMm);
                if(!cut.HasValue())continue;
                kernel::ReleaseShape(cut.Value());
                // Record only supports that are actually necessary. Otherwise
                // unrelated visible parts become dependencies of this window.
                for(std::size_t at=0;at<handles.size()&&handles.size()>1;) {
                    auto trial=handles;trial.erase(trial.begin()+at);
                    const auto reduced=kernel::TrimSurfaceRegion(trial,loops,tolerance,tolerance.interactiveJoinMm);
                    if(reduced.HasValue()) {kernel::ReleaseShape(reduced.Value());handles=std::move(trial);ids.erase(ids.begin()+at);}
                    else ++at;
                }
                for(const auto& id:ids) {
                    modeling::GuideTableRow source;source.role=modeling::ChainRole::SourceSurface;
                    source.sourceWireIds={id};source.sourceLabels={window_.session_->GetDocument().FindEntity(id)->displayName};
                    table.rows.push_back(source);
                }
                found=true;break;
            }
        }
}
