#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctThicken.h"
#include "kachakacha/kernel/OcctCorrectedThicken.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/kernel/OcctMassProperties.h"
#include "kachakacha/kernel/OcctImage.h"
#include "kachakacha/kernel/OcctIntersectionPartition.h"
#include "kachakacha/kernel/OcctBoundaryCurves.h"
#include "kachakacha/kernel/OcctSpanThicken.h"
#include "kachakacha/kernel/OcctPlanarSplineThicken.h"
#include "kachakacha/kernel/OcctBilinearSupport.h"
#include <cmath>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <array>
#include <fstream>
#include <filesystem>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <Geom_Surface.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Lin.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <gp_Pln.hxx>
#include <gp_Circ.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <GeomConvert.hxx>
#include <Geom_BSplineSurface.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepLib.hxx>
#include <GProp_GProps.hxx>
#include <Geom2d_Line.hxx>
#include <gp_Dir2d.hxx>
#include <gp_Pnt2d.hxx>
#include <Geom_BezierSurface.hxx>
#include <NCollection_Array2.hxx>
#include <STEPControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <sstream>
#include <BRepPrimAPI_MakePrism.hxx>
#endif
using namespace kachakacha::v2;
using test::Require;

#ifdef KACHACAD_V2_WITH_OCCT
namespace {
modeling::KernelShapeHandle Revolved(const std::vector<geometry::Vector3>& points)
{
    modeling::GuideChain chain;chain.role=modeling::ChainRole::Section;
    for(std::size_t i=1;i<points.size();++i) {
        const auto a=points[i-1],b=points[i];
        chain.segments.push_back(geometry::CurveSegment::MakeCubicBezier({a,a+(b-a)*(1.0/3),b-(b-a)*(1.0/3),b}).Value());
    }
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::Revolve;
    request.chains={chain};request.revolveAxisDirection={0,0,1};request.revolveAngleRad=std::acos(-1.0)/2;
    const auto analyzed=modeling::AnalyzeGuideSurfaceRequest(request,{});
    Require(analyzed.HasValue(),"valid revolved section");
    const auto made=kernel::BuildGuideSurface(request,analyzed.Value(),{});
    Require(made.HasValue(),"revolved surface built");return made.Value().handle;
}
}
KACHA_V2_TEST(thicken_quality, dense_offset_integrates_native_spans_and_preserves_signed_volume_and_centroid)
{
    const double pi=std::acos(-1.0),radius=12,height=30,thickness=.5;
    const occ::handle<Geom_Surface> cylinder=new Geom_CylindricalSurface(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(0,0,1)),radius);
    const occ::handle<Geom_Surface> trimmed=new Geom_RectangularTrimmedSurface(cylinder,0,pi*.5,0,height);
    auto surface=GeomConvert::SurfaceToBSplineSurface(trimmed);
    for(int i=1;i<80;++i)surface->InsertUKnot(pi*.5*i/80,1,1e-12,false);
    for(int i=1;i<12;++i)surface->InsertVKnot(height*i/12,1,1e-12,false);
    BRepOffsetAPI_MakeThickSolid offset;offset.MakeThickSolidBySimple(BRepBuilderAPI_MakeFace(surface,1e-7).Face(),thickness);
    Require(offset.IsDone(),"dense spline offset constructed");
    auto solid=TopoDS::Solid(offset.Shape());Require(BRepLib::OrientClosedSolid(solid),"dense wall oriented");
    const auto handle=kernel::StoreShape(solid);
    const auto before=kernel::CaptureOutputShape(handle);Require(before.HasValue(),"dense offset captures before display");
    Require(kernel::BuildShapeMesh(handle,.01).HasValue(),"dense offset displays");
    const auto display=kernel::BuildShapeMesh(handle);Require(display.HasValue(),"parent display built for decal");
    const auto imageFace=kernel::ImageFace(handle,0);Require(imageFace.HasValue(),"independent image face captured");
    const auto faceBrep=kernel::CaptureOutputShape(imageFace.Value());Require(faceBrep.HasValue(),"image face serializes with tessellation");
    domain::CreateImageDefinition image;image.faceBrep=faceBrep.Value();image.pixelWidth=32;image.pixelHeight=32;
    const auto count=kernel::CachedShapeCount();
    std::vector<modeling::MeshTriangle> expectedFace;
    for(const auto& triangle:display.Value().triangles)if(triangle.faceIndex==0)expectedFace.push_back(triangle);
    for(int repeat=0;repeat<3;++repeat) {
        const auto triangles=kernel::ImageSurfaceTriangles(image);Require(triangles.HasValue(),"image face renders repeatedly");
        Require(triangles.Value().size()==expectedFace.size(),"decal reuses parent display tessellation");
        for(std::size_t i=0;i<expectedFace.size();++i)for(const auto& p:triangles.Value()[i].mesh.points) {
            double nearest=1e99;for(const auto& q:expectedFace[i].points)nearest=std::min(nearest,geometry::Distance(p,q));
            Require(nearest<1e-8,"decal vertices match parent without flicker");
        }
        Require(kernel::CachedShapeCount()==count,"image preview releases transient native shapes");
    }
    kernel::ReleaseShape(imageFace.Value());
    const auto after=kernel::CaptureOutputShape(handle);
    Require(after.HasValue()&&before.Value()==after.Value(),"display must not mutate authoritative topology or triangulation");
    const auto restored=kernel::RestoreOutputShape(after.Value());Require(restored.HasValue(),"displayed shape roundtrips");
    kernel::ReleaseShape(restored.Value());kernel::ReleaseShape(handle);
    const double expected=pi*.25*((radius+thickness)*(radius+thickness)-radius*radius)*height;
    const double center=4/(3*pi)*(std::pow(radius+thickness,3)-std::pow(radius,3))/((radius+thickness)*(radius+thickness)-radius*radius);
    gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,1,0)),.7);
    transform.SetTranslationPart(gp_Vec(1000,-2000,3000));
    auto placed=BRepBuilderAPI_Transform(solid,transform,false).Shape();
    for(bool reverse:{false,true}) {
        GProp_GProps properties;kernel::detail::VolumeProperties(placed,properties,true);
        test::RequireNear(properties.Mass(),reverse?-expected:expected,expected*1e-8,"dense offset signed analytic volume");
        auto wanted=gp_Pnt(center,center,height*.5);wanted.Transform(transform);
        test::RequireNear(properties.CentreOfMass().Distance(wanted),0,1e-7,"rotated translated offset analytic centroid");
        placed.Reverse();
    }
}
KACHA_V2_TEST(thicken_quality, cylinder_wall_has_outward_winding_and_analytic_volume)
{
    const auto surface=Revolved({{16,0,0},{16,0,70}});
    const auto solid=kernel::ThickenSurface(surface,.8,fabrication::ThicknessPlacement::Inside,{});
    Require(solid.HasValue(),"cylindrical panel thickens");
    const auto mesh=kernel::BuildShapeMesh(solid.Value().handle,.002);
    Require(mesh.HasValue(),"mesh built");double volume=0;
    for(const auto& t:mesh.Value().triangles)
        volume+=geometry::Dot(t.points[0],geometry::Cross(t.points[1],t.points[2]))/6;
    const double expected=std::acos(-1.0)/4*(16*16-15.2*15.2)*70;
    Require(volume>0,"signed STL-like volume must be positive, not abs(volume)");
    test::RequireNear(volume,expected,expected*.001,"analytic cylindrical shell volume");
    const auto step=kernel::OutputStep({solid.Value().handle});
    Require(step.HasValue(),"STEP writer verifies solid count, signed volume and bounds after rereading");
}

