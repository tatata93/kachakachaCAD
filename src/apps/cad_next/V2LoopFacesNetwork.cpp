#include "V2LoopFacesTool.h"
#include "V2MainWindow.h"
#include "kachakacha/app/SurfaceNetwork.h"
#include "kachakacha/app/LoopGraph.h"
#include "kachakacha/app/ProfileNetwork.h"
#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/geometry/CurveIntersection.h"
#include "kachakacha/geometry/CurveSampling.h"
#include <algorithm>
#include <cmath>
#include <limits>
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

// Bounds() is sampled, so it cannot prove that a whole spline is exterior.
// Use the exact convex hull for polynomial curves and a containing sphere box
// for arcs/circles. Loose boxes only defer to the detailed intersection check.
std::pair<geometry::Vector3,geometry::Vector3> ContainingBox(const geometry::CurveSegment& curve)
{
    if(curve.Kind()==geometry::CurveKind::Circle||curve.Kind()==geometry::CurveKind::CircularArc){
        const auto center=curve.Center();const double r=curve.Radius();
        return {{center.x-r,center.y-r,center.z-r},{center.x+r,center.y+r,center.z+r}};
    }
    const double infinity=std::numeric_limits<double>::infinity();
    geometry::Vector3 minimum{infinity,infinity,infinity},maximum{-infinity,-infinity,-infinity};
    for(const auto p:curve.ControlPoints()){
        minimum={std::min(minimum.x,p.x),std::min(minimum.y,p.y),std::min(minimum.z,p.z)};
        maximum={std::max(maximum.x,p.x),std::max(maximum.y,p.y),std::max(maximum.z,p.z)};
    }
    return {minimum,maximum};
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

std::vector<geometry::CurveSegment> JoinedStraightIntervals(std::vector<geometry::CurveSegment> curves,double epsilon)
{
    bool changed=true;
    while(changed&&curves.size()>1){changed=false;
        for(std::size_t i=0;i<curves.size();++i){const auto next=(i+1)%curves.size();const auto& a=curves[i];const auto& b=curves[next];
            if(a.Kind()!=geometry::CurveKind::Line||b.Kind()!=geometry::CurveKind::Line
                ||geometry::Distance(a.EndPoint(),b.StartPoint())>epsilon)continue;
            const auto start=a.StartPoint(),end=b.EndPoint();
            if(geometry::Dot(a.EndPoint()-start,end-b.StartPoint())<=0||ChordDistance(a.EndPoint(),start,end)>epsilon)continue;
            const auto joined=geometry::CurveSegment::MakeLine(start,end);if(!joined.HasValue())continue;
            curves[i]=joined.Value();curves.erase(curves.begin()+next);changed=true;break;
        }
    }
    return curves;
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

// A window touching the outer edge is a notch. Resolve its actual 3D contacts
// on temporary intervals, then exclude the independently closed window region.
// Degree-two runs are kept together: the bounded cycle solver sees junctions,
// not every sampling/spline interval. No source entity is split or moved.
app::SurfaceNetworkPlan IndependentPlanarHoles(const std::vector<app::RoleWire>& wires,
    const geometry::GeometryTolerance& tolerance);
// 接触網の出自、閉路基底、穴の所属を一貫して検証するため処理順をまとめる。
app::SurfaceNetworkPlan SelectedNotches(const std::vector<app::RoleWire>& source,
    const geometry::GeometryTolerance& tolerance)
{
    struct Run {std::vector<geometry::CurveSegment> curves;std::vector<base::EntityId> ids;};
    std::vector<modeling::SnapCurve> original;
    for(const auto& wire:source)for(const auto& curve:wire.segments){
        if(!WithinSamplingBudget(curve))return {};
        bool same=false;
        for(const auto& prior:original)same=same||SameInterval(curve,prior.segment,tolerance.modelLinearMm,tolerance.interactiveJoinMm);
        if(!same)original.push_back({wire.id,{},curve});
    }
    // The independent closed-loop path handles ordinary holes and nested islands.
    // This fallback is reserved for a real contact with another contour.
    if(original.size()>128)return {};
    auto exact=tolerance;exact.interactiveJoinMm=exact.modelLinearMm;
    bool contact=false;
    for(std::size_t i=0;i<original.size()&&!contact;++i)for(std::size_t j=i+1;j<original.size()&&!contact;++j)
        for(const auto& hit:geometry::IntersectCurves(original[i].segment,original[j].segment,exact))
            contact=contact||((hit.firstParameter>1e-6&&hit.firstParameter<1.-1e-6)
                ||(hit.secondParameter>1e-6&&hit.secondParameter<1.-1e-6));
    if(!contact)return {};
    const auto runs=[&](const std::vector<modeling::SnapCurve>& curves,bool requireCovered) {
        app::LoopGraph graph(tolerance.modelLinearMm);std::vector<app::LoopEdge> edges;
        for(std::size_t i=0;i<curves.size();++i)edges.push_back(app::MakeLoopEdge(i,{curves[i].segment},graph));
        app::PruneDeadEnds(edges,graph.NodeCount());
        std::vector<std::vector<std::size_t>> at(graph.NodeCount());
        std::vector<std::vector<geometry::CurvePoint>> retained;
        for(std::size_t i=0;i<edges.size();++i)if(edges[i].alive){
            at[edges[i].from].push_back(i);at[edges[i].to].push_back(i);
            auto samples=geometry::SampleCurve(curves[i].segment,tolerance.interactiveJoinMm*.125,16384);
            if(samples.size()>=16384)return std::vector<Run>{};
            retained.push_back(std::move(samples));
        }
        std::vector<bool> used(edges.size());std::vector<Run> result;
        const auto walk=[&](std::size_t start,std::size_t node){
            Run run;auto edge=start;
            while(!used[edge]){
                used[edge]=true;auto curve=curves[edge].segment;const bool forward=edges[edge].from==node;
                if(!forward){const auto reverse=geometry::ReverseCurve(curve);if(!reverse.HasValue())return;curve=reverse.Value();}
                run.curves.push_back(curve);if(std::find(run.ids.begin(),run.ids.end(),curves[edge].entityId)==run.ids.end())run.ids.push_back(curves[edge].entityId);
                node=forward?edges[edge].to:edges[edge].from;
                if(at[node].size()!=2)break;
                edge=at[node][0]==edge?at[node][1]:at[node][0];
            }
            if(!run.curves.empty())result.push_back(std::move(run));
        };
        for(std::size_t node=0;node<at.size();++node)if(at[node].size()!=2)
            for(const auto edge:at[node])if(!used[edge])walk(edge,node);
        for(std::size_t edge=0;edge<edges.size();++edge)if(edges[edge].alive&&!used[edge])walk(edge,edges[edge].from);
        if(requireCovered){
            if(result.size()>24)return std::vector<Run>{};
            app::LoopGraph compact(tolerance.modelLinearMm);std::vector<app::LoopEdge> compactEdges;
            for(std::size_t i=0;i<result.size();++i)compactEdges.push_back(app::MakeLoopEdge(i,result[i].curves,compact));
            auto cycles=app::FindLoopCycles(compactEdges);
            if(cycles.empty()||cycles.size()>=20000)return std::vector<Run>{};
            const auto outer=std::max_element(cycles.begin(),cycles.end(),[&](const auto& a,const auto& b){return app::EnclosedArea(compactEdges,a)<app::EnclosedArea(compactEdges,b);});
            const auto ring=app::CycleRing(compactEdges,*outer);auto plane=geometry::FitPlane(ring);
            geometry::Vector3 area{};for(std::size_t i=0;i<ring.size();++i)area=area+geometry::Cross(ring[i],ring[(i+1)%ring.size()]);
            if(!plane.valid||area.LengthSquared()<1e-20)return std::vector<Run>{};
            plane.normal=geometry::Normalized(area);const auto frame=geometry::MakeFrame(plane);
            const double infinity=std::numeric_limits<double>::infinity();
            geometry::Vector3 minimum{infinity,infinity,infinity},maximum{-infinity,-infinity,-infinity};
            for(std::size_t i=0;i<edges.size();++i)if(edges[i].alive){const auto box=ContainingBox(curves[i].segment);
                minimum={std::min(minimum.x,box.first.x),std::min(minimum.y,box.first.y),std::min(minimum.z,box.first.z)};
                maximum={std::max(maximum.x,box.second.x),std::max(maximum.y,box.second.y),std::max(maximum.z,box.second.z)};}
            std::vector<std::vector<geometry::Point2>> polygons;
            for(const auto& cycle:cycles)if(app::EnclosedArea(compactEdges,cycle)>tolerance.modelLinearMm*tolerance.modelLinearMm)
                polygons.push_back(geometry::ProjectToFrame(app::CycleRing(compactEdges,cycle),frame));
            for(std::size_t i=0;i<edges.size();++i)if(!edges[i].alive){
                const auto& curve=curves[i].segment;
                const auto box=ContainingBox(curve);const auto eps=tolerance.modelLinearMm;
                if(box.second.x<minimum.x-eps||box.first.x>maximum.x+eps||box.second.y<minimum.y-eps
                    ||box.first.y>maximum.y+eps||box.second.z<minimum.z-eps||box.first.z>maximum.z+eps)continue;
                const auto points=geometry::SampleCurve(curve,tolerance.interactiveJoinMm*.125,16384);
                if(points.size()>=16384)return std::vector<Run>{};
                bool covered=true;for(std::size_t j=1;j<points.size();++j)
                    covered=covered&&CoveredChord(curve,points[j-1].parameter,points[j].parameter,retained,tolerance.interactiveJoinMm);
                if(covered)continue;
                // Only exterior tails may be ignored. An open inner window or
                // divider is an error, even when a surrounding loop is closed.
                std::vector<geometry::Vector3> clipped;
                for(const auto& point:points)clipped.push_back(curve.Evaluate(std::clamp(point.parameter,1e-5,1.-1e-5)));
                const auto flat=geometry::ProjectToFrame(clipped,frame);
                for(const auto& polygon:polygons)
                    if(std::any_of(flat.begin(),flat.end(),[&](const auto& p){return geometry::ContainsPoint(polygon,p);})
                        ||geometry::LoopsIntersect(flat,polygon,tolerance.modelLinearMm))return std::vector<Run>{};
            }
        }
        return result;
    };
    std::vector<Run> windows;
    for(const auto& run:runs(original,false))if(geometry::SegmentsFormClosedLoop(run.curves,tolerance))windows.push_back(run);
    if(windows.empty())return {};
    const auto split=app::detail::SplitProfileNetwork(original,exact);
    std::vector<modeling::SnapCurve> unique;
    for(const auto& curve:split){
        bool duplicate=false;for(const auto& prior:unique)
            duplicate=duplicate||SameInterval(curve.segment,prior.segment,exact.modelLinearMm,exact.modelLinearMm);
        if(!duplicate)unique.push_back(curve);
    }
    const auto chains=runs(unique,true);
    if(chains.empty()||chains.size()>24)return {};
    app::LoopGraph graph(exact.modelLinearMm);std::vector<app::LoopEdge> edges;
    for(std::size_t i=0;i<chains.size();++i)edges.push_back(app::MakeLoopEdge(i,chains[i].curves,graph));
    auto cycles=app::FindLoopCycles(edges);
    if(cycles.empty()||cycles.size()>=20000)return {};
    std::stable_sort(cycles.begin(),cycles.end(),[&](const auto& a,const auto& b){return app::EnclosedArea(edges,a)<app::EnclosedArea(edges,b);});
    const auto enclosingRing=app::CycleRing(edges,cycles.back());auto plane=geometry::FitPlane(enclosingRing);
    geometry::Vector3 area{};for(std::size_t i=0;i<enclosingRing.size();++i)area=area+geometry::Cross(enclosingRing[i],enclosingRing[(i+1)%enclosingRing.size()]);
    if(!plane.valid||area.LengthSquared()<1e-20)return {};
    plane.normal=geometry::Normalized(area);const auto frame=geometry::MakeFrame(plane);
    const auto includes=[&](const auto& polygon,const geometry::Point2& p){
        if(geometry::ContainsPoint(polygon,p))return true;
        for(std::size_t j=0;j<polygon.size();++j){const auto a=polygon[j],b=polygon[(j+1)%polygon.size()];
            const double u=b.u-a.u,v=b.v-a.v,d=u*u+v*v;
            const double t=d>0?std::clamp(((p.u-a.u)*u+(p.v-a.v)*v)/d,0.,1.):0.;
            if(std::hypot(p.u-a.u-t*u,p.v-a.v-t*v)<=tolerance.interactiveJoinMm*.25)return true;}
        return false;
    };
    std::vector<std::vector<geometry::Point2>> windowPolygons;
    for(const auto& w:windows)windowPolygons.push_back(geometry::ProjectToFrame(geometry::SampleChain(w.curves,tolerance.interactiveJoinMm*.125),frame));
    std::vector<bool> isWindow(windows.size());
    for(std::size_t i=0;i<windows.size();++i){
        bool touches=false;
        for(const auto& own:windows[i].curves)for(const auto& other:original){
            if(std::any_of(windows[i].curves.begin(),windows[i].curves.end(),[&](const auto& c){return SameInterval(c,other.segment,exact.modelLinearMm,exact.modelLinearMm);}))continue;
            for(const auto& hit:geometry::IntersectCurves(own,other.segment,exact))
                touches=touches||(hit.firstParameter>1e-6&&hit.firstParameter<1.-1e-6)||(hit.secondParameter>1e-6&&hit.secondParameter<1.-1e-6);
        }
        // Independent holes and islands are classified together by the existing
        // containment tree below; only touching contours are subtracted here.
        if(!touches)continue;
        app::ProfileRegion region;region.plane=plane;region.frame=frame;region.outer.planar=windowPolygons[i];
        for(const auto& p:windowPolygons[i])region.outer.sampled.push_back(frame.origin+frame.uDirection*p.u+frame.vDirection*p.v);
        const auto interior=geometry::ProjectToFrame({app::ProfileRegionInterior(region)},frame).front();
        const double size=std::abs(geometry::SignedArea(windowPolygons[i]));
        bool surroundsAnother=false;
        for(std::size_t j=0;j<windows.size();++j)if(i!=j&&size>std::abs(geometry::SignedArea(windowPolygons[j])))
            surroundsAnother=surroundsAnother||std::any_of(windowPolygons[j].begin(),windowPolygons[j].end(),[&](const auto& p){return includes(windowPolygons[i],p);});
        if(surroundsAnother)continue;
        for(const auto& cycle:cycles){const auto polygon=geometry::ProjectToFrame(app::CycleRing(edges,cycle),frame);
            if(std::abs(geometry::SignedArea(polygon))>size+tolerance.modelLinearMm&&includes(polygon,interior))isWindow[i]=true;}
    }
    std::vector<std::vector<bool>> basis(chains.size());
    app::SurfaceNetworkPlan result;bool removedWindow=false;
    for(const auto& cycle:cycles){
        modeling::GuideTableRow row;
        for(std::size_t j=0;j<cycle.edges.size();++j){
            const auto& run=chains[cycle.edges[j]];auto curves=run.curves;
            if(!cycle.forward[j]){std::reverse(curves.begin(),curves.end());for(auto& c:curves){
                const auto reversed=geometry::ReverseCurve(c);if(!reversed.HasValue())return {};c=reversed.Value();}}
            row.segments.insert(row.segments.end(),curves.begin(),curves.end());
            for(const auto id:run.ids)if(std::find(row.sourceWireIds.begin(),row.sourceWireIds.end(),id)==row.sourceWireIds.end()){
                row.sourceWireIds.push_back(id);row.sourceLabels.push_back("選択境界");}
        }
        for(std::size_t i=0;i<row.segments.size();++i)
            if(geometry::Distance(row.segments[i].EndPoint(),row.segments[(i+1)%row.segments.size()].StartPoint())>exact.modelLinearMm)return {};
        row.segments=JoinedStraightIntervals(std::move(row.segments),exact.modelLinearMm);
        const auto points=geometry::SampleChain(row.segments,tolerance.interactiveJoinMm*.125);
        const auto fit=geometry::FitPlane(points);if(!fit.valid)continue;
        modeling::GuideTable table;
        table.method=fit.maximumDeviationMm<=exact.modelLinearMm?modeling::GuideSurfaceMethod::PlanarBoundary:modeling::GuideSurfaceMethod::BoundaryFill;
        row.role=table.method==modeling::GuideSurfaceMethod::PlanarBoundary?modeling::ChainRole::OuterBoundary:modeling::ChainRole::BoundarySide;
        table.rows.push_back(row);
        const auto request=modeling::ToGuideSurfaceRequest(table,tolerance);
        if(!request.HasValue()||!modeling::AnalyzeGuideSurfaceRequest(request.Value(),tolerance).HasValue())continue;
        std::vector<bool> mask(chains.size());for(const auto at:cycle.edges)mask[at]=!mask[at];
        bool independent=false;
        for(std::size_t i=0;i<mask.size();++i)if(mask[i]){
            if(basis[i].empty()){basis[i]=mask;independent=true;break;}
            for(std::size_t j=i;j<mask.size();++j)mask[j]=mask[j]!=basis[i][j];
        }
        if(!independent)continue;
        const auto flat=geometry::ProjectToFrame(points,frame);bool window=false;
        for(std::size_t i=0;i<windows.size();++i)if(isWindow[i])
            window=window||std::all_of(flat.begin(),flat.end(),[&](const auto& p){return includes(windowPolygons[i],p);});
        if(window){removedWindow=true;continue;}
        result.tables.push_back(std::move(table));
    }
    if(removedWindow&&result.tables.size()>1){
        base::DeterministicIdGenerator ids{81286};std::vector<app::RoleWire> loops;
        std::vector<std::pair<base::EntityId,std::vector<base::EntityId>>> sources;
        for(const auto& table:result.tables){
            app::RoleWire loop;loop.id=ids.NextTyped<base::IdKind::Entity>();std::vector<base::EntityId> input;
            for(const auto& row:table.rows){loop.segments.insert(loop.segments.end(),row.segments.begin(),row.segments.end());
                for(const auto id:row.sourceWireIds)if(std::find(input.begin(),input.end(),id)==input.end())input.push_back(id);}
            sources.push_back({loop.id,input});loops.push_back(std::move(loop));
        }
        auto grouped=IndependentPlanarHoles(loops,exact);
        if(grouped.tables.empty())grouped=app::PlanSurfaceNetwork(loops,exact);
        if(grouped.tables.empty())return {};
        for(auto& table:grouped.tables)for(auto& row:table.rows){
            std::vector<base::EntityId> restored;
            for(const auto key:row.sourceWireIds)for(const auto& entry:sources)if(entry.first==key)
                for(const auto id:entry.second)if(std::find(restored.begin(),restored.end(),id)==restored.end())restored.push_back(id);
            if(restored.empty())return {};
            row.sourceWireIds=std::move(restored);row.sourceLabels.assign(row.sourceWireIds.size(),"選択境界");
        }
        result=std::move(grouped);
    }
    return removedWindow&&!result.tables.empty()?result:app::SurfaceNetworkPlan{};
}

app::SurfaceNetworkPlan IndependentPlanarHoles(const std::vector<app::RoleWire>& wires,
    const geometry::GeometryTolerance& tolerance)
{
    modeling::SnapScene scene; base::DeterministicIdGenerator ids{81927};
    for(const auto& wire:wires)for(const auto& curve:wire.segments)
        scene.curves.push_back({wire.id,ids.NextTyped<base::IdKind::Segment>(),curve});
    auto exact=tolerance;exact.interactiveJoinMm=exact.modelLinearMm;
    const auto boundaries=app::DetectSpatialBoundaries(scene,exact);
    if(boundaries.size()<2)return {};
    std::vector<geometry::Vector3> points;
    for(const auto& b:boundaries)points.insert(points.end(),b.sampled.begin(),b.sampled.end());
    const auto fit=geometry::FitPlane(points);
    if(!fit.valid||fit.maximumDeviationMm>exact.modelLinearMm)return {};
    // All selected intervals must belong to a closed boundary. In particular,
    // never accept just the outer rectangle while an inner contour is open.
    for(const auto& c:scene.curves){
        bool used=false;for(const auto& b:boundaries)for(const auto& curve:b.segments)
            used=used||SameInterval(c.segment,curve,exact.modelLinearMm,exact.modelLinearMm);
        if(!used)return {};
    }
    app::SurfaceNetworkPlan result;
    for(const auto& region:app::DetectProfileRegions(scene,exact)){
        modeling::GuideTable table;table.method=modeling::GuideSurfaceMethod::PlanarBoundary;
        const auto append=[&](const auto& boundary,modeling::ChainRole role){
            modeling::GuideTableRow row;row.role=role;row.segments=boundary.segments;
            row.sourceWireIds=boundary.entityIds;row.sourceLabels.assign(row.sourceWireIds.size(),"選択境界");
            table.rows.push_back(std::move(row));
        };
        append(region.outer,modeling::ChainRole::OuterBoundary);
        for(const auto& hole:region.holes)append(hole,modeling::ChainRole::HoleBoundary);
        result.tables.push_back(std::move(table));
    }
    return result;
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
    bool nearGap=false;
    for(std::size_t i=0;i<ends.size();++i) {
        bool exact=false,near=false;
        for(std::size_t j=0;j<ends.size();++j)if(i!=j) {
            const auto d=geometry::Distance(ends[i],ends[j]);
            exact=exact||d<=tolerance.modelLinearMm;near=near||d<=tolerance.interactiveJoinMm;
        }
        nearGap=nearGap||(near&&!exact);
    }
    auto network = IndependentPlanarHoles(wires,tolerance);
    bool notch=false;
    if(network.tables.empty()){network=SelectedNotches(wires,tolerance);notch=!network.tables.empty();}
    if(network.tables.empty()&&!nearGap)network=app::PlanSurfaceNetwork(wires, tolerance);
    if(network.tables.empty()&&!closed.empty()){
        std::vector<app::RoleWire> selected;
        for(const auto& s:selections_)selected.push_back({s.sourceWireId,s.segments});
        network=SelectedNotches(selected,tolerance);
        notch=!network.tables.empty();
    }
    if (network.tables.empty()) return false;
    const bool holes=std::any_of(network.tables.begin(),network.tables.end(),[](const auto& table){
        return std::any_of(table.rows.begin(),table.rows.end(),[](const auto& row){return row.role==modeling::ChainRole::HoleBoundary;});
    });
    if (!compound&&!holes&&!notch) return false;
    networkTables_ = std::move(network.tables);
    if(omitted&&!closed.empty())networkUnusedJa_=QStringLiteral("重複の余り %1区間（許容 %2 mm以内。元の線と曲線形式は保持）")
        .arg(static_cast<int>(omitted)).arg(tolerance.interactiveJoinMm,0,'f',3);
    for(auto& table:networkTables_) {
        const bool opening=notch||std::any_of(table.rows.begin(),table.rows.end(),[](const auto& row){return row.role==modeling::ChainRole::HoleBoundary;});
        AttachSourceSupports(table,opening);
    }
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
        face.sideCount = std::count_if(table.rows.begin(),table.rows.end(),[](const auto& row){return row.role!=modeling::ChainRole::SourceSurface;});
        for (const auto& row : table.rows)
            face.previewLines.push_back(geometry::SampleChain(row.segments, tolerance.interactiveJoinMm*0.5));
        plan.faces.push_back(std::move(face));
    }
    plan.summaryJa = "接続区画 " + std::to_string(plan.faces.size()) + "。元の線の区間を使い、線を変更しません";
    const bool refilled=std::any_of(networkTables_.begin(),networkTables_.end(),[](const auto& table){
        return table.method!=modeling::GuideSurfaceMethod::PlanarBoundary
            &&std::none_of(table.rows.begin(),table.rows.end(),[](const auto& row){return row.role==modeling::ChainRole::SourceSurface;});
    });
    if(refilled)plan.summaryJa+="。新しく張る曲面は近似";
    std::size_t holeCount=0;for(const auto& table:networkTables_)for(const auto& row:table.rows)holeCount+=row.role==modeling::ChainRole::HoleBoundary;
    if(holeCount)plan.summaryJa+="。穴 "+std::to_string(holeCount)+"個";
    for(const auto& table:networkTables_)for(const auto& row:table.rows)
        if(row.role==modeling::ChainRole::SourceSurface&&!row.sourceLabels.empty())
            plan.summaryJa+="。元の面「"+row.sourceLabels.front()+"」の形を保持";
    if(notch)plan.summaryJa+="。外周に接する開口を切り欠きとして保持";
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
