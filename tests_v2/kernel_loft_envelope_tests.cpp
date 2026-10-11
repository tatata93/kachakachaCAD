#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include <array>
#include <algorithm>
#include <iostream>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Trsf.hxx>
#include <gp_Ax1.hxx>
#endif
using namespace kachakacha::v2;
using test::Require;
#ifdef KACHACAD_V2_WITH_OCCT
KACHA_V2_TEST(loft_envelope, exact_sections_and_rigid_motion_are_preserved)
{
    modeling::GuideSurfaceRequest input;input.method=modeling::GuideSurfaceMethod::LoftSections;
    input.keepSectionOrder=true;
    for(const auto [x,r,h]:std::array<std::array<double,3>,5>{{{0,2,3},{2,4,6},{8,7,10},{20,8,12},{70,8,12}}}) {
        modeling::GuideChain chain;chain.role=modeling::ChainRole::Section;
        chain.index=static_cast<int>(input.chains.size())+1;
        chain.segments.push_back(geometry::CurveSegment::MakeCubicBezier(
            {{x,-r,h*.7},{x,-r,h},{x,-r*.55,h},{x,0,h}}).Value());
        input.chains.push_back(chain);
    }
    gp_Trsf motion;motion.SetRotation(gp_Ax1(gp_Pnt(3,-2,5),gp_Dir(1,2,3)),.83);
    motion.SetTranslationPart(gp_Vec(17,-28,9));
    modeling::GuideSurfaceRequest moved=input;
    for(auto& chain:moved.chains) {
        auto points=chain.segments[0].ControlPoints();
        for(auto& p:points){gp_Pnt q(p.x,p.y,p.z);q.Transform(motion);p={q.X(),q.Y(),q.Z()};}
        chain.segments[0]=geometry::CurveSegment::MakeCubicBezier(points).Value();
    }
    const auto build=[](const modeling::GuideSurfaceRequest& request){
        const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,{});
        Require(analysis.HasValue(),"section analysis succeeds");
        const auto result=kernel::BuildGuideSurface(request,analysis.Value(),{});
        Require(result.HasValue(),"matched loft builds");
        TopoDS_Shape shape;Require(kernel::LookupShape(result.Value().handle,shape),"shape available");
        TopExp_Explorer faces(shape,TopAbs_FACE);Require(faces.More(),"face exists");
        return std::pair{result.Value().handle,BRep_Tool::Surface(TopoDS::Face(faces.Current()))};
    };
    const auto [originalHandle,original]=build(input);
    const auto [movedHandle,transformed]=build(moved);
    auto reversedInput=input;
    for(auto& chain:reversedInput.chains) {
        auto points=chain.segments[0].ControlPoints();std::reverse(points.begin(),points.end());
        chain.segments[0]=geometry::CurveSegment::MakeCubicBezier(points).Value();
    }
    const auto [reversedHandle,reversedSurface]=build(reversedInput);
    for(const auto& chain:input.chains)for(int i=0;i<=20;++i) {
        const auto p=chain.segments[0].Evaluate(i/20.0);
        GeomAPI_ProjectPointOnSurf projected(gp_Pnt(p.x,p.y,p.z),original);
        Require(projected.NbPoints()>0&&projected.LowerDistance()<1e-6,"loft passes through every input section");
    }
    double u0,u1,v0,v1;original->Bounds(u0,u1,v0,v1);
    for(int i=0;i<=12;++i)for(int j=0;j<=60;++j) {
        auto p=original->Value(u0+(u1-u0)*i/12.0,v0+(v1-v0)*j/60.0);
        GeomAPI_ProjectPointOnSurf reversedProjection(p,reversedSurface);
        Require(reversedProjection.NbPoints()>0&&reversedProjection.LowerDistance()<1e-6,
            "reversing all section curves preserves the surface and only changes its normal");
        p.Transform(motion);
        GeomAPI_ProjectPointOnSurf projected(p,transformed);
        Require(projected.NbPoints()>0&&projected.LowerDistance()<1e-6,"rotation and translation preserve all sampled shape points");
    }
    kernel::ReleaseShape(originalHandle);kernel::ReleaseShape(movedHandle);kernel::ReleaseShape(reversedHandle);
}
KACHA_V2_TEST(loft_envelope, long_nose_sections_do_not_create_a_flaring_tail)
{
    const std::array<std::array<double,4>,13> stations{{
        {0,1.6,3.8,4.5},{3,3.1,4.1,5},{7,4.8,4.7,6},{13,6.8,6.1,8},
        {21,8.5,8.4,11.5},{31,9.8,11.7,16.5},{42,10.7,15,21.2},
        {54,11.15,17.3,23.7},{64,11.2,17.8,24},{76,11.2,17.8,24},
        {100,11.2,17.8,24},{140,11.2,17.8,24},{27100.0/150,11.2,17.8,24}}};
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::LoftSections;
    request.keepSectionOrder=true;
    for(const auto& row:stations) {
        const auto [x,r,shoulder,top]=row;
        modeling::GuideChain chain;chain.role=modeling::ChainRole::Section;
        chain.index=static_cast<int>(request.chains.size())+1;
        chain.segments.push_back(geometry::CurveSegment::MakeCubicBezier(
            {{x,-r,shoulder},{x,-r,top},{x,-.55*r,top},{x,0,top}}).Value());
        request.chains.push_back(chain);
    }
    const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,{});
    if(!analysis.HasValue())std::cout<<analysis.FirstMessageJa()<<'\n';
    Require(analysis.HasValue(),"ordered compatible sections accepted");
    const auto built=kernel::BuildGuideSurface(request,analysis.Value(),{});
    Require(built.HasValue(),"roof loft builds");
    const auto mesh=kernel::BuildShapeMesh(built.Value().handle,.01);
    Require(mesh.HasValue(),"roof mesh available");
    double minY=0,maxZ=0,minX=0,maxX=0;
    for(const auto& tri:mesh.Value().triangles)for(const auto& p:tri.points) {
        minY=std::min(minY,p.y);maxZ=std::max(maxZ,p.z);
        minX=std::min(minX,p.x);maxX=std::max(maxX,p.x);
    }
    std::cout<<"roof envelope min_y="<<minY<<" max_z="<<maxZ<<" x="<<minX<<","<<maxX<<'\n';
    Require(minY>=-11.25&&maxZ<=24.05&&minX>=-.05&&maxX<=27100.0/150+.05,
        "flat constant body sections must not balloon beyond the reference envelope");
    kernel::ReleaseShape(built.Value().handle);
}
KACHA_V2_TEST(loft_envelope, eccentric_tank_keeps_constant_width_between_stations)
{
    const std::array<std::array<double,4>,9> rows{{
        {-58,1.6,1.6,23},{-56,7.5,7,23},{-52,12.6,10.8,23},{-45,14,11.8,22.8},
        {-33,14,13,21.6},{-18,14,13.5,21.1},{0,14,13.5,21.1},{18,14,13.5,21.1},{33,14,13,21.6}}};
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::LoftSections;
    request.keepSectionOrder=true;constexpr double k=.5522847498307936;
    for(const auto& row:rows) {
        const auto [x,ry,rz,cz]=row;modeling::GuideChain chain;
        chain.role=modeling::ChainRole::Section;chain.index=static_cast<int>(request.chains.size())+1;
        chain.segments.push_back(geometry::CurveSegment::MakeCubicBezier(
            {{x,0,cz-rz},{x,k*ry,cz-rz},{x,ry,cz-k*rz},{x,ry,cz}}).Value());
        request.chains.push_back(chain);
    }
    const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,{});
    Require(analysis.HasValue(),"Eccentric quarter sections accepted");
    const auto built=kernel::BuildGuideSurface(request,analysis.Value(),{});
    Require(built.HasValue(),"Eccentric tank guide built");
    const auto mesh=kernel::BuildShapeMesh(built.Value().handle,.005);
    Require(mesh.HasValue(),"Tank mesh available");double maximumY=0;
    for(const auto& t:mesh.Value().triangles)for(const auto& p:t.points)maximumY=std::max(maximumY,p.y);
    Require(maximumY<=14.001,"Changing tank centre height must not inflate constant half-width");
    kernel::ReleaseShape(built.Value().handle);
}
#else
KACHA_V2_TEST(loft_envelope, requires_occt)
{
    Require(true,"OCCT integration only");
}
#endif
KACHA_V2_TEST_MAIN("kernel_loft_envelope_tests")