KACHA_V2_TEST(thicken_quality, radius_five_outer_skin_and_one_mm_wall_have_radius_four_inner_skin)
{
    const double pi=std::acos(-1.0);
    const occ::handle<Geom_Surface> cylinder=new Geom_CylindricalSurface(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(0,0,1)),5);
    const auto face=BRepBuilderAPI_MakeFace(cylinder,0,pi*.5,0,10,1e-7).Face();
    const auto source=kernel::StoreShape(face);const auto original=kernel::CaptureOutputShape(source);
    const auto wall=kernel::ThickenSurface(source,1,fabrication::ThicknessPlacement::Inside,{});
    Require(wall.HasValue(),"one mm wall must retain the radius-five outside skin");
    TopoDS_Shape solid;Require(kernel::LookupShape(wall.Value().handle,solid),"native solid retained");
    IntCurvesFace_ShapeIntersector ray;ray.Load(solid,1e-7);
    ray.Perform(gp_Lin(gp_Pnt(0,0,5),gp_Dir(1,1,0)),0,6);
    Require(ray.IsDone()&&ray.NbPnt()==2,"radial ray meets precisely the inner and outer skin");
    std::vector<double> radii;for(int i=1;i<=ray.NbPnt();++i)radii.push_back(ray.WParameter(i));std::sort(radii.begin(),radii.end());
    test::RequireNear(radii[0],4,1e-7,"inner radius is four mm");
    test::RequireNear(radii[1],5,1e-7,"outer radius remains five mm");
    test::RequireNear(wall.Value().volumeMm3,pi*.25*(25-16)*10,1e-6,"analytic material volume");
    const auto after=kernel::CaptureOutputShape(source);
    Require(original.HasValue()&&after.HasValue()&&original.Value()==after.Value(),"thickness does not alter the source face");
    kernel::ReleaseShape(source);kernel::ReleaseShape(wall.Value().handle);
}
KACHA_V2_TEST(thicken_quality, rim_with_impossible_offset_is_rejected)
{
    const auto surface=Revolved({{15.9,0,1},{16.5,0,1.6},{16.5,0,3},{16.2,0,3.3},{16.2,0,63}});
    const auto before=kernel::CachedShapeCount();
    const auto solid=kernel::ThickenSurface(surface,.8,fabrication::ThicknessPlacement::Inside,{});
    Require(!solid.HasValue(),"self-intersecting simple offset must not become a part");
    Require(!solid.Diagnostics().empty()&&!solid.Diagnostics().front().detailsJa.empty(),"failure retains an actionable diagnostic");
    Require(kernel::CachedShapeCount()==before,"rejected thickness leaves no orphaned shape");
}
KACHA_V2_TEST(thicken_quality, focal_fold_rejection_respects_placement_and_reversed_normal)
{
    const double pi=std::acos(-1.0),radius=2,height=5;
    const occ::handle<Geom_Surface> cylinder=new Geom_CylindricalSurface(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(0,0,1)),radius);
    auto face=BRepBuilderAPI_MakeFace(cylinder,0,pi*.5,0,height,1e-7).Face();
    for(bool reversed:{false,true}) {
        const auto source=kernel::StoreShape(face);const auto before=kernel::CaptureOutputShape(source);
        Require(before.HasValue(),"cylinder source captures");
        for(auto placement:{fabrication::ThicknessPlacement::Outside,fabrication::ThicknessPlacement::Inside,fabrication::ThicknessPlacement::Centered}) {
            const double thickness=placement==fabrication::ThicknessPlacement::Centered?6:3;
            const auto count=kernel::CachedShapeCount();const auto made=kernel::ThickenSurface(source,thickness,placement,{});
            const bool outward=placement==(reversed?fabrication::ThicknessPlacement::Inside:fabrication::ThicknessPlacement::Outside);
            Require(made.HasValue()==outward,"only a thickness crossing the actual oriented focal radius is rejected");
            if(made.HasValue()) {
                test::RequireNear(made.Value().volumeMm3,pi*.25*(25-4)*height,1e-7,"outward wall analytic volume");
                kernel::ReleaseShape(made.Value().handle);
            } else Require(made.FirstSummaryJa()=="この厚みでは曲面の内側が折り返します。","focal fold has specific actionable reason");
            Require(kernel::CachedShapeCount()==count,"placement check releases all temporary results");
        }
        const auto after=kernel::CaptureOutputShape(source);
        Require(after.HasValue()&&before.Value()==after.Value(),"placement checks preserve native source");
        kernel::ReleaseShape(source);face.Reverse();
    }
}
KACHA_V2_TEST(thicken_quality, planar_to_steep_plane_has_full_wedge_and_safe_roundtrip)
{
    auto face=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,10),gp_Dir(0,0,1)),-1,1,-1,1).Face();
    const double angle=75*std::acos(-1.0)/180;
    for(int reverse=0;reverse<2;++reverse) {
        if(reverse)face.Reverse();
        const auto source=kernel::StoreShape(face);const auto before=kernel::CaptureOutputShape(source);
        const auto made=kernel::ThickenSurfaceToPlane(source,{0,0,0},{0,-std::sin(angle),std::cos(angle)},{});
        if(!made.HasValue())std::cout<<made.FirstSummaryJa()<<' '<<made.FirstDetailsJa()<<'\n';
        Require(made.HasValue(),"steep target receives the entire planar source, including reversed input");
        test::RequireNear(made.Value().volumeMm3,40,1e-6,"independent wedge volume: area 4 times mean height 10");
        const auto checked=kernel::CheckSolidForExport(made.Value().handle,1e-6);
        Require(checked.HasValue()&&checked.Value().Ok(),"full manifold and self-intersection check");
        Require(kernel::OutputStep({made.Value().handle}).HasValue(),"wedge STEP readback");
        Require(kernel::CaptureOutputShape(source).Value()==before.Value(),"source BRep preserved");
        kernel::ReleaseShape(made.Value().handle);kernel::ReleaseShape(source);
    }
}
KACHA_V2_TEST(thicken_quality, target_crossing_circle_boundary_is_rejected_without_leaks)
{
    const auto edge=BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0,0,1),gp_Dir(0,0,1)),3)).Edge();
    const auto face=BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(edge).Wire(),true).Face();
    const auto source=kernel::StoreShape(face);const auto count=kernel::CachedShapeCount();
    const auto made=kernel::ThickenSurfaceToPlane(source,{0,0,0},{1,0,1},{});
    Require(!made.HasValue()&&made.FirstDiagnostic().code==kernel::kThickenBadTarget,
        "a periodic boundary crosses the plane between its topological vertices");
    Require(kernel::CachedShapeCount()==count,"rejected source leaves no result");kernel::ReleaseShape(source);
}
KACHA_V2_TEST(thicken_quality, curved_to_plane_refuses_unreachable_normal_extrusion)
{
    const auto source=Revolved({{16,0,0},{16,0,70}});const auto count=kernel::CachedShapeCount();
    const auto made=kernel::ThickenSurfaceToPlane(source,{0,0,-5},{0,0,1},{});
    Require(!made.HasValue()&&!made.FirstDetailsJa().empty(),"radial offset cannot fill down to a horizontal plane");
    Require(kernel::CachedShapeCount()==count,"no false successful tube");kernel::ReleaseShape(source);
}
KACHA_V2_TEST(thicken_quality, folded_band_has_exact_miter_thickness_and_portable_step)
{
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::RuledSections;
    request.keepSectionOrder=true;
    for(int side=0;side<2;++side) {
        modeling::GuideChain chain;chain.role=modeling::ChainRole::Section;chain.index=side+1;
        const double x=side*10.0;
        chain.segments={geometry::CurveSegment::MakeLine({x,0,0},{x,0,10}).Value(),
            geometry::CurveSegment::MakeLine({x,0,10},{x,5,15}).Value()};
        request.chains.push_back(chain);
    }
    const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,{});
    Require(analysis.HasValue(),"folded band input");
    const auto face=kernel::BuildGuideSurface(request,analysis.Value(),{});
    Require(face.HasValue(),"folded band surface");
    const auto solid=kernel::ThickenSurface(face.Value().handle,.8,fabrication::ThicknessPlacement::Inside,{});
    Require(solid.HasValue(),"C0 crease thickens with a miter, not a smooth offset");
    const double expected=10*((10+std::sqrt(50.0))*.8+.8*.8*std::tan(std::acos(-1.0)/8));
    test::RequireNear(solid.Value().volumeMm3,expected,1e-6,"independent two-plane miter volume");
    Require(kernel::OutputStep({solid.Value().handle}).HasValue(),"STEP roundtrip preserves the folded closed solid");
    kernel::ReleaseShape(solid.Value().handle);kernel::ReleaseShape(face.Value().handle);
}
KACHA_V2_TEST(thicken_quality, smooth_vase_inward_wall_preserves_exact_outer_skin)
{
    const std::vector<geometry::Vector3> points{
#include "fixtures/vase_profile_points.inc"
    };
    modeling::GuideChain chain;chain.role=modeling::ChainRole::Section;
    chain.segments={geometry::CurveSegment::MakeCubicBSpline(points).Value()};
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::Revolve;
    request.chains={chain};request.revolveAxisDirection={0,0,1};request.revolveAngleRad=2*std::acos(-1.0);
    const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,{});
    Require(analysis.HasValue(),"Smooth photo profile request");
    const auto surface=kernel::BuildGuideSurface(request,analysis.Value(),{});
    Require(surface.HasValue(),"Full revolution source");
    const auto solid=kernel::ThickenSurface(surface.Value().handle,.8,fabrication::ThicknessPlacement::Inside,{});
    if(!solid.HasValue())for(const auto& diagnostic:solid.Diagnostics())
        std::cout<<diagnostic.summaryJa<<" / "<<diagnostic.detailsJa<<'\n';
    Require(solid.HasValue(),"Inward offset of smooth vase is a valid closed outward solid");
    // Independent SciPy per-knot integration of pi*r^2 dz on the source
    // spline and its exact .8mm normal offset (tools/integrate_vase.py).
    test::RequireNear(solid.Value().volumeMm3,24083.255651747582,.0003,
        "Spline revolution volume agrees with independent meridian integral");
    TopoDS_Shape outer,wall;
    Require(kernel::LookupShape(surface.Value().handle,outer)&&kernel::LookupShape(solid.Value().handle,wall),"Vase shapes available");
    const auto face=TopoDS::Face(TopExp_Explorer(outer,TopAbs_FACE).Current());
    const auto skin=BRep_Tool::Surface(face);double u0,u1,v0,v1;
    BRepTools::UVBounds(face,u0,u1,v0,v1);
    double maximumWallError=0;
    for(int i=0;i<96;++i) {
        gp_Pnt point;gp_Vec du,dv;
        skin->D1(u0+(u1-u0)*(.07+.86*(i%17)/16),v0+(v1-v0)*(i+.5)/96,point,du,dv);
        auto n=du.Crossed(dv);if(face.Orientation()==TopAbs_REVERSED)n.Reverse();n.Normalize();
        IntCurvesFace_ShapeIntersector ray;ray.Load(wall,1e-7);
        ray.Perform(gp_Lin(point,gp_Dir(-n)),-1e-4,1.6);
        Require(ray.IsDone(),"Normal ray intersects the actual B-Rep wall");
        double first=1e9,inside=1e9;
        for(int j=1;j<=ray.NbPnt();++j) {
            first=std::min(first,std::abs(ray.WParameter(j)));
            if(ray.WParameter(j)>.01)inside=std::min(inside,ray.WParameter(j));
        }
        Require(first<1e-7,"Original outer skin retained on all sampled normals");
        maximumWallError=std::max(maximumWallError,std::abs(inside-.8));
    }
    std::cout<<"Vase normal-ray wall error max="<<maximumWallError<<" mm / 96 samples\n";
    Require(maximumWallError<2e-6,"Vase .8mm thickness measured on actual surfaces");
    const auto step=kernel::OutputStep({solid.Value().handle});
    if(!step.HasValue())for(const auto& diagnostic:step.Diagnostics())
        std::cout<<diagnostic.summaryJa<<" / "<<diagnostic.detailsJa<<'\n';
    Require(step.HasValue(),"Vase STEP rereads with identical volume and bounds");
    // The real sample's separate base intersects the spline wall. Its joined
    // STEP used to retain a valid B-Rep but fail accurate read-back integration.
    BRepPrimAPI_MakeCylinder base(29.28301886792453,1.2);
    BRepAlgoAPI_Fuse joined(wall,base.Shape());joined.Build();joined.SimplifyResult(true,true);
    Require(joined.IsDone()&&BRepCheck_Analyzer(joined.Shape()).IsValid(),"Joined vase base is valid");
    const auto joinedHandle=kernel::StoreShape(joined.Shape());
    const auto joinedStep=kernel::OutputStep({joinedHandle});
    if(!joinedStep.HasValue())for(const auto& diagnostic:joinedStep.Diagnostics())
        std::cout<<diagnostic.summaryJa<<" / "<<diagnostic.detailsJa<<'\n';
    Require(joinedStep.HasValue(),"Joined vase STEP retains original signed volume, closed solid and bounds");
    kernel::ReleaseShape(joinedHandle);
    kernel::ReleaseShape(solid.Value().handle);kernel::ReleaseShape(surface.Value().handle);
}
KACHA_V2_TEST(thicken_quality, stl_revolution_poles_have_no_collapsed_facets)
{
    const auto surface=Revolved({{0,0,0},{5,0,0},{5,0,10},{0,0,10}});
    const auto stl=kernel::OutputStl({surface},.01);
    if(!stl.HasValue())for(const auto& diagnostic:stl.Diagnostics())std::cout<<diagnostic.summaryJa<<" / "<<diagnostic.detailsJa<<'\n';
    Require(stl.HasValue(),"Pole surface STL");const auto& bytes=stl.Value();
    std::uint32_t count=0;std::memcpy(&count,bytes.data()+80,4);
    Require(count>0&&bytes.size()==84+50*count,"Nonempty exact binary STL length");
    for(std::size_t i=0;i<count;++i) {
        geometry::Vector3 p[3];
        for(int j=0;j<3;++j) {
            float xyz[3];std::memcpy(xyz,bytes.data()+84+50*i+12+12*j,12);p[j]={xyz[0],xyz[1],xyz[2]};
        }
        Require(geometry::Cross(p[1]-p[0],p[2]-p[0]).Length()>0,"Every written float triangle has positive area");
    }
    kernel::ReleaseShape(surface);
}

