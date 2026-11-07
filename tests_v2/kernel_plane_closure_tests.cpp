#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctSurfaceExtrude.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <BRepLib.hxx>
#include <Geom_BezierSurface.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom2d_Line.hxx>
#include <GeomTools.hxx>
#include <NCollection_Array2.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <array>
#include <sstream>
#include <iomanip>
#endif
#include <cmath>
#include <algorithm>
using namespace kachakacha::v2;
using test::Require;
using test::RequireNear;
#ifdef KACHACAD_V2_WITH_OCCT
namespace {
TopoDS_Wire UvHole(const occ::handle<Geom_Surface>& surface,double u0,double v0,double u1,double v1) {
    const std::array<gp_Pnt2d,4> points{gp_Pnt2d(u0,v0),gp_Pnt2d(u0,v1),gp_Pnt2d(u1,v1),gp_Pnt2d(u1,v0)};
    BRepBuilderAPI_MakeWire wire;
    for(std::size_t i=0;i<4;++i) {
        const auto& a=points[i];const auto& b=points[(i+1)%4];
        const occ::handle<Geom2d_Curve> line=new Geom2d_Line(a,gp_Dir2d(b.X()-a.X(),b.Y()-a.Y()));
        wire.Add(BRepBuilderAPI_MakeEdge(line,surface,0,a.Distance(b)).Edge());
    }
    Require(wire.IsDone(),"hole boundary connects");return wire.Wire();
}
TopoDS_Face ParabolicTop(bool holes=false,double slope=0,double height=4) {
    // x=10u, y=8v, z=slope*x+2*height*u*(1-u).
    NCollection_Array2<gp_Pnt> poles(1,3,1,2);
    for(int i=1;i<=3;++i)for(int j=1;j<=2;++j)
        poles.SetValue(i,j,gp_Pnt((i-1)*5,(j-1)*8,slope*(i-1)*5+(i==2?height:0)));
    const occ::handle<Geom_Surface> surface=new Geom_BezierSurface(poles);
    BRepBuilderAPI_MakeFace face(surface,0,1,0,1,1e-7);
    if(holes){face.Add(UvHole(surface,.2,.2,.4,.4));face.Add(UvHole(surface,.6,.6,.8,.8));}
    auto made=face.Face();Require(BRepLib::BuildCurves3d(made),"source 3D boundaries exist");return made;
}
modeling::ExtrudeRequest ToPlane(const geometry::Vector3& normal={0,0,1}) {
    modeling::ExtrudeRequest request;request.outputs.part=true;request.extent=modeling::ExtrudeExtentMode::ToTarget;
    request.customDirection={0,0,-1};request.targetKind=modeling::ExtrudeTargetKind::Plane;
    request.targetPlane.origin={0,0,0};request.targetPlane.normal=normal;return request;
}
void CheckClosed(const base::Result<kernel::ExtrudeBuildResult>& made,double expected) {
    Require(made.HasValue(),made.FirstSummaryJa()+" "+made.FirstDetailsJa());
    Require(made.Value().parts.size()==1,"one connected solid");
    const auto checked=kernel::CheckSolidForExport(made.Value().parts.front().handle,1e-6);
    Require(checked.HasValue()&&checked.Value().Ok(),"independent closed/positive/self-intersection export check");
    RequireNear(checked.Value().volumeMm3,expected,1e-6,"analytic volume under parabola, excluding holes");
    Require(std::count(made.Value().parts.front().faceKeys.begin(),made.Value().parts.front().faceKeys.end(),"start")==1,"original curved cap remains a start face");
    Require(std::count(made.Value().parts.front().faceKeys.begin(),made.Value().parts.front().faceKeys.end(),"end")==1,"projected plane cap remains an end face");
}
double HoleVolume(double a,double b,double width) {
    const auto primitive=[](double u){return u*u*.5-u*u*u/3;};return 80*8*(primitive(b)-primitive(a))*width;
}
std::string SurfaceGeometry(const TopoDS_Face& face) {
    std::ostringstream encoded;GeomTools::Write(BRep_Tool::Surface(face),encoded);return encoded.str();
}
struct BoundaryCurve {std::string definition;double first,last;gp_Pnt begin,end;};
std::vector<BoundaryCurve> BoundaryGeometry(const TopoDS_Face& face) {
    std::vector<BoundaryCurve> curves;
    for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()) {
        double first,last;TopLoc_Location location;const auto curve=BRep_Tool::Curve(TopoDS::Edge(e.Current()),location,first,last);
        Require(!curve.IsNull(),"original cap boundary remains a 3D curve");
        std::ostringstream encoded;encoded<<std::setprecision(17);GeomTools::Write(curve,encoded);
        for(int i=1;i<=3;++i)for(int j=1;j<=4;++j)encoded<<" "<<location.Transformation().Value(i,j);
        curves.push_back({encoded.str(),first,last,curve->Value(first).Transformed(location.Transformation()),curve->Value(last).Transformed(location.Transformation())});
    }
    std::sort(curves.begin(),curves.end(),[](const auto& a,const auto& b){return a.definition==b.definition?a.first<b.first:a.definition<b.definition;});return curves;
}
void CheckOriginalCap(const TopoDS_Face& original,const kernel::ExtrudeBuildResult& made) {
    TopoDS_Shape result;Require(kernel::LookupShape(made.parts.front().handle,result),"result native exists");
    std::size_t index=0;bool checked=false;
    for(TopExp_Explorer e(result,TopAbs_FACE);e.More();e.Next(),++index) {
        if(made.parts.front().faceKeys[index]!="start")continue;
        const auto face=TopoDS::Face(e.Current());
        Require(SurfaceGeometry(face)==SurfaceGeometry(original),"source support geometry is exactly retained");
        const auto before=BoundaryGeometry(original),after=BoundaryGeometry(face);
        Require(before.size()==after.size(),"no source boundary edge dropped or split");
        for(std::size_t i=0;i<before.size();++i) {
            Require(before[i].definition==after[i].definition,"every complete 3D curve definition retained exactly");
            RequireNear(after[i].first,before[i].first,1e-12,"first parameter differs only by numerical roundoff");
            RequireNear(after[i].last,before[i].last,1e-12,"last parameter differs only by numerical roundoff");
            Require(before[i].begin.Distance(after[i].begin)<=1e-12&&before[i].end.Distance(after[i].end)<=1e-12,
                "roundoff also stays within numerical epsilon in physical millimetres");
        }checked=true;
    }Require(checked,"original curved top can be identified after closure");
}
}
#endif

