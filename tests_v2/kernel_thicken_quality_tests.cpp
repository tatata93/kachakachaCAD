#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctThicken.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include <cmath>
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
    const auto solid=kernel::ThickenSurface(surface,.8,fabrication::ThicknessPlacement::Inside,{});
    Require(!solid.HasValue(),"self-intersecting simple offset must not become a part");
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
#else
KACHA_V2_TEST(thicken_quality, missing_kernel_is_reported)
{
    Require(!kernel::ThickenSurface(0,.8,fabrication::ThicknessPlacement::Inside,{}).HasValue(),"missing kernel refused");
}
#endif
KACHA_V2_TEST_MAIN("kernel_thicken_quality_tests")