namespace {
TopoDS_Face TightParabola()
{
    NCollection_Array2<gp_Pnt> poles(1,3,1,2);
    for(int j=1;j<=2;++j) {poles(1,j)=gp_Pnt(-1,(j-1)*5,3);poles(2,j)=gp_Pnt(0,(j-1)*5,-3);poles(3,j)=gp_Pnt(1,(j-1)*5,3);}
    NCollection_Array1<double> knots(1,2);knots(1)=0;knots(2)=1;
    NCollection_Array1<int> um(1,2),vm(1,2);um.Init(3);vm.Init(2);
    return BRepBuilderAPI_MakeFace(new Geom_BSplineSurface(poles,knots,knots,um,vm,2,1),1e-7).Face();
}
void Diagnose(const base::Result<kernel::ThickenedSolid>& result)
{
    if(!result.HasValue())for(const auto& d:result.Diagnostics())std::cout<<d.summaryJa<<" / "<<d.detailsJa<<std::endl;
}
}
KACHA_V2_TEST(thicken_quality, corrected_opposite_retains_source_and_passes_native_checks_under_focal_limit)
{
    const auto face=TightParabola();
    for(int mode=0;mode<3;++mode) {
        auto input=face;
        if(mode==1)input.Reverse();
        if(mode==2){gp_Trsf tr;tr.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.73);tr.SetTranslationPart(gp_Vec(21,-32,41));input=TopoDS::Face(BRepBuilderAPI_Transform(input,tr,true).Shape());}
        const auto source=kernel::StoreShape(input);const auto original=kernel::CaptureOutputShape(source);
        const auto placement=mode==1?fabrication::ThicknessPlacement::Inside:fabrication::ThicknessPlacement::Outside;
        Require(!kernel::ThickenSurface(source,.3,placement,{}).HasValue(),"strict focal offset refused");
        const auto corrected=kernel::ThickenSurface(source,.3,placement,{},true);Diagnose(corrected);
        Require(corrected.HasValue()&&corrected.Value().corrected,"explicit correction builds native solid");
        const auto& wall=corrected.Value();
        Require(wall.normalThicknessMinMm>0&&wall.normalThicknessMaxMm<=.30001,"positive measured thickness, nominal not falsified");
        Require(wall.maxOffsetDeviationMm>0&&wall.rmsOffsetDeviationMm>0&&wall.rmsOffsetDeviationMm<=wall.maxOffsetDeviationMm,"reported correction errors");
        Require(kernel::CaptureOutputShape(source).Value()==original.Value(),"exact source BRep unchanged");
        TopoDS_Shape nativeWall;Require(kernel::LookupShape(wall.handle,nativeWall),"native wall available");
        const auto skin=BRep_Tool::Surface(input);bool retained=false;
        for(TopExp_Explorer f(nativeWall,TopAbs_FACE);f.More();f.Next()) {
            const auto candidate=BRep_Tool::Surface(TopoDS::Face(f.Current()));bool same=true;
            for(double u:{.2,.5,.8})for(double v:{.2,.5,.8})same=same&&skin->Value(u,v).Distance(candidate->Value(u,v))<1e-8;
            retained=retained||same;
        }
        Require(retained,"original geometric skin is also retained inside the resulting solid");

        const auto check=kernel::CheckSolidForExport(wall.handle,1e-6);
        Require(check.HasValue()&&check.Value().Ok(),"native closure, positive volume and self intersection check");
        const auto encoded=kernel::CaptureOutputShape(wall.handle);const auto restored=kernel::RestoreOutputShape(encoded.Value());
        Require(restored.HasValue()&&kernel::CheckSolidForExport(restored.Value(),1e-6).Value().Ok(),"native result survives serialization");
        kernel::ReleaseShape(restored.Value());kernel::ReleaseShape(wall.handle);kernel::ReleaseShape(source);
    }
}
KACHA_V2_TEST(thicken_quality, corrected_normal_is_independent_of_spline_parameter_speed)
{
    // Both surfaces are the same 10 mm square. Their endpoint tangent speeds
    // approach zero independently of the angle between those tangents.
    for(double slow:{0.0,1e-10}) {
        NCollection_Array2<gp_Pnt> poles(1,6,1,6);
        const std::array<double,6> positions{0,slow,2*slow,10-2*slow,10-slow,10};
        for(int i=1;i<=6;++i)for(int j=1;j<=6;++j)
            poles(i,j)=gp_Pnt(positions[i-1],positions[j-1],0);
        const auto face=BRepBuilderAPI_MakeFace(new Geom_BezierSurface(poles),1e-7).Face();
        Require(BRepCheck_Analyzer(face).IsValid(),"slowly parametrized planar source is valid");
        const auto source=kernel::StoreShape(face);const auto before=kernel::CaptureOutputShape(source);
        const auto wall=kernel::detail::CorrectedThicken(source,.5,fabrication::ThicknessPlacement::Inside,{});
        Diagnose(wall);Require(wall.HasValue(),"small derivative magnitudes do not erase a valid normal");
        Require(wall.Value().normalThicknessMinMm>.49999&&wall.Value().normalThicknessMaxMm<.50001,
            "parameter speed does not change measured thickness");
        const auto check=kernel::CheckSolidForExport(wall.Value().handle,1e-6);
        Require(check.HasValue()&&check.Value().Ok()&&std::abs(check.Value().volumeMm3-50)<1e-4,
            "corrected square is a closed native solid with analytic volume");
        Require(kernel::CaptureOutputShape(source).Value()==before.Value(),"slow source remains byte-identical");
        kernel::ReleaseShape(wall.Value().handle);kernel::ReleaseShape(source);
    }
}
KACHA_V2_TEST(thicken_quality, corrected_normal_handles_interior_stationary_parameters)
{
    // x(t)=5+40*(t-.5)^3 is monotone and covers the square exactly once,
    // although D1 and D2 vanish at its interior stationary parameter.
    for(int mode:{0,1,2}) {
        NCollection_Array2<gp_Pnt> poles(1,4,1,4);
        const std::array<double,4> positions{0,10,0,10};
        for(int i=1;i<=4;++i)for(int j=1;j<=4;++j)
            poles(i,j)=gp_Pnt(positions[i-1],positions[j-1],0);
        auto face=BRepBuilderAPI_MakeFace(new Geom_BezierSurface(poles),1e-7).Face();
        if(mode==1)face.Reverse();
        if(mode==2){gp_Trsf tr;tr.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.73);
            tr.SetTranslationPart(gp_Vec(21,-32,41));face=TopoDS::Face(BRepBuilderAPI_Transform(face,tr,true).Shape());}
        Require(BRepCheck_Analyzer(face).IsValid(),"stationary parameter does not invalidate the planar face");
        const auto source=kernel::StoreShape(face);const auto before=kernel::CaptureOutputShape(source);
        const auto wall=kernel::detail::CorrectedThicken(source,.5,fabrication::ThicknessPlacement::Inside,{});
        Diagnose(wall);Require(wall.HasValue(),"unique limiting normal permits corrected thickness");
        Require(wall.Value().normalThicknessMinMm>.49999&&wall.Value().normalThicknessMaxMm<.50001,
            "interior stationary parameters do not alter wall thickness");
        const auto check=kernel::CheckSolidForExport(wall.Value().handle,1e-6);
        Require(check.HasValue()&&check.Value().Ok()&&std::abs(check.Value().volumeMm3-50)<1e-4,
            "stationary surface makes an oriented closed solid with analytic volume");
        Require(kernel::CaptureOutputShape(source).Value()==before.Value(),"stationary source is unchanged");
        kernel::ReleaseShape(wall.Value().handle);kernel::ReleaseShape(source);
    }
}
KACHA_V2_TEST(thicken_quality, corrected_normal_handles_independent_parameter_rates_on_a_curved_surface)
{
    // An exact R5 quarter cylinder: regular circular U, stationary monotone Z(V).
    // Translating it must not require the two sampling directions to share a step.
    NCollection_Array2<gp_Pnt> poles(1,3,1,4);NCollection_Array2<double> weights(1,3,1,4);
    const std::array<double,4> heights{0,10,0,10};
    for(int i=1;i<=3;++i)for(int j=1;j<=4;++j) {
        poles(i,j)=gp_Pnt(i==3?0:5,i==1?0:5,heights[j-1]);
        weights(i,j)=i==2?std::sqrt(.5):1;
    }
    NCollection_Array1<double> knots(1,2);knots(1)=0;knots(2)=1;
    NCollection_Array1<int> um(1,2),vm(1,2);um.Init(3);vm.Init(4);
    const occ::handle<Geom_Surface> surface=new Geom_BSplineSurface(poles,weights,knots,knots,um,vm,2,3);
    const auto face=BRepBuilderAPI_MakeFace(surface,1e-7).Face();
    gp_Trsf tr;tr.SetTranslation(gp_Vec(1000,2000,3000));
    const auto shifted=BRepBuilderAPI_Transform(face,tr,true).Shape();
    Require(BRepCheck_Analyzer(shifted).IsValid(),"stationary cylinder remains a valid translated source");
    const auto source=kernel::StoreShape(shifted);const auto before=kernel::CaptureOutputShape(source);
    const auto wall=kernel::detail::CorrectedThicken(source,.5,fabrication::ThicknessPlacement::Inside,{});
    Diagnose(wall);Require(wall.HasValue(),"independent parameter steps resolve the unique curved normal");
    Require(wall.Value().normalThicknessMinMm>.4&&wall.Value().normalThicknessMaxMm<.50001,
        "corrected inner skin reports positive bounded thickness");
    const auto check=kernel::CheckSolidForExport(wall.Value().handle,1e-6);
    Require(check.HasValue()&&check.Value().Ok()&&check.Value().volumeMm3>30&&check.Value().volumeMm3<38,
        "corrected cylinder passes native checks and annular volume bounds");
    Require(kernel::CaptureOutputShape(source).Value()==before.Value(),"translated cylinder source remains exact");
    kernel::ReleaseShape(wall.Value().handle);kernel::ReleaseShape(source);
}
KACHA_V2_TEST(thicken_quality, corrected_crease_reports_both_sided_normals_at_stationary_parameters)
{
    // Two planes meet at a real native C0 knot; the other parameter is monotone
    // but stationary internally. Neither condition moves or refits the source.
    NCollection_Array2<gp_Pnt> poles(1,3,1,4);const std::array<double,4> positions{0,10,0,10};
    for(int i=1;i<=3;++i)for(int j=1;j<=4;++j)
        poles(i,j)=gp_Pnt(5*(i-1),positions[j-1],i==3?1:0);
    NCollection_Array1<double> uk(1,3),vk(1,2);uk(1)=0;uk(2)=.5;uk(3)=1;vk(1)=0;vk(2)=1;
    NCollection_Array1<int> um(1,3),vm(1,2);um.Init(2);um(2)=1;vm.Init(4);
    const occ::handle<Geom_Surface> surface=new Geom_BSplineSurface(poles,uk,vk,um,vm,1,3);
    const auto face=BRepBuilderAPI_MakeFace(surface,1e-7).Face();
    Require(BRepCheck_Analyzer(face).IsValid(),"one native face can include a geometric crease");
    const auto source=kernel::StoreShape(face);const auto before=kernel::CaptureOutputShape(source);
    const auto wall=kernel::detail::CorrectedThicken(source,.5,fabrication::ThicknessPlacement::Inside,{});
    Diagnose(wall);Require(wall.HasValue(),"explicit opposite-skin correction handles both sides of a crease");
    Require(wall.Value().normalThicknessMinMm>.45&&wall.Value().normalThicknessMaxMm<.50001,
        "all one-sided normal thicknesses remain positive and bounded");
    const auto check=kernel::CheckSolidForExport(wall.Value().handle,1e-6);
    Require(check.HasValue()&&check.Value().Ok(),"creased corrected skin passes native solid checks");
    Require(kernel::CaptureOutputShape(source).Value()==before.Value(),"creased source remains exact");
    kernel::ReleaseShape(wall.Value().handle);kernel::ReleaseShape(source);
}
KACHA_V2_TEST(thicken_quality, corrected_normal_does_not_bridge_a_folded_parameter_sheet)
{
    const auto baseline=kernel::CachedShapeCount();
    NCollection_Array2<gp_Pnt> poles(1,4,1,2);const std::array<double,4> positions{0,10,10,0};
    for(int i=1;i<=4;++i)for(int j=1;j<=2;++j)poles(i,j)=gp_Pnt(positions[i-1],10*(j-1),0);
    const auto source=kernel::StoreShape(BRepBuilderAPI_MakeFace(new Geom_BezierSurface(poles),1e-7).Face());
    const auto before=kernel::CaptureOutputShape(source);
    const auto wall=kernel::detail::CorrectedThicken(source,.5,fabrication::ThicknessPlacement::Inside,{});
    const bool refused=!wall.HasValue();if(wall.HasValue())kernel::ReleaseShape(wall.Value().handle);
    Require(kernel::CaptureOutputShape(source).Value()==before.Value(),"refused folded source remains unchanged");
    kernel::ReleaseShape(source);
    Require(refused&&kernel::CachedShapeCount()==baseline,"opposite normal limits cannot create a wall or leak a handle");
}
KACHA_V2_TEST(thicken_quality, corrected_normal_refuses_a_corner_with_path_dependent_limits)
{
    // S(u,v)=(u*u,v*v,u*v). The two boundary approaches give different
    // limiting normals at (0,0), although none of its four edges is collapsed.
    NCollection_Array2<gp_Pnt> poles(1,3,1,3);const std::array<double,3> square{0,0,1};
    for(int i=1;i<=3;++i)for(int j=1;j<=3;++j)
        poles(i,j)=gp_Pnt(square[i-1],square[j-1],(i-1)*(j-1)*.25);
    const auto face=BRepBuilderAPI_MakeFace(new Geom_BezierSurface(poles),1e-7).Face();
    Require(BRepCheck_Analyzer(face).IsValid(),"singular corner can belong to a topologically valid source");
    const auto source=kernel::StoreShape(face);const auto before=kernel::CaptureOutputShape(source);
    const auto wall=kernel::detail::CorrectedThicken(source,.1,fabrication::ThicknessPlacement::Outside,{});
    const bool refused=!wall.HasValue();if(wall.HasValue())kernel::ReleaseShape(wall.Value().handle);
    Require(kernel::CaptureOutputShape(source).Value()==before.Value(),"nonunique source corner remains unchanged");
    kernel::ReleaseShape(source);Require(refused,"a parameter aspect ratio must not invent a unique corner normal");
}
KACHA_V2_TEST(thicken_quality, corrected_centered_preserves_reference_and_rejects_excessive_thickness)
{
    const auto source=kernel::StoreShape(TightParabola());const auto original=kernel::CaptureOutputShape(source);
    const auto center=kernel::ThickenSurface(source,.6,fabrication::ThicknessPlacement::Centered,{},true);Diagnose(center);
    Require(center.HasValue()&&center.Value().corrected,"both new skins corrected");
    Require(kernel::CaptureOutputShape(source).Value()==original.Value(),"center reference unchanged");
    Require(!kernel::ThickenSurface(source,3,fabrication::ThicknessPlacement::Outside,{},true).HasValue(),"inverted or grossly displaced opposite skin refused");
    kernel::ReleaseShape(center.Value().handle);kernel::ReleaseShape(source);
}
KACHA_V2_TEST(thicken_quality, correction_option_keeps_exact_radius_when_possible_and_does_not_fill_holes)
{
    const occ::handle<Geom_Surface> cylinder=new Geom_CylindricalSurface(gp_Ax3(),5);
    const auto source=kernel::StoreShape(BRepBuilderAPI_MakeFace(cylinder,0,std::acos(-1.0)/2,0,10,1e-7).Face());
    const auto exact=kernel::ThickenSurface(source,1,fabrication::ThicknessPlacement::Inside,{},true);
    Require(exact.HasValue()&&!exact.Value().corrected,"R5 to R4 remains exact in correction mode");
    Require(exact.Value().normalThicknessMinMm==1&&exact.Value().normalThicknessMaxMm==1&&exact.Value().maxOffsetDeviationMm==0,"strict metrics stay exact");
    kernel::ReleaseShape(exact.Value().handle);kernel::ReleaseShape(source);
    BRepBuilderAPI_MakeFace maker(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-5,5,-5,5);
    BRepBuilderAPI_MakeWire hole(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(0,0,1)),1)).Edge());
    auto reversed=hole.Wire();reversed.Reverse();maker.Add(reversed);
    const auto holeSource=kernel::StoreShape(maker.Face());
    Require(!kernel::detail::CorrectedThicken(holeSource,.3,fabrication::ThicknessPlacement::Inside,{}).HasValue(),"unsupported correction never fills an inner boundary");
    kernel::ReleaseShape(holeSource);
}
KACHA_V2_TEST(thicken_quality, intersection_partition_preserves_dense_native_surface_domains_and_source)
{
    for(int mode:{0,1,2}) {
        auto face=TightParabola();
        auto spline=occ::handle<Geom_BSplineSurface>::DownCast(BRep_Tool::Surface(face));
        Require(!spline.IsNull(),"native test spline available");
        for(int i=1;i<80;++i)spline->InsertUKnot(i/80.0,1,1e-12,false);
        if(mode==1)face.Reverse();
        if(mode==2){gp_Trsf tr;tr.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.71);
            tr.SetTranslationPart(gp_Vec(12,-23,34));face=TopoDS::Face(BRepBuilderAPI_Transform(face,tr,true).Shape());}
        const auto source=kernel::StoreShape(face);const auto original=kernel::CaptureOutputShape(source);
        // The helper receives a private copy, as it does in production.
        const auto copy=BRepBuilderAPI_Copy(face,true,false).Shape();
        const auto split=kernel::detail::PartitionIntersectionFaces(copy);
        Require(BRepCheck_Analyzer(split).IsValid(),"exact partition has valid parameter topology");
        int count=0;const auto wanted=BRep_Tool::Surface(face);
        for(TopExp_Explorer e(split,TopAbs_FACE);e.More();e.Next()) {
            ++count;const auto f=TopoDS::Face(e.Current());const auto actual=BRep_Tool::Surface(f);
            double u0,u1,v0,v1;BRepTools::UVBounds(f,u0,u1,v0,v1);
            for(double a:{0.0,.2,.5,.8,1.0})for(double b:{0.0,.25,.5,.75,1.0}) {
                const auto u=u0+(u1-u0)*a,v=v0+(v1-v0)*b;
                test::RequireNear(actual->Value(u,v).Distance(wanted->Value(u,v)),0,1e-9,"native position and global UV preserved");
            }
            Require(f.Orientation()==face.Orientation(),"reversed material-side orientation retained");
        }
        Require(count>1,"dense exact geometry is partitioned for intersection analysis");
        Require(kernel::CaptureOutputShape(source).Value()==original.Value(),"analysis never mutates authoritative native source");
        kernel::ReleaseShape(source);
    }
}
KACHA_V2_TEST(thicken_quality, bounded_boundary_handles_high_degree_nonuniform_rational_trimmed_reversed_edges)
{
    for(int mode:{0,1,2}) {
        NCollection_Array1<gp_Pnt> poles(1,15);NCollection_Array1<double> weights(1,15);
        for(int i=1;i<=15;++i){poles(i)=gp_Pnt(i,i%2?4:-3,.1*i*i);weights(i)=mode==1&&i%2?.7:1;}
        NCollection_Array1<double> knots(1,3);knots(1)=0;knots(2)=.0001;knots(3)=1;
        NCollection_Array1<int> multiplicities(1,3);multiplicities(1)=8;multiplicities(2)=7;multiplicities(3)=8;
        const occ::handle<Geom_BSplineCurve> spline=new Geom_BSplineCurve(poles,weights,knots,multiplicities,7);
        auto edge=BRepBuilderAPI_MakeEdge(spline,.000025,.75).Edge();
        if(mode==1)edge.Reverse();
        if(mode==2){gp_Trsf tr;tr.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.73);
            tr.SetTranslationPart(gp_Vec(111,-222,333));edge=TopoDS::Edge(BRepBuilderAPI_Transform(edge,tr,true).Shape());}
        const auto handle=kernel::StoreShape(edge);const auto before=kernel::CaptureOutputShape(handle);
        const auto result=kernel::detail::BoundaryCurves(edge,.001);
        Require(result.HasValue(),"high-degree uneven native knots become smooth bounded curves");
        Require(result.Value().maxDeviationBoundMm<=.001&&result.Value().curves.size()<512,"deviation bound holds without a dense point cloud");
        double first,last;const auto native=BRep_Tool::Curve(edge,first,last);
        for(std::size_t at=0;at<result.Value().curves.size();++at) {
            const auto& c=result.Value().curves[at];const auto [a,b]=result.Value().parameters[at];
            Require(c.Kind()==geometry::CurveKind::CubicBezier,"editable smooth geometry, never sampled line segments");
            for(int i=0;i<=64;++i) {
                const double t=i/64.0;const auto p=c.Evaluate(t);const auto q=native->Value(a+(b-a)*t);
                Require(gp_Pnt(p.x,p.y,p.z).Distance(q)<=result.Value().maxDeviationBoundMm+1e-10,"dense independent samples lie inside reported bound");
            }
            if(at)Require(geometry::Distance(result.Value().curves[at-1].EndPoint(),c.StartPoint())<1e-9,"connected spans retain shared endpoints");
        }
        const auto grouped=kernel::OutputBoundaryWires(handle,.001);
        Require(grouped.HasValue()&&grouped.Value().size()==1&&!grouped.Diagnostics().empty(),"one native edge stays one wire and deviation is reported");
        Require(kernel::CaptureOutputShape(handle).Value()==before.Value(),"conversion does not mutate the native edge");
        kernel::ReleaseShape(handle);
    }
}

