#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctWireProjection.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctSurfaceRegion.h"
#include "kachakacha/kernel/OcctSurfaceExtrude.h"
#include "kachakacha/kernel/OcctThicken.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include "kachakacha/modeling/MeshPick.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/app/SurfaceNetwork.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include <cmath>
#include <functional>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Shape.hxx>
#include <Geom_BSplineSurface.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TColStd_Array2OfReal.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <gp_Ax3.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Cone.hxx>
#include <gp_Sphere.hxx>
#include <gp_Pln.hxx>
using namespace kachakacha::v2;
using test::Require;
namespace {
struct Owned {
    modeling::KernelShapeHandle h;
    explicit Owned(const TopoDS_Shape& shape):h(kernel::StoreShape(shape)){}
    ~Owned(){kernel::ReleaseShape(h);}
};
geometry::CurveSegment Line(geometry::Vector3 a,geometry::Vector3 b){return geometry::CurveSegment::MakeLine(a,b).Value();}
Owned Plane(double z=0){return Owned(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,z),gp_Dir(0,0,1)),-30,30,-30,30).Face());}
std::vector<geometry::CurveSegment> ClosedSquare(double z){return {Line({-2,-2,z},{2,-2,z}),Line({2,-2,z},{2,2,z}),Line({2,2,z},{-2,2,z}),Line({-2,2,z},{-2,-2,z})};}
kernel::WireProjectionOptions Options(kernel::WireProjectionMode mode=kernel::WireProjectionMode::Direction){kernel::WireProjectionOptions o;o.mode=mode;return o;}
void OnSurface(const kernel::WireProjectionResult& r,double tolerance,std::function<double(geometry::Vector3)> distance) {
    for(const auto& wire:r.wires)for(const auto& c:wire)for(int i=0;i<=128;++i)Require(distance(c.Evaluate(i/128.0))<=tolerance,"independent surface distance");
}
}
KACHA_V2_TEST(projection, DirectionClosedAndUnchanged) {
    auto face=Plane();const auto original=kernel::CaptureOutputShape(face.h).Value();
    const auto r=kernel::ProjectNativeWires({face.h},{{ClosedSquare(10),{0,0,-1}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==1&&r.Value().openEnds==0,"closed rectangle");
    OnSurface(r.Value(),1e-6,[](auto p){return std::abs(p.z);});
    Require(kernel::CaptureOutputShape(face.h).Value()==original,"input native face byte identical");
}
KACHA_V2_TEST(projection, ReverseAndDistance) {
    auto face=Plane();auto o=Options();const auto input=Line({-2,0,10},{2,0,10});
    Require(!kernel::ProjectNativeWires({face.h},{{{input},{0,0,1}}},o).HasValue(),"opposite ray not generated");
    o.maximumDistanceMm=5;Require(!kernel::ProjectNativeWires({face.h},{{{input},{0,0,-1}}},o).HasValue(),"distance cutoff");
    o.maximumDistanceMm=11;Require(kernel::ProjectNativeWires({face.h},{{{input},{0,0,-1}}},o).HasValue(),"correct ray");
    Require(!kernel::ProjectNativeWires({face.h},{{{input},{0,0,0}}},o).HasValue(),"zero direction");
}
KACHA_V2_TEST(projection, DifferentDrawingDirectionsJoinAtSeam) {
    auto front=Plane();Owned side(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(1,0,0)),-30,30,-30,30).Face());
    const auto r=kernel::ProjectNativeWires({front.h,side.h},
        {{{Line({-4,0,5},{0,0,5})},{0,0,-1}},{{Line({5,0,0},{5,0,4})},{-1,0,0}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==1&&r.Value().openEnds==2,"directions meet without artificial bridge");
}
KACHA_V2_TEST(projection, DisconnectedSourcesStaySeparate) {
    auto face=Plane();const auto r=kernel::ProjectNativeWires({face.h},{{{Line({-10,0,3},{-5,0,3}),Line({5,0,3},{10,0,3})},{0,0,-1}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==2&&r.Value().openEnds==4,"no bridge between separated paths");
}
KACHA_V2_TEST(projection, CylinderDirectionalNearestSide) {
    Owned cylinder(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(),10),-1.0,1.0,-10,10).Face());
    const auto r=kernel::ProjectNativeWires({cylinder.h},{{{Line({20,-2,0},{20,2,0})},{-1,0,0}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());OnSurface(r.Value(),0.003,[](auto p){return std::abs(std::hypot(p.x,p.y)-10);});
    for(const auto& run:r.Value().wires)for(const auto& c:run)Require(c.Evaluate(.5).x>9,"front side only");
}
KACHA_V2_TEST(projection, ClosestPointOnCylinder) {
    Owned cylinder(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(),10),-1,1,-10,10).Face());
    const auto r=kernel::ProjectNativeWires({cylinder.h},{{{Line({12,-2,0},{12,2,0})},{}}},Options(kernel::WireProjectionMode::Closest));
    Require(r.HasValue(),r.FirstMessageJa());OnSurface(r.Value(),0.003,[](auto p){return std::abs(std::hypot(p.x,p.y)-10);});
}
KACHA_V2_TEST(projection, CurvedIntervalsRemainSingleEditingSegments) {
    Owned cylinder(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(),10),-1,1,-10,10).Face());
    const std::vector<geometry::CurveSegment> sources={
        geometry::CurveSegment::MakeCircularArc({20,0,0},{1,0,0},{0,1,0},2,0,geometry::kPi/2).Value(),
        geometry::CurveSegment::MakeCubicBezier({{20,-3,-2},{20,3,-1},{20,-2,1},{20,3,2}}).Value()};
    for(const auto& source:sources) {
        const auto r=kernel::ProjectNativeWires({cylinder.h},{{{source},{-1,0,0}}},Options());
        Require(r.HasValue(),r.FirstMessageJa());
        std::string counts;for(const auto& run:r.Value().wires){counts+=" "+std::to_string(run.size());
            for(const auto& c:run)counts+=" kind"+std::to_string(static_cast<int>(c.Kind()));
            if(run.size()==2)counts+=" gap"+std::to_string(geometry::Distance(run[0].EndPoint(),run[1].StartPoint()))+" cos"+std::to_string(geometry::Dot(geometry::Normalized(run[0].FirstDerivative(1)),geometry::Normalized(run[1].FirstDerivative(0))));}
        Require(r.Value().wires.size()==1&&r.Value().wires.front().size()==1,"one whole curved interval, not adaptive Bezier fragments; source="+std::to_string(static_cast<int>(source.Kind()))+" counts="+counts);
        Require(r.Value().wires.front().front().Kind()!=geometry::CurveKind::Line,"curve is not flattened to a line");
        Require(r.Value().maxCurveDeviationMm<=Options().toleranceMm,"certified whole-curve error bound retained");
        OnSurface(r.Value(),.003,[](auto p){return std::abs(std::hypot(p.x,p.y)-10);});
    }
    auto plane=Plane();const auto a=geometry::CurveSegment::MakeCircularArc({0,0,3},{0,0,1},{1,0,0},2,0,geometry::kPi/2).Value();
    const auto r=kernel::ProjectNativeWires({plane.h},{{{Line({2,-3,3},{2,0,3}),a,Line({0,2,3},{-3,2,3})},{0,0,-1}}},Options());
    Require(r.HasValue()&&r.Value().wires.size()==1&&r.Value().wires.front().size()==3,"tangent lines stay separate from the curve");
    int lines=0,curves=0;for(const auto& c:r.Value().wires.front())(c.Kind()==geometry::CurveKind::Line?lines:curves)++;
    Require(lines==2&&curves==1,"only curve interval selected as one segment");
}
KACHA_V2_TEST(projection, FullCylinderSelectsFrontHit) {
    Owned cylinder(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(),10),0,geometry::kPi*2,-10,10).Face());
    for(const auto mode:{kernel::WireProjectionMode::Direction,kernel::WireProjectionMode::Closest}) {
        const auto r=kernel::ProjectNativeWires({cylinder.h},{{{Line({20,1,0},{20,2,0})},{-1,0,0}}},Options(mode));
        Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==1,"nearest hit only");
        for(const auto& c:r.Value().wires.front())Require(c.Evaluate(.5).x>9,"back face not generated");
    }
}
KACHA_V2_TEST(projection, AllHitsAndEitherDirection) {
    Owned cylinder(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(),10),0,geometry::kPi*2,-10,10).Face());auto o=Options();
    o.hits=kernel::WireProjectionHits::All;
    const auto all=kernel::ProjectNativeWires({cylinder.h},{{{Line({20,1,0},{20,2,0})},{1,0,0}}},o);
    Require(all.HasValue()&&all.Value().wires.size()==2,"explicit all hits includes opposite solutions");
    o.hits=kernel::WireProjectionHits::NearestEither;
    const auto nearest=kernel::ProjectNativeWires({cylinder.h},{{{Line({20,1,0},{20,2,0})},{1,0,0}}},o);
    Require(nearest.HasValue()&&nearest.Value().wires.size()==1,"either direction nearest");
}
KACHA_V2_TEST(projection, TrainWindowAcrossFrontRadiusAndSide) {
    Owned front(BRepBuilderAPI_MakeFace(gp_Pln(gp_Ax3(gp_Pnt(),gp_Dir(0,-1,0),gp_Dir(1,0,0))),-8,0,-5,5).Face());
    Owned radius(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(gp_Pnt(0,5,0),gp_Dir(0,0,1)),5),-geometry::kPi/2,0,-5,5).Face());
    Owned side(BRepBuilderAPI_MakeFace(gp_Pln(gp_Ax3(gp_Pnt(5,0,0),gp_Dir(1,0,0),gp_Dir(0,1,0))),5,14,-5,5).Face());
    const auto r=kernel::ProjectNativeWires({front.h,radius.h,side.h},
        {{{Line({-4,-5,1},{5,-5,1}),Line({5,-5,-1},{-4,-5,-1}),Line({-4,-5,-1},{-4,-5,1})},{0,1,0}},
        {{Line({10,5,1},{10,10,1}),Line({10,10,1},{10,10,-1}),Line({10,10,-1},{10,5,-1})},{-1,0,0}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());std::string ends;
    for(const auto& run:r.Value().wires){const auto a=run.front().StartPoint(),b=run.back().EndPoint();ends+=" ["+std::to_string(a.x)+","+std::to_string(a.y)+","+std::to_string(a.z)+" -> "+std::to_string(b.x)+","+std::to_string(b.y)+","+std::to_string(b.z)+"]";}
    Require(r.Value().wires.size()==1&&r.Value().openEnds==0,"front / R / side closed window"+ends);
    OnSurface(r.Value(),.003,[](auto p){if(p.x<0)return std::abs(p.y);if(p.y>5)return std::abs(p.x-5);return std::abs(std::hypot(p.x,p.y-5)-5);});
}
KACHA_V2_TEST(projection, BranchesDoNotCreateArbitraryChain) {
    auto face=Plane();const auto r=kernel::ProjectNativeWires({face.h},
        {{{Line({-2,0,3},{0,0,3}),Line({0,0,3},{2,0,3}),Line({0,0,3},{0,2,3})},{0,0,-1}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==3,"branch remains explicit separate arms");
}
KACHA_V2_TEST(projection, RoundedPanoramaAndSpatialRegion) {
    Owned front(BRepBuilderAPI_MakeFace(gp_Pln(gp_Ax3(gp_Pnt(),gp_Dir(0,-1,0),gp_Dir(1,0,0))),-8,0,-5,5).Face());
    Owned radius(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(gp_Pnt(0,5,0),gp_Dir(0,0,1)),5),-geometry::kPi/2,0,-5,5).Face());
    Owned side(BRepBuilderAPI_MakeFace(gp_Pln(gp_Ax3(gp_Pnt(5,0,0),gp_Dir(1,0,0),gp_Dir(0,1,0))),5,14,-5,5).Face());
    const auto arc=[](geometry::Vector3 center,geometry::Vector3 normal,geometry::Vector3 ref,double a){
        return geometry::CurveSegment::MakeCircularArc(center,normal,ref,.25,a,geometry::kPi/2).Value();};
    const std::vector<geometry::CurveSegment> a={Line({5,-5,1},{-3.75,-5,1}),arc({-3.75,-5,.75},{0,-1,0},{1,0,0},geometry::kPi/2),
        Line({-4,-5,.75},{-4,-5,-.75}),arc({-3.75,-5,-.75},{0,-1,0},{1,0,0},geometry::kPi),Line({-3.75,-5,-1},{5,-5,-1})};
    const std::vector<geometry::CurveSegment> b={Line({10,5,-1},{10,9.75,-1}),arc({10,9.75,-.75},{1,0,0},{0,1,0},-geometry::kPi/2),
        Line({10,10,-.75},{10,10,.75}),arc({10,9.75,.75},{1,0,0},{0,1,0},0),Line({10,9.75,1},{10,5,1})};
    const auto projected=kernel::ProjectNativeWires({front.h,radius.h,side.h},{{a,{0,1,0}},{b,{-1,0,0}}},Options());
    Require(projected.HasValue(),projected.FirstMessageJa());
    Require(projected.Value().wires.size()==1&&projected.Value().openEnds==0,"rounded panorama closes over three faces");
    for(const auto& source:{a[1],a[3],b[1],b[3]})for(int i=0;i<=32;++i){auto p=source.Evaluate(i/32.0);if(p.x==10)p.x=5;else p.y=0;
        double error=1e100;for(const auto& c:projected.Value().wires.front())error=std::min(error,c.ClosestPoint(p).distance);
        Require(error<.002,"all four rounded corners survive projection");}
    const auto patch=kernel::TrimSurfaceRegion({front.h,radius.h,side.h},projected.Value().wires,{});
    Require(patch.HasValue(),patch.FirstMessageJa());
    const auto solid=kernel::BuildSurfaceExtrude(patch.Value(),{1,-1,0},0,1,{});
    Require(solid.HasValue(),solid.FirstMessageJa());Require(solid.Value().totalVolumeMm3>1,"uniform straight extrusion across three faces");
    for(const auto& part:solid.Value().parts)kernel::ReleaseShape(part.handle);
    kernel::ReleaseShape(patch.Value());
}
KACHA_V2_TEST(projection, SpatialWindowHoleThicknessAndExtrusionMatrix) {
    Owned front(BRepBuilderAPI_MakeFace(gp_Pln(gp_Ax3(gp_Pnt(),gp_Dir(0,-1,0),gp_Dir(1,0,0))),-8,0,-5,5).Face());
    Owned radius(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(gp_Pnt(0,5,0),gp_Dir(0,0,1)),5),-geometry::kPi/2,0,-5,5).Face());
    Owned side(BRepBuilderAPI_MakeFace(gp_Pln(gp_Ax3(gp_Pnt(5,0,0),gp_Dir(1,0,0),gp_Dir(0,1,0))),5,14,-5,5).Face());
    const std::vector<modeling::KernelShapeHandle> supports={front.h,radius.h,side.h};
    const auto loop=[&](double left,double right,double z) {
        return kernel::ProjectNativeWires(supports,
            {{{Line({left,-5,z},{5,-5,z}),Line({5,-5,-z},{left,-5,-z}),Line({left,-5,-z},{left,-5,z})},{0,1,0}},
             {{Line({10,5,z},{10,right,z}),Line({10,right,z},{10,right,-z}),Line({10,right,-z},{10,5,-z})},{-1,0,0}}},Options());
    };
    const auto outer=loop(-7,13,4),hole=loop(-3,10,1);
    Require(outer.HasValue()&&hole.HasValue(),"outer and hole both span front, curved corner and side");
    const auto cut=kernel::TrimSurfaceRegion(supports,{outer.Value().wires.front(),hole.Value().wires.front()},{});
    Require(cut.HasValue(),cut.FirstMessageJa());
    const auto checkHole=[&](modeling::KernelShapeHandle shape,bool closed) {
        const auto mesh=kernel::BuildShapeMesh(shape,.01);Require(mesh.HasValue(),mesh.FirstMessageJa());
        bool holeHit=false,wallHit=false;
        for(const auto& t:mesh.Value().triangles) {
            holeHit=holeHit||modeling::RayHitsTriangle({-4,2,0},{1,-1,0},t).has_value();
            wallHit=wallHit||modeling::RayHitsTriangle({-4,2,3},{1,-1,0},t).has_value();
        }
        Require(!holeHit&&wallHit,"real empty window with surrounding original material");
        if(closed){const auto quality=kernel::CheckSolidForExport(shape,.001);Require(quality.HasValue()&&quality.Value().Ok()&&quality.Value().volumeMm3>0,"valid closed positive-volume solid");}
    };
    checkHole(cut.Value(),false);
    for(const auto placement:{fabrication::ThicknessPlacement::Inside,fabrication::ThicknessPlacement::Centered,fabrication::ThicknessPlacement::Outside}) {
        const auto thick=kernel::ThickenSurface(cut.Value(),.2,placement,{});
        Require(thick.HasValue(),thick.FirstMessageJa());checkHole(thick.Value().handle,true);kernel::ReleaseShape(thick.Value().handle);
    }
    for(const auto range:{std::pair{0.,.4},std::pair{-.2,.2},std::pair{-.4,0.}}) {
        const auto solid=kernel::BuildSurfaceExtrude(cut.Value(),{1,-1,0},range.first,range.second,{});
        Require(solid.HasValue(),solid.FirstMessageJa());
        for(const auto& part:solid.Value().parts){checkHole(part.handle,true);kernel::ReleaseShape(part.handle);}
    }
    Owned stop(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(20,-20,0),gp_Dir(1,-1,0)),-40,40,-40,40).Face());
    for(const double offset:{-.2,0.,.2}) {
        modeling::ExtrudeRequest request;request.customDirection={1,-1,0};
        const auto solid=kernel::BuildExtrudeToFace(request,cut.Value(),stop.h,{},offset);
        Require(solid.HasValue(),solid.FirstMessageJa());
        for(const auto& part:solid.Value().parts){checkHole(part.handle,true);kernel::ReleaseShape(part.handle);}
    }
    kernel::ReleaseShape(cut.Value());
}
KACHA_V2_TEST(projection, CurvedNetworkIntersectionsHoleAndStraightExtrusion) {
    Owned body(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(gp_Pnt(),gp_Dir(0,0,1)),10),
        -geometry::kPi*.75,-geometry::kPi*.25,-5,5).Face());
    const auto y=-20.;
    const std::vector<std::vector<geometry::CurveSegment>> drawings={
        {Line({-5,y,-4},{5,y,-4}),Line({5,y,-4},{5,y,4}),Line({5,y,4},{-5,y,4}),Line({-5,y,4},{-5,y,-4})},
        {geometry::CurveSegment::MakeCircle({-2,y,0},{0,1,0},{1,0,0},1).Value()},
        {Line({0,y,-4},{0,y,4})}};
    base::DeterministicIdGenerator ids{1031};std::vector<app::RoleWire> wires;
    for(const auto& curves:drawings) {
        const auto projected=kernel::ProjectNativeWires({body.h},{{curves,{0,1,0}}},Options());
        Require(projected.HasValue()&&projected.Value().wires.size()==1,"project each source without joining the branch into the outer loop");
        wires.push_back({ids.NextTyped<base::IdKind::Entity>(),projected.Value().wires.front()});
    }
    const auto plan=app::PlanSurfaceNetwork(wires,{});
    Require(plan.tables.size()==2&&plan.unusedSegments.empty(),"spatial T intersections make two regions without dropping wires");
    int holes=0;bool holeHit=false,wallHit=false;
    for(const auto& table:plan.tables) {
        std::vector<std::vector<geometry::CurveSegment>> loops(1);
        for(const auto& row:table.rows)if(row.role==modeling::ChainRole::BoundarySide||row.role==modeling::ChainRole::OuterBoundary)
            loops.front().insert(loops.front().end(),row.segments.begin(),row.segments.end());
        for(const auto& row:table.rows)if(row.role==modeling::ChainRole::HoleBoundary){loops.push_back(row.segments);++holes;}
        if(loops.size()>1) {
            Require(table.method==modeling::GuideSurfaceMethod::BoundaryFill,"spatial hole uses the supported surface method after partitioning");
            auto request=modeling::ToGuideSurfaceRequest(table,{});Require(request.HasValue(),"ordinary surface request retains the hole");
            modeling::GuideChain source;source.role=modeling::ChainRole::SourceSurface;source.supportShapeHandle=body.h.value;
            auto input=request.Value();input.chains.push_back(source);
            const auto analysis=modeling::AnalyzeGuideSurfaceRequest(input,{});Require(analysis.HasValue(),analysis.FirstMessageJa());
            const auto built=kernel::BuildGuideSurface(input,analysis.Value(),{});
            Require(built.HasValue(),built.FirstMessageJa());kernel::ReleaseShape(built.Value().handle);
        }
        const auto region=kernel::TrimSurfaceRegion({body.h},loops,{},Options().toleranceMm);
        Require(region.HasValue(),region.FirstMessageJa());
        const auto solid=kernel::BuildSurfaceExtrude(region.Value(),{0,1,0},0,.3,{});
        Require(solid.HasValue(),solid.FirstMessageJa());
        for(const auto& part:solid.Value().parts) {
            const auto check=kernel::CheckSolidForExport(part.handle,.001);Require(check.HasValue()&&check.Value().Ok(),"each intersected spatial region is a safe solid");
            const auto mesh=kernel::BuildShapeMesh(part.handle,.01);Require(mesh.HasValue(),"solid mesh");
            for(const auto& t:mesh.Value().triangles){holeHit=holeHit||modeling::RayHitsTriangle({-2,-20,0},{0,1,0},t).has_value();
                wallHit=wallHit||modeling::RayHitsTriangle({-4,-20,0},{0,1,0},t).has_value();}
            kernel::ReleaseShape(part.handle);
        }
        kernel::ReleaseShape(region.Value());
    }
    Require(holes==1&&!holeHit&&wallHit,"one real window hole survives network splitting and extrusion");
}
KACHA_V2_TEST(projection, SurfaceRegionHolesAndInvalidBoundary) {
    auto plane=Plane();auto outer=ClosedSquare(0);
    const auto hole=geometry::CurveSegment::MakeCircle({0,0,0},{0,0,1},{1,0,0},.5).Value();
    const auto patch=kernel::TrimSurfaceRegion({plane.h},{outer,{hole}},{});
    Require(patch.HasValue(),patch.FirstMessageJa());
    const auto solid=kernel::BuildSurfaceExtrude(patch.Value(),{0,0,1},0,2,{});
    Require(solid.HasValue(),solid.FirstMessageJa());
    Require(std::abs(solid.Value().totalVolumeMm3-2*(16-geometry::kPi*.25))<1e-5,"window is a true hole in the extruded solid");
    for(const auto& part:solid.Value().parts)kernel::ReleaseShape(part.handle);
    kernel::ReleaseShape(patch.Value());
    Require(!kernel::TrimSurfaceRegion({plane.h},{ClosedSquare(1)},{}).HasValue(),"off-surface boundary cannot silently project");
    outer.pop_back();Require(!kernel::TrimSurfaceRegion({plane.h},{outer},{}).HasValue(),"open boundary cannot create a face");
}
KACHA_V2_TEST(projection, SurfaceHoleSourceSeparateFromContinuityAndReportsDeviation) {
    auto plane=Plane();modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::BoundaryFill;
    modeling::GuideChain outer;outer.role=modeling::ChainRole::BoundarySide;outer.closed=true;outer.segments=ClosedSquare(0);
    // A continuity support is not the surface to cut from.
    auto other=Plane(20);outer.supportShapeHandle=other.h.value;request.chains.push_back(outer);
    modeling::GuideChain hole;hole.role=modeling::ChainRole::HoleBoundary;hole.closed=true;
    hole.segments={geometry::CurveSegment::MakeCircle({0,0,0},{0,0,1},{1,0,0},.5).Value()};request.chains.push_back(hole);
    auto made=kernel::BuildSurfaceWithHoles(request,{});Require(made.HasValue(),made.FirstMessageJa());kernel::ReleaseShape(made.Value().handle);
    modeling::GuideChain source;source.role=modeling::ChainRole::SourceSurface;source.supportShapeHandle=plane.h.value;
    request.chains.push_back(source);
    request.chains[0].segments=ClosedSquare(.0001);
    request.chains[1].segments={geometry::CurveSegment::MakeCircle({0,0,.0001},{0,0,1},{1,0,0},.5).Value()};
    made=kernel::BuildSurfaceWithHoles(request,{});Require(made.HasValue(),made.FirstMessageJa());
    Require(!made.Diagnostics().empty()&&made.Value().maximumDeviationMm>=.000099&&made.Value().rmsDeviationMm>=.000099,
        "boundary approximation warning, maximum and RMS retained");
    kernel::ReleaseShape(made.Value().handle);
    request.chains.back().supportShapeHandle=0;
    Require(!kernel::BuildSurfaceWithHoles(request,{}).HasValue(),"missing explicit source cannot silently refill");
}
KACHA_V2_TEST(projection, SurfaceRegionOrderAndInteriorPatches) {
    std::vector<modeling::KernelShapeHandle> patches;
    for(int x=-2;x<2;++x)for(int y=-2;y<2;++y)
        patches.push_back(kernel::StoreShape(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(),gp_Dir(0,0,1)),x,x+1,y,y+1).Face()));
    auto outer=ClosedSquare(0);std::swap(outer[0],outer[2]);
    outer[1]=geometry::ReverseCurve(outer[1]).Value();
    const auto region=kernel::TrimSurfaceRegion(patches,{outer},{});
    Require(region.HasValue(),region.FirstMessageJa());
    const auto solid=kernel::BuildSurfaceExtrude(region.Value(),{0,0,1},0,1,{});
    Require(solid.HasValue(),solid.FirstMessageJa());
    Require(std::abs(solid.Value().totalVolumeMm3-16)<1e-5,"interior patches retained, shuffled and reversed edges accepted");
    for(const auto& part:solid.Value().parts)kernel::ReleaseShape(part.handle);
    kernel::ReleaseShape(region.Value());for(auto patch:patches)kernel::ReleaseShape(patch);
}
KACHA_V2_TEST(projection, SurfaceRegionNeverIncludesCylinderRear) {
    Owned cylinder(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(),10),0,geometry::kPi*2,-10,10).Face());
    const auto circle=geometry::CurveSegment::MakeCircle({20,0,0},{1,0,0},{0,1,0},2).Value();
    const auto projected=kernel::ProjectNativeWires({cylinder.h},{{{circle},{-1,0,0}}},Options());
    Require(projected.HasValue(),projected.FirstMessageJa());
    const auto patch=kernel::TrimSurfaceRegion({cylinder.h},projected.Value().wires,{},Options().toleranceMm);
    Require(patch.HasValue(),patch.FirstMessageJa());
    const auto mesh=kernel::BuildShapeMesh(patch.Value());Require(mesh.HasValue(),mesh.FirstMessageJa());
    Require(mesh.Value().minimum.x>9,"front-only window never generates the opposite patch");
    kernel::ReleaseShape(patch.Value());
}
KACHA_V2_TEST(projection, ObliqueProjectionKeepsNearestFaceAndSourceParameter) {
    auto first=Plane(10),second=Plane(20);
    const auto r=kernel::ProjectNativeWires({first.h,second.h},{{{Line({0,0,0},{10,0,0})},{1,0,1}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==1,"oblique first hit only");
    OnSurface(r.Value(),1e-6,[](auto p){return std::abs(p.z-10);});
    Require(geometry::Distance(r.Value().wires[0].front().StartPoint(),{10,0,10})<.001,"ray origin source start");
}
KACHA_V2_TEST(projection, PartialOcclusionUsesNearFaceOnlyInOverlappingInterval) {
    Owned first(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,10),gp_Dir(0,0,1)),5,10,-5,5).Face());auto second=Plane(20);
    const auto r=kernel::ProjectNativeWires({first.h,second.h},{{{Line({0,0,0},{10,0,0})},{0,0,1}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==2,"two depth intervals, no spurious connecting edge");
    for(const auto& run:r.Value().wires)for(const auto& c:run)for(int i=1;i<64;++i){const auto p=c.Evaluate(i/64.0);Require(std::abs(p.z-(p.x<5?20:10))<.001,"nearest in partial overlap");}
}
KACHA_V2_TEST(projection, IntersectingTargetsChangeNearestSide) {
    auto flat=Plane(10);Owned tilted(BRepBuilderAPI_MakeFace(gp_Pln(gp_Ax3(gp_Pnt(5,0,10),gp_Dir(-1,0,1),gp_Dir(1,0,1))),-20,20,-5,5).Face());
    const auto r=kernel::ProjectNativeWires({flat.h,tilted.h},{{{Line({0,0,0},{10,0,0})},{0,0,1}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());
    for(const auto& run:r.Value().wires)for(const auto& c:run)for(int i=0;i<=64;++i){const auto p=c.Evaluate(i/64.0);Require(std::abs(p.z-std::min(10.0,p.x+5))<.001,"first surface changes at native crossing");}
}
KACHA_V2_TEST(projection, WrapCylinderPreservesLengthAndClosure) {
    Owned cylinder(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(),10),-1,1,-10,10).Face());auto o=Options(kernel::WireProjectionMode::Wrap);o.targetPoint={10,0,0};
    const auto r=kernel::ProjectNativeWires({cylinder.h},{{ClosedSquare(0),{}}},o);
    Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==1&&r.Value().openEnds==0,"wrapped closed outline");
    double length=0;for(const auto& c:r.Value().wires.front())length+=c.TotalLength(1e-7);
    Require(std::abs(length-16)<0.001,"cylinder physical perimeter preserved");
    OnSurface(r.Value(),0.001,[](auto p){return std::abs(std::hypot(p.x,p.y)-10);});
}
KACHA_V2_TEST(projection, WrapConePreservesLength) {
    Owned cone(BRepBuilderAPI_MakeFace(gp_Cone(gp_Ax3(),0.4,10),-1,1,-10,10).Face());auto o=Options(kernel::WireProjectionMode::Wrap);o.targetPoint={10,0,0};
    const auto r=kernel::ProjectNativeWires({cone.h},{{{Line({-2,0,0},{2,0,0})},{}}},o);
    Require(r.HasValue(),r.FirstMessageJa());double length=0;for(const auto& run:r.Value().wires)for(const auto& c:run)length+=c.TotalLength(1e-7);
    Require(std::abs(length-4)<0.001,"cone geodesic length preserved");
    OnSurface(r.Value(),0.001,[](auto p){return std::abs(std::hypot(p.x,p.y)-(10+p.z*std::tan(0.4)));});
}
KACHA_V2_TEST(projection, WrapPlaneRotationAndNonplanarRefusal) {
    auto face=Plane();auto o=Options(kernel::WireProjectionMode::Wrap);o.targetPoint={3,4,0};o.wrapRotationRad=geometry::kPi/2;
    const auto r=kernel::ProjectNativeWires({face.h},{{{Line({0,0,0},{2,0,0})},{}}},o);
    Require(r.HasValue(),r.FirstMessageJa());Require(geometry::Distance(r.Value().wires.front().back().EndPoint(),{3,6,0})<0.001,"rotation and anchor");
    Require(!kernel::ProjectNativeWires({face.h},{{{Line({0,0,0},{2,0,1})},{}}},o).HasValue(),"nonplanar input refused");
}
KACHA_V2_TEST(projection, CompoundCurvatureWrapRefusedButProjectionWorks) {
    Owned sphere(BRepBuilderAPI_MakeFace(gp_Sphere(gp_Ax3(),10),-1,1,-0.8,0.8).Face());auto o=Options(kernel::WireProjectionMode::Wrap);o.targetPoint={10,0,0};
    const auto r=kernel::ProjectNativeWires({sphere.h},{{{Line({-1,0,0},{1,0,0})},{}}},o);
    Require(!r.HasValue()&&r.FirstCode()==kernel::kWrapNotDevelopable,"honest compound curvature refusal");
    const auto projected=kernel::ProjectNativeWires({sphere.h},{{{Line({20,-1,0},{20,1,0})},{-1,0,0}}},Options());
    Require(projected.HasValue(),projected.FirstMessageJa());OnSurface(projected.Value(),0.003,[](auto p){return std::abs(p.Length()-10);});
}
KACHA_V2_TEST(projection, FaceHoleClipsOutline) {
    BRepBuilderAPI_MakeFace builder(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-10,10,-10,10);
    BRepBuilderAPI_MakePolygon hole;hole.Add(gp_Pnt(-1,-1,0));hole.Add(gp_Pnt(-1,1,0));hole.Add(gp_Pnt(1,1,0));hole.Add(gp_Pnt(1,-1,0));hole.Close();builder.Add(hole.Wire());Owned face(builder.Face());
    const auto r=kernel::ProjectNativeWires({face.h},{{{Line({-5,0,4},{5,0,4})},{0,0,-1}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==2,"hole leaves two unbridged portions");
}
KACHA_V2_TEST(projection, InvalidToleranceAndOutside) {
    auto face=Plane();auto o=Options();o.toleranceMm=0;
    Require(!kernel::ProjectNativeWires({face.h},{{{Line({0,0,2},{1,0,2})},{0,0,-1}}},o).HasValue(),"invalid tolerance");
    Require(!kernel::ProjectNativeWires({face.h},{{{Line({100,0,2},{101,0,2})},{0,0,-1}}},Options()).HasValue(),"outside selected surface");
}
KACHA_V2_TEST(projection, CircularAndBezierSourcesStaySmooth) {
    auto face=Plane();const auto circle=geometry::CurveSegment::MakeCircle({0,0,10},{0,0,1},{1,0,0},4).Value();
    const auto closed=kernel::ProjectNativeWires({face.h},{{{circle},{0,0,-1}}},Options());
    Require(closed.HasValue(),closed.FirstMessageJa());Require(closed.Value().wires.size()==1&&closed.Value().openEnds==0,"closed circular projection");
    Require(closed.Value().wires.front().front().Kind()==geometry::CurveKind::Circle,"analytic circle preserved");
    const auto cubic=geometry::CurveSegment::MakeCubicBezier({{-4,0,10},{-2,3,10},{2,-3,10},{4,0,10}}).Value();
    const auto spline=kernel::ProjectNativeWires({face.h},{{{cubic},{0,0,-1}}},Options());
    Require(spline.HasValue(),spline.FirstMessageJa());OnSurface(spline.Value(),.001,[](auto p){return std::abs(p.z);});
    Require(spline.Value().wires.front().front().Kind()!=geometry::CurveKind::Line,"curved source never flattened to lines");
}
KACHA_V2_TEST(projection, PeriodicComplementaryArcsOnSplinePlane) {
    TColgp_Array2OfPnt p(1,2,1,2);
    for(int i=1;i<=2;++i)for(int j=1;j<=2;++j){const double x=i==1?-30:30,y=j==1?-30:30;p.SetValue(i,j,gp_Pnt(x,y,.08*x));}
    TColStd_Array1OfReal k(1,2);k.SetValue(1,0);k.SetValue(2,1);
    TColStd_Array1OfInteger m(1,2);m.SetValue(1,2);m.SetValue(2,2);
    occ::handle<Geom_BSplineSurface> surface=new Geom_BSplineSurface(p,k,k,m,m,1,1);
    Owned face(BRepBuilderAPI_MakeFace(surface,1e-7).Face());
    for(const double radius:{.1,.75,1.25,1.5,4.0})for(const double phase:{0.0,.4,1.7})for(const double sign:{-1.0,1.0}) {
        const auto circle=geometry::CurveSegment::MakeCircle({2,0,10},{0,0,sign},{std::cos(phase),std::sin(phase),0},radius).Value();
        const auto r=kernel::ProjectNativeWires({face.h},{{{circle},{0,0,-1}}},Options());
        Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==1&&r.Value().openEnds==0,"periodic complementary arcs stay closed");
        OnSurface(r.Value(),.001,[](auto q){return std::abs(q.z-.08*q.x);});
        for(int i=0;i<256;++i){const auto original=circle.Evaluate(i/256.0);const geometry::Vector3 expected{original.x,original.y,.08*original.x};double distance=1e100;
            for(const auto& c:r.Value().wires.front())distance=std::min(distance,c.ClosestPoint(expected).distance);
            double dense=1e100;if(distance>=.002)for(const auto& c:r.Value().wires.front())for(int j=0;j<=16000;++j)dense=std::min(dense,geometry::Distance(c.Evaluate(j/16000.),expected));
            Require(distance<.002,"every angle covered by the projected lamp radius="+std::to_string(radius)+" phase="+std::to_string(phase)+" sign="+std::to_string(sign)+" distance="+std::to_string(distance)+" dense="+std::to_string(dense)+" pieces="+std::to_string(r.Value().wires.front().size()));}
    }
}
KACHA_V2_TEST(projection, PeriodicCircleWithPartialNearLayer) {
    auto back=Plane();Owned near(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,2),gp_Dir(0,0,1)),0,30,-30,30).Face());
    const auto circle=geometry::CurveSegment::MakeCircle({0,0,10},{0,0,1},{0,1,0},4).Value();
    const auto r=kernel::ProjectNativeWires({back.h,near.h},{{{circle},{0,0,-1}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());
    for(const auto& run:r.Value().wires)for(const auto& c:run)for(int i=1;i<16;++i){const auto q=c.Evaluate(i/16.0);
        Require(std::abs(q.z-(q.x>1e-4?2:0))<.002,"nearest layer chosen around the periodic seam");}
    Require(r.Value().openEnds==4,"different depths do not receive artificial joining walls");
}
KACHA_V2_TEST(projection, PeriodicCylinderCircleKeepsFullFrontLoop) {
    Owned cylinder(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(),10),0,geometry::kPi*2,-10,10).Face());
    for(const double phase:{0.0,.8,2.4}) {
        const auto circle=geometry::CurveSegment::MakeCircle({20,0,0},{1,0,0},{0,std::cos(phase),std::sin(phase)},2).Value();
        const auto r=kernel::ProjectNativeWires({cylinder.h},{{{circle},{-1,0,0}}},Options());
        Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==1&&r.Value().openEnds==0,"cylinder front circle is closed across its seam");
        OnSurface(r.Value(),.003,[](auto q){return std::abs(std::hypot(q.x,q.y)-10);});
        for(const auto& c:r.Value().wires.front())for(int i=0;i<=16;++i)Require(c.Evaluate(i/16.0).x>9,"rear circumference excluded");
    }
}
KACHA_V2_TEST(projection, ClippedCircleNeverInventsMissingArc) {
    Owned face(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(),gp_Dir(0,0,1)),0,10,-10,10).Face());
    const auto circle=geometry::CurveSegment::MakeCircle({0,0,3},{0,0,1},{1,0,0},2).Value();
    const auto r=kernel::ProjectNativeWires({face.h},{{{circle},{0,0,-1}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().openEnds==2,"out-of-face half remains open");
    OnSurface(r.Value(),1e-6,[](auto q){return q.x<0?-q.x:std::abs(q.z);});
}
KACHA_V2_TEST(projection, NativeCurveFormsAndUnnecessarySplits) {
    auto face=Plane();const auto circle=geometry::CurveSegment::MakeCircle({0,0,3},{0,0,1},{1,0,0},2).Value();
    const auto half=circle.Split(.5).Value();const auto first=half.first->Split(.5).Value(),last=half.second->Split(.5).Value();
    const auto r=kernel::ProjectNativeWires({face.h},{{{*first.first,*first.second,*last.first,*last.second},{0,0,-1}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==1&&r.Value().wires.front().size()==1
        &&r.Value().wires.front().front().Kind()==geometry::CurveKind::Circle,"four exact arcs merge into an analytic circle");
    const auto line=kernel::ProjectNativeWires({face.h},{{{Line({-4,0,3},{0,0,3}),Line({0,0,3},{4,0,3})},{0,0,-1}}},Options());
    Require(line.HasValue()&&line.Value().wires.front().size()==1&&line.Value().wires.front().front().Kind()==geometry::CurveKind::Line,"collinear source splits do not create extra projected segments");
}
KACHA_V2_TEST(projection, PlanarSplineSupportPreservesSourceTypes) {
    TColgp_Array2OfPnt p(1,2,1,2);for(int i=1;i<=2;++i)for(int j=1;j<=2;++j)p.SetValue(i,j,gp_Pnt(i==1?-30:30,j==1?-30:30,0));
    TColStd_Array1OfReal k(1,2);k.SetValue(1,0);k.SetValue(2,1);TColStd_Array1OfInteger m(1,2);m.SetValue(1,2);m.SetValue(2,2);
    occ::handle<Geom_BSplineSurface> surface=new Geom_BSplineSurface(p,k,k,m,m,1,1);Owned face(BRepBuilderAPI_MakeFace(surface,1e-7).Face());
    const std::vector<geometry::CurveSegment> sources={Line({-2,0,3},{2,0,3}),
        geometry::CurveSegment::MakeCircle({0,0,3},{0,0,1},{1,0,0},2).Value(),
        geometry::CurveSegment::MakeCircularArc({0,0,3},{0,0,1},{1,0,0},2,.3,-1.7).Value(),
        geometry::CurveSegment::MakeCubicBezier({{-2,0,3},{-1,2,3},{1,-2,3},{2,0,3}}).Value(),
        geometry::CurveSegment::MakeCubicBSpline({{-3,0,3},{-2,2,3},{0,-2,3},{2,2,3},{3,0,3}}).Value()};
    for(const auto mode:{kernel::WireProjectionMode::Direction,kernel::WireProjectionMode::Closest})for(const auto& source:sources) {
        const auto r=kernel::ProjectNativeWires({face.h},{{{source},{0,0,-1}}},Options(mode));
        Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==1&&r.Value().wires.front().size()==1,"untrimmed source stays a single segment");
        const auto& output=r.Value().wires.front().front();Require(output.Kind()==source.Kind(),"line circle arc Bezier and B-spline retain their native core type");
        for(int i=0;i<=128;++i){auto expected=source.Evaluate(i/128.0);expected.z=0;Require(geometry::Distance(output.Evaluate(i/128.0),expected)<1e-7,"full source parameterization preserved");}
    }
}
KACHA_V2_TEST(projection, ClosedBSplinePartialLayersStayCurved) {
    auto back=Plane();Owned near(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,2),gp_Dir(0,0,1)),0,30,-30,30).Face());
    const auto source=geometry::CurveSegment::MakeCubicBSpline({{0,2,10},{-2,0,10},{0,-2,10},{2,0,10},{0,2,10},{-2,0,10},{0,-2,10}}).Value();
    const auto r=kernel::ProjectNativeWires({back.h,near.h},{{{source},{0,0,-1}}},Options());Require(r.HasValue(),r.FirstMessageJa());
    for(const auto& run:r.Value().wires)for(const auto& c:run)for(int i=1;i<16;++i){const auto q=c.Evaluate(i/16.0);
        Require(std::abs(q.z-(q.x>1e-4?2:0))<.002,"closed spline resolves partial near layer without flattening");}
    Require(r.Value().openEnds==4,"depth discontinuities remain explicit");
}
KACHA_V2_TEST(projection, SmallCurvesDoNotCollapseInsideInteractiveJoinRadius) {
    Owned cylinder(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(),10),0,geometry::kPi*2,-10,10).Face());
    const auto circle=geometry::CurveSegment::MakeCircle({20,0,0},{1,0,0},{0,1,0},.005).Value();auto options=Options();options.toleranceMm=1e-5;
    const auto r=kernel::ProjectNativeWires({cylinder.h},{{{circle},{-1,0,0}}},options);
    Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==1&&r.Value().openEnds==0,"0.01mm lamp stays a complete loop");
    double length=0;for(const auto& c:r.Value().wires.front())length+=c.TotalLength(1e-7);
    Require(std::abs(length-2*geometry::kPi*.005)<1e-4,"small lamp perimeter remains complete");
    for(int i=0;i<128;++i){const auto source=circle.Evaluate(i/128.0);const geometry::Vector3 expected{std::sqrt(100-source.y*source.y),source.y,source.z};double distance=1e100;
        for(const auto& c:r.Value().wires.front())distance=std::min(distance,c.ClosestPoint(expected).distance);
        Require(distance<1.2e-5,"every angle of the small lamp is covered within geometric tolerance");}
    auto back=Plane();Owned near(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,2),gp_Dir(0,0,1)),0,30,-30,30).Face());
    const auto line=kernel::ProjectNativeWires({back.h,near.h},{{{Line({-.001,0,3},{.001,0,3})},{0,0,-1}}},Options());
    Require(line.HasValue(),line.FirstMessageJa());Require(line.Value().openEnds==4,"short open line is not treated as periodic or closed");
}
KACHA_V2_TEST(projection, NearbyDisconnectedSourcesAreNeverHealed) {
    auto face=Plane();const auto r=kernel::ProjectNativeWires({face.h},
        {{{Line({-2,0,3},{-.00002,0,3})},{0,0,-1}},{{Line({.00002,0,3},{2,0,3})},{0,0,-1}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==2&&r.Value().openEnds==4,"distinct nearby sources stay disconnected");
}
KACHA_V2_TEST(projection, RationalDegreeSevenSurface) {
    TColgp_Array2OfPnt points(1,8,1,8);TColStd_Array2OfReal weights(1,8,1,8);
    for(int i=1;i<=8;++i)for(int j=1;j<=8;++j){const double x=(i-4.5)*3,y=(j-4.5)*3;points.SetValue(i,j,gp_Pnt(x,y,2*std::exp(-(x*x+y*y)/60)));weights.SetValue(i,j,1+.02*(i+j));}
    TColStd_Array1OfReal knots(1,2);knots.SetValue(1,0);knots.SetValue(2,1);TColStd_Array1OfInteger mult(1,2);mult.SetValue(1,8);mult.SetValue(2,8);
    occ::handle<Geom_BSplineSurface> surface=new Geom_BSplineSurface(points,weights,knots,knots,mult,mult,7,7);
    Owned face(BRepBuilderAPI_MakeFace(surface,1e-7).Face());const auto original=kernel::CaptureOutputShape(face.h).Value();
    const auto r=kernel::ProjectNativeWires({face.h},{{ClosedSquare(10),{0,0,-1}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==1&&r.Value().openEnds==0,"closed outline on rational compound surface");
    OnSurface(r.Value(),.003,[&](auto p){GeomAPI_ProjectPointOnSurf projection(gp_Pnt(p.x,p.y,p.z),surface);return projection.NbPoints()>0?projection.LowerDistance():1e100;});
    Require(kernel::CaptureOutputShape(face.h).Value()==original,"rational input BRep unchanged");
}
KACHA_V2_TEST(projection, C0RationalFaceUnchangedInBothProjectionModes) {
    TColgp_Array2OfPnt points(1,15,1,8);TColStd_Array2OfReal weights(1,15,1,8);
    for(int i=1;i<=15;++i)for(int j=1;j<=8;++j){const double x=(i-8)*1.5,y=(j-4.5)*3;points.SetValue(i,j,gp_Pnt(x,y,2*std::exp(-(x*x+y*y)/60)));weights.SetValue(i,j,1+.01*(i+j));}
    TColStd_Array1OfReal uk(1,3),vk(1,2);uk.SetValue(1,0);uk.SetValue(2,.5);uk.SetValue(3,1);vk.SetValue(1,0);vk.SetValue(2,1);
    TColStd_Array1OfInteger um(1,3),vm(1,2);um.SetValue(1,8);um.SetValue(2,7);um.SetValue(3,8);vm.SetValue(1,8);vm.SetValue(2,8);
    occ::handle<Geom_BSplineSurface> surface=new Geom_BSplineSurface(points,weights,uk,vk,um,vm,7,7);
    Owned face(BRepBuilderAPI_MakeFace(surface,1e-7).Face());const auto original=kernel::CaptureOutputShape(face.h).Value();
    for(const auto mode:{kernel::WireProjectionMode::Direction,kernel::WireProjectionMode::Closest}) {
        const auto r=kernel::ProjectNativeWires({face.h},{{ClosedSquare(10),{0,0,-1}}},Options(mode));
        Require(r.HasValue(),r.FirstMessageJa());std::string ends;for(const auto& run:r.Value().wires){const auto a=run.front().StartPoint(),b=run.back().EndPoint();ends+=" ["+std::to_string(a.x)+","+std::to_string(a.y)+","+std::to_string(a.z)+" -> "+std::to_string(b.x)+","+std::to_string(b.y)+","+std::to_string(b.z)+"]";}
        Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==1&&r.Value().openEnds==0,"closed C0 rational projection mode "+std::to_string(static_cast<int>(mode))+ends);
        Require(geometry::SegmentsFormClosedLoop(r.Value().wires.front(),{}),"projection joins satisfy modeling precision, not just the pick radius");
        Require(r.Value().maxCurveDeviationMm<=Options().toleranceMm,"connection correction remains inside the reported whole-curve bound");
        Require(kernel::CaptureOutputShape(face.h).Value()==original,"native caller bytes protected in every mode");
    }
}
KACHA_V2_TEST(projection, NativeRegionKeepsFaceAndClosedBoundary) {
    auto face=Plane();const auto original=kernel::CaptureOutputShape(face.h).Value();
    const auto r=kernel::ProjectNativeRegions({face.h},{{ClosedSquare(10),{0,0,-1}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().faces.size()==1&&r.Value().boundary.wires.size()==1,"one native window face");
    OnSurface(r.Value().boundary,1e-6,[](auto p){return std::abs(p.z);});
    for(const auto& wire:r.Value().boundary.wires)Require((wire.front().StartPoint()-wire.back().EndPoint()).Length()<1e-6,"native closed loop");
    for(const auto& h:r.Value().faces)kernel::ReleaseShape(h);
    Require(kernel::CaptureOutputShape(face.h).Value()==original,"face region projection preserves original bytes");
}
KACHA_V2_TEST(projection, NativeRegionPreservesHole) {
    BRepBuilderAPI_MakeFace builder(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-10,10,-10,10);
    BRepBuilderAPI_MakePolygon hole;hole.Add(gp_Pnt(-1,-1,0));hole.Add(gp_Pnt(-1,1,0));hole.Add(gp_Pnt(1,1,0));hole.Add(gp_Pnt(1,-1,0));hole.Close();builder.Add(hole.Wire());Owned face(builder.Face());
    const auto r=kernel::ProjectNativeRegions({face.h},{{ClosedSquare(10),{0,0,-1}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().faces.size()==1&&r.Value().boundary.wires.size()==2,"window region preserves target hole");
    for(const auto& h:r.Value().faces)kernel::ReleaseShape(h);
}
KACHA_V2_TEST(projection, NativeRegionOnCurvedFaceAndMultipleTargets) {
    Owned cylinder(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(),10),-.8,.8,-10,10).Face());
    const std::vector<geometry::CurveSegment> window={Line({20,-2,-2},{20,2,-2}),Line({20,2,-2},{20,2,2}),Line({20,2,2},{20,-2,2}),Line({20,-2,2},{20,-2,-2})};
    const auto original=kernel::CaptureOutputShape(cylinder.h).Value();
    const auto r=kernel::ProjectNativeRegions({cylinder.h},{{window,{-1,0,0}}},Options());
    Require(r.HasValue(),r.FirstMessageJa());OnSurface(r.Value().boundary,.003,[](auto p){return std::abs(std::hypot(p.x,p.y)-10);});
    for(const auto& h:r.Value().faces)kernel::ReleaseShape(h);
    Require(kernel::CaptureOutputShape(cylinder.h).Value()==original,"curved target unchanged");
    auto front=Plane(3),back=Plane(0);const auto both=kernel::ProjectNativeRegions({front.h,back.h},{{ClosedSquare(10),{0,0,-1}}},Options());
    Require(both.HasValue()&&both.Value().faces.size()==2,"only explicitly selected faces receive region");
    for(const auto& h:both.Value().faces)kernel::ReleaseShape(h);
}
KACHA_V2_TEST(projection, NativeRegionRefusesOpenParallelAndNonplanar) {
    auto face=Plane();auto curves=ClosedSquare(10);curves.pop_back();
    Require(!kernel::ProjectNativeRegions({face.h},{{curves,{0,0,-1}}},Options()).HasValue(),"open contour refusal");
    Require(!kernel::ProjectNativeRegions({face.h},{{ClosedSquare(10),{1,0,0}}},Options()).HasValue(),"parallel direction refusal");
    curves={Line({0,0,10},{2,0,10}),Line({2,0,10},{2,2,11}),Line({2,2,11},{0,2,10}),Line({0,2,10},{0,0,10})};
    Require(!kernel::ProjectNativeRegions({face.h},{{curves,{0,0,-1}}},Options()).HasValue(),"nonplanar source refusal");
}
#endif
KACHA_V2_TEST_MAIN("kernel_wire_projection_tests")

KACHA_V2_TEST(projection, WrappedSourceIntervalsDoNotExposeFittingSpans) {
    Owned cylinder(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(),10),-2,2,-10,10).Face());
    auto o=Options(kernel::WireProjectionMode::Wrap);o.targetPoint={10,0,0};
    const auto s=geometry::CurveSegment::MakeCubicBezier({{-6,-2,0},{-3,5,0},{3,-5,0},{6,2,0}}).Value();
    for(const auto& source:std::vector<geometry::CurveSegment>{Line({-6,0,0},{6,0,0}),s}){
        const auto r=kernel::ProjectNativeWires({cylinder.h},{{{source}, {}}},o);
        Require(r.HasValue(),r.FirstMessageJa());Require(r.Value().wires.size()==1,"one source interval");
        Require(r.Value().wires.front().size()==1,"internal fitting spans become one curved interval");
        OnSurface(r.Value(),o.toleranceMm,[](auto p){return std::abs(std::hypot(p.x,p.y)-10);});
        for(int i=0;i<=200;++i){const auto p=source.Evaluate(i/200.0);
            const geometry::Vector3 expected{10*std::cos(p.x/10),10*std::sin(p.x/10),p.y};
            Require(r.Value().wires.front().front().ClosestPoint(expected).distance<o.toleranceMm,"wrapped curve stays within tolerance");}
        double length=0;for(const auto& c:r.Value().wires.front())length+=c.TotalLength(1e-7);
        Require(std::abs(length-source.TotalLength(1e-7))<.003,"physical length preserved");
    }
}
