#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctThicken.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/kernel/OcctMassProperties.h"
#include <cmath>
#include <cstring>
#include <cstdint>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <Geom_Surface.hxx>
#include <TopoDS.hxx>
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
#include <BRepLib.hxx>
#include <GProp_GProps.hxx>
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
#else
KACHA_V2_TEST(thicken_quality, missing_kernel_is_reported)
{
    Require(!kernel::ThickenSurface(0,.8,fabrication::ThicknessPlacement::Inside,{}).HasValue(),"missing kernel refused");
}
#endif
KACHA_V2_TEST_MAIN("kernel_thicken_quality_tests")