namespace {
struct CylinderTrim {
    occ::handle<Geom_Surface> surface;
    TopoDS_Face face;
};
TopoDS_Face PerforatedFace(const occ::handle<Geom_Surface>& surface,double angle,double height,bool diagonal) {
    const auto wire=[&](std::vector<gp_Pnt2d> points,bool reverse=false) {
        if(reverse)std::reverse(points.begin(),points.end());BRepBuilderAPI_MakeWire made;
        for(std::size_t i=0;i<points.size();++i) {
            const auto a=points[i],b=points[(i+1)%points.size()];gp_Vec2d d(a,b);
            const occ::handle<Geom2d_Curve> line=new Geom2d_Line(a,gp_Dir2d(d));
            BRepBuilderAPI_MakeEdge edge(line,surface,0,d.Magnitude());Require(edge.IsDone(),"native trim edge");made.Add(edge.Edge());
        }
        Require(made.IsDone(),"native oriented trim wire");return made.Wire();
    };
    BRepBuilderAPI_MakeFace face(surface,wire({{0,0},{angle,0},{angle,height},{angle*.8,height},
        {angle*.8,height*.7},{angle*.6,height*.7},{angle*.6,height},{0,height}}),true);
    face.Add(wire({{angle*.1,height/6},{angle*.25,height/6},{angle*.25,height*.4},{angle*.1,height*.4}},true));
    if(diagonal)face.Add(wire({{angle*.35,height*.1},{angle*.55,height*.1},{angle*.55,height*.5}},true));
    else face.Add(wire({{angle*.35,height*.1},{angle*.55,height*.1},{angle*.55,height*.5},{angle*.35,height*.5}},true));
    Require(face.IsDone(),"notched face with two holes");const auto result=face.Face();
    Require(BRepLib::BuildCurves3d(result),"native UV edges have exact spatial curves");
    Require(BRepCheck_Analyzer(result).IsValid(),"valid perforated native face");return result;
}
CylinderTrim PerforatedCylinder(bool diagonal) {
    const double angle=std::acos(-1.)*.5;
    const occ::handle<Geom_Surface> surface=new Geom_CylindricalSurface(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(0,0,1)),12);
    return {surface,PerforatedFace(surface,angle,30,diagonal)};
}
}
KACHA_V2_TEST(thicken_quality, notched_offset_with_multiple_holes_preserves_analytic_signed_volume_and_centroid)
{
    const auto fixture=PerforatedCylinder(false);const double angle=std::acos(-1.)*.5;
    const auto theta=[&](double u){const auto p=fixture.surface->Value(u,0);return std::atan2(p.Y(),p.X());};
    const double r=12,R=12.5;
    double volume=0,mx=0,my=0,mz=0;
    const auto region=[&](double u0,double u1,double z0,double z1,int sign) {
        const auto a=theta(u0),b=theta(u1);const double area=(R*R-r*r)*.5,radial=(R*R*R-r*r*r)/3;
        volume+=sign*area*(b-a)*(z1-z0);
        mx+=sign*radial*(std::sin(b)-std::sin(a))*(z1-z0);
        my+=sign*radial*(std::cos(a)-std::cos(b))*(z1-z0);
        mz+=sign*area*(b-a)*(z1*z1-z0*z0)*.5;
    };
    region(0,angle,0,30,1);region(angle*.6,angle*.8,21,30,-1);
    region(angle*.1,angle*.25,5,12,-1);region(angle*.35,angle*.55,3,15,-1);
    BRepOffsetAPI_MakeThickSolid made;made.MakeThickSolidBySimple(fixture.face,.5);Require(made.IsDone(),"perforated offset wall built");
    auto solid=TopoDS::Solid(made.Shape());Require(BRepLib::OrientClosedSolid(solid),"perforated wall oriented");
    gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,1,0)),.7);transform.SetTranslationPart(gp_Vec(1000,-2000,3000));
    auto placed=BRepBuilderAPI_Transform(solid,transform,false).Shape();
    gp_Pnt expected(mx/volume,my/volume,mz/volume);expected.Transform(transform);
    for(bool reverse:{false,true}) {
        GProp_GProps result;kernel::detail::VolumeProperties(placed,result,true);
        test::RequireNear(result.Mass(),reverse?-volume:volume,volume*1e-8,"holes and notch keep analytic signed volume");
        test::RequireNear(result.CentreOfMass().Distance(expected),0,1e-7,"transformed perforated wall analytic centroid");placed.Reverse();
    }
}
KACHA_V2_TEST(thicken_quality, diagonal_hole_is_integrated_as_native_trim_not_uv_rectangle)
{
    const auto fixture=PerforatedCylinder(true);const double angle=std::acos(-1.)*.5;
    const auto theta=[&](double u){const auto p=fixture.surface->Value(u,0);return std::atan2(p.Y(),p.X());};
    // Independent 1D integration by parts of the analytic cylinder sector.
    // No surface normals or flux quadrature from VolumeProperties are reused.
    const auto triangle=[&](int n) {
        const double a=angle*.35,b=angle*.55;double sum=theta(a)+theta(b);
        for(int i=1;i<n;++i)sum+=(i%2?4:2)*theta(a+(b-a)*i/n);
        return 12*(theta(b)-sum/(3*n));
    };
    const double triangleArea=triangle(10000);
    test::RequireNear(triangleArea,triangle(20000),1e-11,"independent triangle reference converges");
    const double area=angle*30-(theta(angle*.8)-theta(angle*.6))*9
        -(theta(angle*.25)-theta(angle*.1))*7-triangleArea;
    const double expected=area*(12.5*12.5-12*12)*.5;
    BRepOffsetAPI_MakeThickSolid made;made.MakeThickSolidBySimple(fixture.face,.5);Require(made.IsDone(),"diagonal hole offset built");
    auto solid=TopoDS::Solid(made.Shape());Require(BRepLib::OrientClosedSolid(solid),"diagonal hole wall oriented");
    const auto handle=kernel::StoreShape(solid);const auto before=kernel::CaptureOutputShape(handle);Require(before.HasValue(),"capture before mass query");
    GProp_GProps result;kernel::detail::VolumeProperties(solid,result);
    test::RequireNear(result.Mass(),expected,expected*1e-8,"diagonal boundary material is subtracted exactly");
    Require(before.Value()==kernel::CaptureOutputShape(handle).Value(),"mass query never alters stored skin or trim wires");
    kernel::ReleaseShape(handle);
}


