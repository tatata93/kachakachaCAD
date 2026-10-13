#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctWireProjection.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctOutput.h"
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
#endif
KACHA_V2_TEST_MAIN("kernel_wire_projection_tests")