KACHA_V2_TEST(kernel_plane_closure, curved_boundary_can_already_touch_stop_plane) {
#ifdef KACHACAD_V2_WITH_OCCT
    const auto original=ParabolicTop();const auto source=kernel::StoreShape(original);const auto before=kernel::CaptureOutputShape(source);
    const auto made=kernel::BuildExtrudeToFace(ToPlane(),source,{},{});CheckClosed(made,80*8/6.);
    CheckOriginalCap(original,made.Value());
    Require(kernel::CaptureOutputShape(source).Value()==before.Value(),"source geometry/topology bytes unchanged");kernel::ClearShapeCache();
#endif
}
KACHA_V2_TEST(kernel_plane_closure, two_curved_holes_survive_directional_closure) {
#ifdef KACHACAD_V2_WITH_OCCT
    const auto source=kernel::StoreShape(ParabolicTop(true));const auto made=kernel::BuildExtrudeToFace(ToPlane(),source,{},{});
    CheckClosed(made,80*8/6.-HoleVolume(.2,.4,.2)-HoleVolume(.6,.8,.2));
    Require(made.Value().startProfileWires.size()==3&&made.Value().endProfileWires.size()==3,"both caps retain two independent holes");kernel::ClearShapeCache();
#endif
}
KACHA_V2_TEST(kernel_plane_closure, inclined_stop_and_oblique_direction_preserve_curved_top) {
#ifdef KACHACAD_V2_WITH_OCCT
    for(bool incline:{false,true}) {
        auto request=ToPlane(incline?geometry::Vector3{-.2,0,1}:geometry::Vector3{0,0,1});
        if(!incline)request.customDirection={.25,0,-1};
        const auto source=kernel::StoreShape(ParabolicTop(false,incline?.2:0));
        CheckClosed(kernel::BuildExtrudeToFace(request,source,{},{}),80*8/6.);kernel::ClearShapeCache();
    }
#endif
}
KACHA_V2_TEST(kernel_plane_closure, cylindrical_arch_closes_at_its_touching_straight_edges) {
#ifdef KACHACAD_V2_WITH_OCCT
    const occ::handle<Geom_Surface> cylinder=new Geom_CylindricalSurface(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(0,-1,0),gp_Dir(1,0,0)),10);
    const auto face=BRepBuilderAPI_MakeFace(cylinder,0,geometry::kPi,0,8,1e-7).Face();
    const auto made=kernel::BuildExtrudeToFace(ToPlane(),kernel::StoreShape(face),{},{});
    CheckClosed(made,400*geometry::kPi);CheckOriginalCap(face,made.Value());kernel::ClearShapeCache();