KACHA_V2_TEST(thicken_quality, planar_multiknot_spline_keeps_native_holes_notch_and_oriented_thickness)
{
    NCollection_Array2<gp_Pnt> poles(1,97,1,2);
    NCollection_Array1<double> uk(1,97),vk(1,2);NCollection_Array1<int> um(1,97),vm(1,2);
    vk(1)=0;vk(2)=30;vm.Init(2);um.Init(1);um(1)=um(97)=2;
    for(int i=1;i<=97;++i){uk(i)=double(i-1)/96;for(int j=1;j<=2;++j)poles(i,j)=gp_Pnt(30*uk(i),(j-1)*30,0);}
    const occ::handle<Geom_BSplineSurface> spline=new Geom_BSplineSurface(poles,uk,vk,um,vm,1,1);
    const auto face=PerforatedFace(spline,1,30,true);
    gp_Trsf placed;placed.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,1,0)),.7);placed.SetTranslationPart(gp_Vec(1000,-2000,3000));
    const double area=30*(30-.2*9-.15*7-.2*12*.5),thickness=.4;
    for(bool reverse:{false,true})for(auto placement:{fabrication::ThicknessPlacement::Centered,
        fabrication::ThicknessPlacement::Outside,fabrication::ThicknessPlacement::Inside}) {
        auto oriented=BRepBuilderAPI_Transform(face,placed,false).Shape();if(reverse)oriented.Reverse();
        const auto source=kernel::StoreShape(oriented);const auto before=kernel::CaptureOutputShape(source);
        Require(before.HasValue(),"capture placed multiknot planar source");
        const auto prism=kernel::detail::ThickenPlanarSpline(oriented,thickness,placement,{});
        Require(prism.has_value(),"full control hull proves a planar source without fragmenting it");
        const auto handle=kernel::StoreShape(*prism);const auto checked=kernel::CheckSolidForExport(handle,1e-6);
        Require(checked.HasValue()&&checked.Value().Ok(),"closed oriented planar spline wall");
        test::RequireNear(checked.Value().volumeMm3,area*thickness,1e-6,"independent trimmed planar area times thickness");
        const auto step=kernel::OutputStep({handle});Require(step.HasValue(),"planar spline wall passes real STEP validation");
        STEPControl_Reader reader;std::istringstream input(step.Value());
        Require(reader.ReadStream("planar-spline.step",input)==IFSelect_RetDone&&reader.TransferRoots(),"read independent STEP result");
        for(const auto& shape:{*prism,reader.OneShape()}) {
            IntCurvesFace_ShapeIntersector ray;ray.Load(shape,1e-7);
            for(const auto& xy:{std::pair{4.5,6.},std::pair{15.,6.},std::pair{21.,25.}}) {
                ray.Perform(gp_Lin(gp_Pnt(xy.first,xy.second,-2).Transformed(placed),gp_Dir(0,0,1).Transformed(placed)),0,5);
                Require(ray.NbPnt()==0,"both holes and the outer notch stay empty");
            }
            ray.Perform(gp_Lin(gp_Pnt(27,17,-2).Transformed(placed),gp_Dir(0,0,1).Transformed(placed)),0,5);
            Require(ray.NbPnt()==2,"two planar skins remain");
            const double z0=(reverse?-1.:1.)*(placement==fabrication::ThicknessPlacement::Inside?-thickness:
                placement==fabrication::ThicknessPlacement::Centered?-thickness*.5:0.);
            const double z1=z0+(reverse?-thickness:thickness);gp_Trsf undo=placed.Inverted();
            const double a=ray.Pnt(1).Transformed(undo).Z(),b=ray.Pnt(2).Transformed(undo).Z();
            test::RequireNear(std::min(a,b),std::min(z0,z1),1e-6,"correct low skin in original frame");
            test::RequireNear(std::max(a,b),std::max(z0,z1),1e-6,"correct high skin in original frame");
        }
        Require(before.Value()==kernel::CaptureOutputShape(source).Value(),"source support and every trim curve remain exact");
        kernel::ReleaseShape(handle);kernel::ReleaseShape(source);
    }
    auto warped=occ::handle<Geom_BSplineSurface>::DownCast(spline->Copy());
    warped->SetPole(49,2,gp_Pnt(15,30,1e-8));
    const auto curved=PerforatedFace(warped,1,30,true);
    Require(!kernel::detail::ThickenPlanarSpline(curved,.4,fabrication::ThicknessPlacement::Outside,{}),
        "even a small real departure from the plane is never flattened");
}
KACHA_V2_TEST(thicken_quality, planar_prism_direction_uses_trimmed_face_not_control_hull_order)
{
    for(bool trimmed:{false,true}) {
        const int count=trimmed?3:2;NCollection_Array2<gp_Pnt> poles(1,count,1,2);
        NCollection_Array1<double> uk(1,count),vk(1,2);NCollection_Array1<int> um(1,count),vm(1,2);
        um.Init(1);um(1)=um(count)=2;vm.Init(2);vk(1)=0;vk(2)=1;
        for(int i=1;i<=count;++i) {
            uk(i)=double(i-1)/(count-1);
            const double x=trimmed?(i==1?0:i==2?2:1):(i-1)*10.;
            poles(i,1)=gp_Pnt(x,0,0);poles(i,2)=gp_Pnt(x,trimmed?1:i==1?1:10,0);
        }
        const occ::handle<Geom_BSplineSurface> spline=new Geom_BSplineSurface(poles,uk,vk,um,vm,1,1);
        const auto face=BRepBuilderAPI_MakeFace(spline,trimmed?.5:0.,1.,0.,1.,1e-7).Face();
        for(bool reverse:{false,true})for(auto placement:{fabrication::ThicknessPlacement::Outside,fabrication::ThicknessPlacement::Inside}) {
            TopoDS_Shape source=face;if(reverse)source.Reverse();
            const auto result=kernel::detail::ThickenPlanarSpline(source,.2,placement,{});
            Require(result.has_value(),"valid trimmed planar face supports exact oriented prism");
            IntCurvesFace_ShapeIntersector ray;ray.Load(*result,1e-7);
            ray.Perform(gp_Lin(gp_Pnt(trimmed?1.5:5.,.5,-1),gp_Dir(0,0,1)),0,2);
            Require(ray.NbPnt()==2,"prism has two skins at material point");
            const double normal=(trimmed?-1.:1.)*(reverse?-1.:1.);
            const double shift=normal*(placement==fabrication::ThicknessPlacement::Outside?.2:-.2);
            test::RequireNear(std::min(ray.Pnt(1).Z(),ray.Pnt(2).Z()),std::min(0.,shift),1e-7,"oriented low skin");
            test::RequireNear(std::max(ray.Pnt(1).Z(),ray.Pnt(2).Z()),std::max(0.,shift),1e-7,"oriented high skin");
        }
        if(trimmed) {
            const auto folded=BRepBuilderAPI_MakeFace(spline,1e-7).Face();
            Require(!kernel::detail::ThickenPlanarSpline(folded,.2,fabrication::ThicknessPlacement::Outside,{}),
                "a source whose active cells reverse orientation is not flattened");
        }
    }
}
KACHA_V2_TEST(thicken_quality, redundant_bilinear_knots_keep_trim_placement_and_bounded_normal)
{
    NCollection_Array2<gp_Pnt> poles(1,97,1,2);
    NCollection_Array1<double> uk(1,97),vk(1,2);NCollection_Array1<int> um(1,97),vm(1,2);
    vk(1)=0;vk(2)=30;vm.Init(2);um.Init(1);um(1)=um(97)=2;
    for(int i=1;i<=97;++i){uk(i)=double(i-1)/96;for(int j=1;j<=2;++j)poles(i,j)=gp_Pnt(30*uk(i),(j-1)*30,5*uk(i)*(j-1));}
    const occ::handle<Geom_BSplineSurface> spline=new Geom_BSplineSurface(poles,uk,vk,um,vm,1,1);
    const auto face=PerforatedFace(spline,1,30,true);
    gp_Trsf placed;placed.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,1,0)),.7);placed.SetTranslationPart(gp_Vec(1000,-2000,3000));
    for(bool reverse:{false,true}) {
        auto source=BRepBuilderAPI_Transform(face,placed,false).Shape();if(reverse)source.Reverse();
        const auto original=kernel::StoreShape(source);const auto before=kernel::CaptureOutputShape(original);
        const auto simple=kernel::detail::BilinearSupportCopy(source,.4,.01);
        Require(simple.has_value(),"redundant grid has bounded equivalent bilinear support");
        TopExp_Explorer faces(*simple,TopAbs_FACE);const auto copy=TopoDS::Face(faces.Current());faces.Next();
        Require(!faces.More()&&copy.Orientation()==source.Orientation(),"one face and its orientation remain");
        BRepAdaptor_Surface adaptor(copy,false);const auto support=adaptor.BSpline();
        Require(support->NbUKnots()==2&&support->NbVKnots()==2,"calculation support has no redundant knot seams");
        for(int i=1;i<=97;++i)for(int j=1;j<=2;++j)
            test::RequireNear(support->Value(uk(i),vk(j)).Distance(spline->Value(uk(i),vk(j)).Transformed(placed)),0,1e-9,"location and same UV preserved");
        const auto wall=kernel::detail::ThickenSmoothSpans(source,.4,fabrication::ThicknessPlacement::Centered,{});
        Require(wall.has_value(),"equivalent support thickens and passes strict STEP");
        const auto handle=kernel::StoreShape(*wall);const auto step=kernel::OutputStep({handle});
        Require(step.HasValue(),"curved two-hole notched bilinear wall exports through real STEP reader");
        Require(before.Value()==kernel::CaptureOutputShape(original).Value(),"all original curves and redundant source knots remain unchanged");
        kernel::ReleaseShape(handle);kernel::ReleaseShape(original);
    }
    auto changed=occ::handle<Geom_BSplineSurface>::DownCast(spline->Copy());
    changed->SetPole(49,2,gp_Pnt(15,30,2.5+1e-7));
    Require(!kernel::detail::BilinearSupportCopy(PerforatedFace(changed,1,30,true),.4,.01),"real geometric variation is not removed");
    NCollection_Array2<gp_Pnt> narrow(1,3,1,2);NCollection_Array1<double> nk(1,3);NCollection_Array1<int> nm(1,3);
    nk(1)=0;nk(2)=1e-10;nk(3)=1;nm(1)=nm(3)=2;nm(2)=1;
    for(int i=1;i<=3;++i)for(int j=1;j<=2;++j)narrow(i,j)=gp_Pnt(30*nk(i),(j-1)*30,i==2?1e-11:0);
    const occ::handle<Geom_Surface> sharp=new Geom_BSplineSurface(narrow,nk,vk,nm,vm,1,1);
    Require(!kernel::detail::BilinearSupportCopy(BRepBuilderAPI_MakeFace(sharp,1e-7).Face(),.4,.01),
        "small position error in a narrow cell cannot hide a large thickness normal error");
}
KACHA_V2_TEST(thicken_quality, c1_support_uses_existing_offset_without_c0_span_preprocessing)
{
    NCollection_Array2<gp_Pnt> poles(1,4,1,2);
    const std::array<double,4> x{0,3,7,10},z{0,1,1,0};
    for(int i=1;i<=4;++i)for(int j=1;j<=2;++j)poles(i,j)=gp_Pnt(x[i-1],(j-1)*5,z[i-1]);
    NCollection_Array1<double> uk(1,3),vk(1,2);uk(1)=0;uk(2)=.5;uk(3)=1;vk(1)=0;vk(2)=1;
    NCollection_Array1<int> um(1,3),vm(1,2);um(1)=3;um(2)=1;um(3)=3;vm.Init(2);
    const occ::handle<Geom_Surface> surface=new Geom_BSplineSurface(poles,uk,vk,um,vm,2,1);
    Require(surface->Continuity()==GeomAbs_C1,"fixture has a C1 parameter join");
    const auto face=BRepBuilderAPI_MakeFace(surface,1e-7).Face();
    const auto source=kernel::StoreShape(face);const auto before=kernel::CaptureOutputShape(source);
    Require(before.HasValue(),"capture unchanged C1 source");
    Require(!kernel::detail::ThickenSmoothSpans(face,.2,fabrication::ThicknessPlacement::Outside,{}),
        "C1 supports bypass C0-specific splitting and quadrature");
    const auto built=kernel::ThickenSurface(source,.2,fabrication::ThicknessPlacement::Outside,{});
    Require(built.HasValue(),"C1 support retains the established native thickening path: "+built.FirstSummaryJa());
    Require(before.Value()==kernel::CaptureOutputShape(source).Value(),"C1 support is never modified by either path");
    kernel::ReleaseShape(built.Value().handle);kernel::ReleaseShape(source);
}
KACHA_V2_TEST(thicken_quality, exact_c0_cylinder_spans_preserve_two_holes_thickness_and_step)
{
    const double pi=std::acos(-1.),w=std::cos(pi/8),c=12/std::sqrt(2.),t=12*std::tan(pi/8);
    NCollection_Array2<gp_Pnt> poles(1,5,1,2);NCollection_Array2<double> weights(1,5,1,2);
    const std::array<gp_Pnt,5> xy{{{12,0,0},{12,t,0},{c,c,0},{t,12,0},{0,12,0}}};
    const std::array<double,5> ws{1,w,1,w,1};
    for(int i=1;i<=5;++i)for(int j=1;j<=2;++j){poles(i,j)=gp_Pnt(xy[i-1].X(),xy[i-1].Y(),(j-1)*30);weights(i,j)=ws[i-1];}
    NCollection_Array1<double> uk(1,3),vk(1,2);uk(1)=0;uk(2)=.4;uk(3)=1;vk(1)=0;vk(2)=30;
    NCollection_Array1<int> um(1,3),vm(1,2);um(1)=3;um(2)=2;um(3)=3;vm.Init(2);
    const occ::handle<Geom_Surface> surface=new Geom_BSplineSurface(poles,weights,uk,vk,um,vm,2,1);
    Require(surface->Continuity()==GeomAbs_C0,"unequal rational parameter spans are globally C0");
    const auto face=PerforatedFace(surface,1,30,true);
    Require(kernel::detail::ThickenSmoothSpans(face,.5,fabrication::ThicknessPlacement::Centered,{}).has_value(),"exact span path is used");
    const auto theta=[&](double u){const auto p=surface->Value(u,0);return std::atan2(p.Y(),p.X());};
    const auto triangle=[&](int n){double sum=theta(.35)+theta(.55);for(int i=1;i<n;++i)sum+=(i%2?4:2)*theta(.35+.2*i/n);return 12*(theta(.55)-sum/(3*n));};
    const double triangular=triangle(10000);test::RequireNear(triangular,triangle(20000),1e-10,"independent angular integral converges");
    const double area=pi*.5*30-(theta(.8)-theta(.6))*9-(theta(.25)-theta(.1))*7-triangular;
    for(bool individual:{false,true})for(bool reverse:{false,true})for(auto placement:{fabrication::ThicknessPlacement::Centered,
        fabrication::ThicknessPlacement::Outside,fabrication::ThicknessPlacement::Inside}) {
        TopoDS_Shape oriented=face;if(reverse)oriented.Reverse();
        const auto source=kernel::StoreShape(oriented);
        const auto before=kernel::CaptureOutputShape(source);Require(before.HasValue(),"source native trim captured");
        const auto built=kernel::ThickenSurface(source,.5,placement,{});
        Require(built.HasValue(),"C0 trimmed source thickens: "+built.FirstSummaryJa());
        auto made=built.Value();
        if(individual) {
            const auto separate=kernel::detail::ThickenSmoothSpans(oriented,.5,placement,{},true);
            Require(separate.has_value(),"independent exact span walls unite without filling openings");
            kernel::ReleaseShape(made.handle);made.handle=kernel::StoreShape(*separate);
            GProp_GProps facts;kernel::detail::VolumeProperties(*separate,facts);made.volumeMm3=facts.Mass();
        }
        double r=11.75,R=12.25;
        if(placement!=fabrication::ThicknessPlacement::Centered) {
            const bool inward=(placement==fabrication::ThicknessPlacement::Inside)!=reverse;
            r=inward?11.5:12;R=inward?12:12.5;
        }
        const double expected=area*(R*R-r*r)*.5;
        test::RequireNear(made.volumeMm3,expected,expected*1e-6,"both holes and notch retain analytic material volume");
        const auto step=kernel::OutputStep({made.handle});
        Require(step.HasValue(),"exact smooth offset spans survive strict STEP readback: "+step.FirstSummaryJa()+
            (step.Diagnostics().empty()?std::string():step.Diagnostics().front().detailsJa));
        STEPControl_Reader reader;std::istringstream input(step.Value());
        Require(reader.ReadStream("c0-test.step",input)==IFSelect_RetDone&&reader.TransferRoots(),"read real STEP output");
        TopoDS_Shape native;Require(kernel::LookupShape(made.handle,native),"native result exists");
        for(const auto& solid:{native,reader.OneShape()}) {
            IntCurvesFace_ShapeIntersector ray;ray.Load(solid,1e-7);
            for(double u:{.1,.39,.41,.9}) {
                const double a=theta(u);ray.Perform(gp_Lin(gp_Pnt(0,0,17),gp_Dir(std::cos(a),std::sin(a),0)),0,20);
                Require(ray.NbPnt()==2,"two real offset skins on either side of the parameter seam");
                const double aRadius=std::hypot(ray.Pnt(1).X(),ray.Pnt(1).Y()),bRadius=std::hypot(ray.Pnt(2).X(),ray.Pnt(2).Y());
                test::RequireNear(std::min(aRadius,bRadius),r,1e-6,"inner skin has requested physical radius");
                test::RequireNear(std::max(aRadius,bRadius),R,1e-6,"outer skin has requested physical radius");
            }
            for(const auto& uz:{std::pair{.15,6.},std::pair{.5,6.},std::pair{.7,25.}}) {
                const double a=theta(uz.first);ray.Perform(gp_Lin(gp_Pnt(0,0,uz.second),gp_Dir(std::cos(a),std::sin(a),0)),0,20);
                Require(ray.NbPnt()==0,"neither hole nor outer notch is filled");
            }
        }
        Require(before.Value()==kernel::CaptureOutputShape(source).Value(),"source geometry and hole boundaries never change");
        kernel::ReleaseShape(made.handle);kernel::ReleaseShape(source);
    }
}

KACHA_V2_TEST(thicken_quality, genuinely_creased_carrier_is_not_misclassified_as_smooth_parameter_spans)
{
    std::ifstream input(std::filesystem::path(KACHACAD_TEST_FIXTURE_DIR)/"concave-carrier-c0.brep");
    Require(input.good(),"actual concave source fixture exists");
    TopoDS_Shape source;BRep_Builder builder;BRepTools::Read(source,input,builder);
    Require(!source.IsNull()&&BRepCheck_Analyzer(source).IsValid(),"valid original trimmed source");
    const auto handle=kernel::StoreShape(source);const auto before=kernel::CaptureOutputShape(handle);
    Require(before.HasValue(),"source captured before offset");
    Require(!kernel::detail::ThickenSmoothSpans(source,.5,fabrication::ThicknessPlacement::Centered,{}),
        "real normal discontinuities are never silently connected by the smooth-span path");
    const auto made=kernel::ThickenSurface(handle,.5,fabrication::ThicknessPlacement::Centered,{});
    Require(made.HasValue(),"actual finite carrier thickens: "+made.FirstSummaryJa());
    const auto step=kernel::OutputStep({made.Value().handle});
    Require(!step.HasValue()&&step.Diagnostics().front().code=="EXP-P003",
        "known nonsmooth offset is explicitly refused when STEP changes its native volume");
    const auto stl=kernel::OutputStl({made.Value().handle},.01);
    Require(stl.HasValue(),"actual carrier supports closed STL: "+stl.FirstSummaryJa());
    Require(before.Value()==kernel::CaptureOutputShape(handle).Value(),"all original trim curves and carrier geometry remain unchanged");
    kernel::ReleaseShape(made.Value().handle);kernel::ReleaseShape(handle);
}