#endif
}
KACHA_V2_TEST(kernel_plane_closure, moved_rotated_and_reversed_source_retains_volume) {
#ifdef KACHACAD_V2_WITH_OCCT
    gp_Trsf transform;transform.SetValues(0,0,1,10000,1,0,0,-20000,0,1,0,30000);
    auto face=ParabolicTop();face.Reverse();const auto moved=BRepBuilderAPI_Transform(face,transform,true).Shape();
    auto request=ToPlane({1,0,0});request.targetPlane.origin={10000,-20000,30000};request.customDirection={-1,0,0};
    CheckClosed(kernel::BuildExtrudeToFace(request,kernel::StoreShape(moved),{},{}),80*8/6.);kernel::ClearShapeCache();
#endif
}
KACHA_V2_TEST(kernel_plane_closure, missing_finite_stop_area_is_not_filled_between_samples) {
#ifdef KACHACAD_V2_WITH_OCCT
    BRepBuilderAPI_MakeFace stop(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-1,11,-1,9);
    BRepBuilderAPI_MakePolygon hole;for(const auto p:{gp_Pnt(7.11,3.21,0),gp_Pnt(7.11,3.23,0),gp_Pnt(7.13,3.23,0),gp_Pnt(7.13,3.21,0)})hole.Add(p);
    hole.Close();stop.Add(hole.Wire());const auto source=kernel::StoreShape(ParabolicTop());const auto before=kernel::CaptureOutputShape(source);
    const auto target=kernel::StoreShape(stop.Shape());const auto targetBefore=kernel::CaptureOutputShape(target);
    const auto made=kernel::BuildExtrudeToFace(ToPlane(),source,target,{});
    Require(!made.HasValue(),"finite stop's tiny missing patch cannot be silently filled");
    Require(kernel::CaptureOutputShape(source).Value()==before.Value(),"rejected operation preserves source");
    Require(kernel::CaptureOutputShape(target).Value()==targetBefore.Value(),"finite stop and its hole remain byte-for-byte unchanged");kernel::ClearShapeCache();
#endif
}
KACHA_V2_TEST(kernel_plane_closure, crossing_wrong_way_parallel_and_coplanar_sources_are_rejected) {
#ifdef KACHACAD_V2_WITH_OCCT
    const auto source=kernel::StoreShape(ParabolicTop());
    for(int mode=0;mode<4;++mode) {
        auto request=ToPlane();if(mode==0)request.targetPlane.origin.z=1;
        if(mode==1)request.customDirection={0,0,1};if(mode==2)request.customDirection={1,0,0};
        const auto input=mode==3?kernel::StoreShape(ParabolicTop(false,0,0)):source;
        Require(!kernel::BuildExtrudeToFace(request,input,{},{}).HasValue(),"crossing/behind/parallel/zero extent is refused");
    }kernel::ClearShapeCache();
#endif
}
KACHA_V2_TEST(kernel_plane_closure, actual_dense_sources_close_to_plane_without_replacing_their_curves) {
#ifdef KACHACAD_V2_WITH_OCCT
    for(const char* side:{"left","right"}) {
        const auto path=std::filesystem::path(KACHACAD_TEST_FIXTURE_DIR)/(std::string("dense-curved-overlap-")+side+".kshape");
        std::ifstream file(path,std::ios::binary);Require(file.good(),"actual curved fixture exists");
        const std::string bytes(std::istreambuf_iterator<char>(file),{});const auto restored=kernel::RestoreOutputShape(bytes);Require(restored.HasValue(),restored.FirstSummaryJa());
        TopoDS_Shape native;Require(kernel::LookupShape(restored.Value(),native),"source native exists");
        TopoDS_Face top;double highest=-1e100;
        for(TopExp_Explorer f(native,TopAbs_FACE);f.More();f.Next()) {
            BRepAdaptor_Surface surface(TopoDS::Face(f.Current()));if(surface.GetType()!=GeomAbs_BSplineSurface)continue;
            const auto spline=surface.BSpline();double z=-1e100;for(int i=1;i<=spline->NbUPoles();++i)for(int j=1;j<=spline->NbVPoles();++j)z=std::max(z,spline->Pole(i,j).Z());
            if(z>highest){highest=z;top=TopoDS::Face(f.Current());}
        }
        Require(!top.IsNull(),"complete original outer spline retained");top.Orientation(TopAbs_FORWARD);
        const auto source=kernel::StoreShape(top);const auto before=kernel::CaptureOutputShape(source);auto request=ToPlane();request.targetPlane.origin.z=27;
        const auto made=kernel::BuildExtrudeToFace(request,source,{},{});Require(made.HasValue(),made.FirstSummaryJa()+" "+made.FirstDetailsJa());
        Require(made.Value().parts.size()==1,"one closed dense cap");const auto quality=kernel::CheckSolidForExport(made.Value().parts.front().handle,1e-6);
        Require(quality.HasValue()&&quality.Value().Ok(),"dense cap passes unchanged native safety checks");
        CheckOriginalCap(top,made.Value());
        Require(quality.Value().volumeMm3>1999&&quality.Value().volumeMm3<2002,"native bulk volume stays in independent geometric interval");
        Require(kernel::CaptureOutputShape(source).Value()==before.Value(),"complete input curve/surface bytes unchanged");kernel::ClearShapeCache();
    }
#endif
}

KACHA_V2_TEST_MAIN("kernel_plane_closure_tests")