KACHA_V2_TEST(thicken_quality, open_spline_panel_with_notch_and_diagonal_hole_has_analytic_prism_mass)
{
    NCollection_Array2<gp_Pnt> poles(1,3,1,3);
    for(int u=1;u<=3;++u)for(int v=1;v<=3;++v)poles.SetValue(u,v,gp_Pnt((u-1)*5.,(v-1)*10.,(u==3?.3:0)+(v==3?.6:0)));
    const occ::handle<Geom_Surface> bezier=new Geom_BezierSurface(poles);
    auto surface=GeomConvert::SurfaceToBSplineSurface(bezier);
    for(int i=1;i<9;++i){surface->InsertUKnot(i/9.,1,1e-12,false);surface->InsertVKnot(i/9.,1,1e-12,false);}
    const auto face=PerforatedFace(surface,1,1,true);
    BRepPrimAPI_MakePrism prism(face,gp_Vec(0,0,.5));Require(prism.IsDone(),"exact graph face extruded vertically");
    auto solid=TopoDS::Solid(prism.Shape());Require(BRepLib::OrientClosedSolid(solid),"graph wall oriented");
    Require(BRepCheck_Analyzer(solid).IsValid(),"closed native graph prism with two holes and notch");
    double a=0,u=0,v=0,uu=0,vv=0;
    const auto rectangle=[&](double x0,double x1,double y0,double y1,int sign) {
        a+=sign*(x1-x0)*(y1-y0);u+=sign*(x1*x1-x0*x0)*.5*(y1-y0);v+=sign*(y1*y1-y0*y0)*.5*(x1-x0);
        uu+=sign*(x1*x1*x1-x0*x0*x0)/3*(y1-y0);vv+=sign*(y1*y1*y1-y0*y0*y0)/3*(x1-x0);
    };
    rectangle(0,1,0,1,1);rectangle(.6,.8,.7,1,-1);rectangle(.1,.25,1./6,.4,-1);
    const double triangle=(.55-.35)*(.5-.1)*.5;
    a-=triangle;u-=triangle*(.35+.55+.55)/3;v-=triangle*(.1+.1+.5)/3;
    uu-=triangle*(.35*.35+.55*.55+.55*.55+.35*.55+.35*.55+.55*.55)/6;
    vv-=triangle*(.1*.1+.1*.1+.5*.5+.1*.1+.1*.5+.1*.5)/6;
    const double expected=200*a*.5;const gp_Pnt center(10*u/a,20*v/a,(.3*uu+.6*vv)/a+.25);
    for(bool reverse:{false,true}) {
        GProp_GProps result;kernel::detail::VolumeProperties(solid,result,true);
        test::RequireNear(result.Mass(),reverse?-expected:expected,expected*1e-8,"native trimmed spline analytic prism volume");
        test::RequireNear(result.CentreOfMass().Distance(center),0,1e-8,"native trimmed spline polynomial analytic centroid");solid.Reverse();
    }
}

#else
KACHA_V2_TEST(thicken_quality, missing_kernel_is_reported)
{
    Require(!kernel::ThickenSurface(0,.8,fabrication::ThicknessPlacement::Inside,{}).HasValue(),"missing kernel refused");
}
#endif
KACHA_V2_TEST_MAIN("kernel_thicken_quality_tests")
