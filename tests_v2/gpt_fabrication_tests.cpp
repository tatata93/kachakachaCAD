#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctThicken.h"
#include "kachakacha/kernel/OcctSurfaceExtrude.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include "kachakacha/fabrication/GptApproximation.h"
#include "kachakacha/kernel/OcctGptFabrication.h"
#include "kachakacha/kernel/OcctGptSurface.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctExtrude.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include "kachakacha/geometry/ArcBuilders.h"
using namespace kachakacha::v2;
using test::Require;
using test::RequireNear;
namespace {
fabrication::GptApproxSource Source(double doubleCurvature)
{
    fabrication::GptApproxSource source;
    const auto p=[&](int x,int y) { return geometry::Vector3{double(x),double(y),.03*x*x+doubleCurvature*y*y}; };
    for (int y=-10;y<=10;++y) { for (int x=-10;x<=10;++x) { source.samples.push_back(p(x,y)); } }
    for (int x=-10;x<10;++x) { source.boundary.push_back(p(x,-10)); }
    for (int y=-10;y<10;++y) { source.boundary.push_back(p(10,y)); }
    for (int x=10;x>-10;--x) { source.boundary.push_back(p(x,10)); }
    for (int y=10;y>-10;--y) { source.boundary.push_back(p(-10,y)); }
    return source;
}
}
KACHA_V2_TEST(gpt_fabrication, cylinder_is_one_piece_and_unfolding_preserves_lengths)
{
    const auto made=fabrication::ApproximateGpt(Source(0),{.1,.5,12,2});
    Require(made.HasValue(),made.FirstMessageJa());
    Require(made.Value().reached && made.Value().panels.size()==1,"one-axis curved panel stays one piece");
    const auto& panel=made.Value().panels.front();
    for (double progress:{0.,.3,1.}) {
        const auto loop=fabrication::GptPanelLoop(panel,panel.pattern.outline,progress);
        for (std::size_t i=1;i<loop.size();++i) {
            const auto a=panel.pattern.outline[i-1], b=panel.pattern.outline[i%panel.pattern.outline.size()];
            RequireNear((loop[i]-loop[i-1]).Length(),std::hypot(b.u-a.u,b.v-a.v),1e-7,"flat pattern edge length is invariant");
        }
        Require(!fabrication::GptPanelMesh(panel,progress).triangles.empty(),"actual panel geometry rendered");
    }
}
KACHA_V2_TEST(gpt_fabrication, double_curvature_is_split_and_limits_are_honest)
{
    const auto made=fabrication::ApproximateGpt(Source(.025),{.3,.5,12,2});
    Require(made.HasValue(),made.FirstMessageJa());
    Require(made.Value().reached && made.Value().panels.size()>1,"few developable pieces approximate double curvature");
    Require(made.Value().maximumMm<=.3 && made.Value().rmsMm<=made.Value().maximumMm
        && made.Value().seamGapMm<=.3,"measured max, RMS and mating gap");
    const auto limited=fabrication::ApproximateGpt(Source(.025),{.001,.5,1,2});
    Require(limited.HasValue() && !limited.Value().reached && limited.Value().panels.size()==1,"cannot silently exceed panel limit or claim success");
}
KACHA_V2_TEST(gpt_fabrication, invalid_inputs_and_holes_are_not_ignored)
{
    Require(!fabrication::ApproximateGpt(Source(0),{0,.5,12,2}).HasValue(),"positive tolerance required");
    auto invalid=Source(0); invalid.boundary[0].x=std::numeric_limits<double>::quiet_NaN();
    Require(!fabrication::ApproximateGpt(invalid,{}).HasValue(),"non-finite boundary rejected");
    auto source=Source(0);
    source.holes={{{-2,-2,.12},{2,-2,.12},{2,2,.12},{-2,2,.12}}};
    const auto made=fabrication::ApproximateGpt(source,{.5,.5,1,2});
    Require(made.HasValue() && made.Value().panels.front().pattern.openings.size()==1,"retain opening in pattern");
}
KACHA_V2_TEST(gpt_fabrication, adaptive_spacing_refines_local_bend_and_respects_limits)
{
    fabrication::GptApproxSource source;
    const auto point=[](int x,int y) {
        const double t=std::max(0.0,y*.5-5.0);
        return geometry::Vector3{x*.5,y*.5,.003*x*x+.08*t*t};
    };
    for (int y=-40;y<=40;++y) { for (int x=-20;x<=20;++x) { source.samples.push_back(point(x,y)); } }
    for (int x=-20;x<20;++x) { source.boundary.push_back(point(x,-40)); }
    for (int y=-40;y<40;++y) { source.boundary.push_back(point(20,y)); }
    for (int x=20;x>-20;--x) { source.boundary.push_back(point(x,40)); }
    for (int y=40;y>-40;--y) { source.boundary.push_back(point(-20,y)); }
    fabrication::GptApproxOptions options{.0001,.5,6,0};
    const auto uniform=fabrication::ApproximateGpt(source,options);
    options.adaptiveSpacing=true;
    const auto adaptive=fabrication::ApproximateGpt(source,options);
    Require(uniform.HasValue() && adaptive.HasValue(),"both spacing modes create valid patterns");
    const auto& result=adaptive.Value();
    Require(!result.reached && result.panels.size()<=6,"cap and unmet tolerance remain honest");
    double smallest=1,largest=0;
    for (std::size_t i=1;i<result.railParameters.size();++i) {
        const double width=result.railParameters[i]-result.railParameters[i-1];
        smallest=std::min(smallest,width); largest=std::max(largest,width);
    }
    Require(largest>smallest*1.5,"adaptive spacing is not uniform");
    Require(result.maximumMm<uniform.Value().maximumMm,"local bend error improves with same budget");
    options.minimumWidthMm=100;
    Require(!fabrication::ApproximateGpt(source,options).HasValue(),"minimum width cannot be silently violated");
}
#ifdef KACHACAD_V2_WITH_OCCT
KACHA_V2_TEST(gpt_fabrication, brep_non_rectangular_boundary_and_pattern)
{
    app::GptSurfaceRequest request;
    const std::vector<geometry::Vector3> points{{0,0,0},{30,0,0},{24,20,0},{0,15,0}};
    app::GptSurfaceCurve curve;
    for (std::size_t i=0;i<points.size();++i) { curve.segments.push_back(geometry::CurveSegment::MakeLine(points[i],points[(i+1)%points.size()]).Value()); }
    curve.role=app::kGptBoundaryRole; curve.closed=true; request.curves.push_back(curve);
    const auto surface=kernel::BuildGptSurface(request,{});
    Require(surface.HasValue(),surface.FirstMessageJa());
    domain::CreateFabricationModelDefinition definition; definition.method=2; definition.materialThickness.value=.2;
    definition.parts.push_back({}); definition.targetMaxDeviation.value=.1; definition.minimumPartWidthMm=.5;
    const auto made=kernel::BuildGptFabrication(definition,{{{},surface.Value().handle}});
    Require(made.HasValue(),made.FirstMessageJa());
    Require(made.Value().panels.size()==1 && made.Value().reachedTolerance,"flat non-rectangular face accepted");
    Require(made.Value().summaryJa.find("分割不要")!=std::string::npos,"one panel reports no split needed");
    double twiceArea=0; const auto& loop=made.Value().panels.front().outline;
    for (std::size_t i=0;i<loop.size();++i) { const auto a=loop[i],b=loop[(i+1)%loop.size()]; twiceArea+=a.u*b.v-a.v*b.u; }
    RequireNear(std::abs(twiceArea)*.5,480.,.01,"trapezoid outline not changed into a bounding rectangle");
    kernel::ReleaseShape(surface.Value().handle);
}
KACHA_V2_TEST(gpt_fabrication, owners_head_becomes_few_manufacturable_panels)
{
    const auto Line=[](geometry::Vector3 a,geometry::Vector3 b){return geometry::CurveSegment::MakeLine(a,b).Value();};
    const auto arc = [](geometry::Vector3 a,geometry::Vector3 b,geometry::Vector3 c) {
        return geometry::ArcThroughThreePoints(a,b,c).Value();
    };
    app::GptSurfaceRequest request;
    request.curves = {{{arc({-3.5,1.936491673,0},{0,2.5,0},{3.5,1.936491673,0}),
        arc({-3.5,1.936491673,0},{-4.581139,1.224745,0},{-5,0,0}),
        arc({5,0,0},{4.581139,1.224745,0},{3.5,1.936491673,0}),
        Line({5,0,0},{3,0,3}),Line({-5,0,0},{-3,0,3}),
        Line({3,0,3},{0,0,3.5}),Line({0,0,3.5},{-3,0,3})}, "outer",true,app::kGptBoundaryRole},
        {{arc({0,0,3.5},{0,2.105897,2.361355},{0,2.5,0})},"rib",false,app::kGptInteriorRole}};
    const auto surface=kernel::BuildGptSurface(request,{});
    Require(surface.HasValue(),surface.FirstMessageJa());
    domain::CreateFabricationModelDefinition definition; definition.method=2; definition.materialThickness.value=.2;
    definition.parts.push_back({}); definition.targetMaxDeviation.value=.25;
    definition.minimumPartWidthMm=.2; definition.maximumPartCount=12;
    const auto made=kernel::BuildGptFabrication(definition,{{{},surface.Value().handle}});
    Require(made.HasValue(),made.FirstMessageJa());
    Require(made.Value().reachedTolerance,made.Value().summaryJa);
    Require(made.Value().maximumSeamGapMm<=.25,"mating gap within requested tolerance");
    Require(made.Value().panels.size()<=12 && made.Value().maximumDeviationMm<=.25,"few pieces within requested deviation");
    for (const auto& panel:made.Value().gptPanels) {
        Require(!fabrication::GptPanelMesh(panel,1).triangles.empty(),"whole part has a visible shape");
    }
    definition.adaptiveSpacing=true;
    const auto adaptive=kernel::BuildGptFabrication(definition,{{{},surface.Value().handle}});
    Require(adaptive.HasValue(),adaptive.FirstMessageJa());
    Require(adaptive.Value().reachedTolerance,adaptive.Value().summaryJa);
    Require(adaptive.Value().panels.size()<=12,"adaptive head respects same total panel budget");
    kernel::ReleaseShape(surface.Value().handle);
}
#endif
KACHA_V2_TEST(gpt_fabrication, generated_boundary_follows_profile_breaks)
{
    fabrication::GptApproxPanel panel; panel.u={1,0,0}; panel.v={0,1,0}; panel.normal={0,0,1};
    panel.profile={{0,0},{1,0},{1,1}}; panel.lengths={0,1,2};
    const std::vector<geometry::Point2> rectangle{{0,0},{2,0},{2,1},{0,1}};
    for(const double progress:{0.0,.37,1.0}) {
        const auto loop=fabrication::GptPanelLoop(panel,rectangle,progress);
        Require(loop.size()==7,"both long edges include the bend");
        double length=0;
        for(std::size_t i=1;i<loop.size();++i) { length+=geometry::Distance(loop[i-1],loop[i]); }
        RequireNear(length,6.,1e-8,"closed boundary preserves its actual length through bending");
    }
}
#ifdef KACHACAD_V2_WITH_OCCT
KACHA_V2_TEST(gpt_fabrication, generated_panel_faces_preserve_holes_and_bend)
{
    fabrication::GptApproxPanel panel; panel.u={1,0,0}; panel.v={0,1,0}; panel.normal={0,0,1};
    panel.profile={{0,0},{1,0},{1,1}}; panel.lengths={0,1,2};
    panel.pattern.outline={{0,0},{2,0},{2,2},{0,2}};
    panel.pattern.openings={{{.3,.5},{.7,.5},{.7,1.5},{.3,1.5}}};
    for(const double progress:{0.0,.37,1.0}) {
        app::GptSurfaceRequest request; request.panelPatches=true;
        for(const auto& patch:fabrication::GptPanelPatches(panel,progress)) {
            app::GptSurfaceCurve curve;
            for(std::size_t i=0;i<patch.size();++i) {
                const auto edge=geometry::CurveSegment::MakeLine(patch[i],patch[(i+1)%patch.size()]);
                if(edge.HasValue()) { curve.segments.push_back(edge.Value()); }
            }
            request.curves.push_back(curve);
        }
        const auto made=kernel::BuildGptSurface(request,{});
        Require(made.HasValue(),made.FirstMessageJa());
        RequireNear(made.Value().areaMm2,3.6,1e-6,"hole area and bend preserve surface area");
        const auto mesh=kernel::BuildShapeMesh(made.Value().handle,0.0,0);
        Require(mesh.HasValue(),mesh.FirstMessageJa());
        Require(mesh.Value().faceCount==(progress==0?1:2),"sampling stations do not split coplanar material; real bend remains");
        const auto hole=kernel::DistanceToShapeSurface(made.Value().handle,panel.Point({.5,1},progress));
        Require(hole.HasValue() && hole.Value()>.19,"opening is not capped");
        const auto onFace=kernel::DistanceToShapeSurface(made.Value().handle,panel.Point({1.5,1},progress));
        Require(onFace.HasValue() && onFace.Value()<1e-6,"bent point lies on generated face");
        kernel::ReleaseShape(made.Value().handle);
    }
}
#endif

#ifdef KACHACAD_V2_WITH_OCCT
namespace {
app::GptSurfaceRequest PatchRequest(const fabrication::GptApproxPanel& panel,double progress){
    app::GptSurfaceRequest request;request.panelPatches=true;
    for(const auto& patch:fabrication::GptPanelPatches(panel,progress)){
        app::GptSurfaceCurve curve;for(std::size_t i=0;i<patch.size();++i)
            curve.segments.push_back(geometry::CurveSegment::MakeLine(patch[i],patch[(i+1)%patch.size()]).Value());
        request.curves.push_back(curve);
    }
    return request;
}
fabrication::GptApproxPanel DensePanel(){
    fabrication::GptApproxPanel panel;panel.u={1,0,0};panel.v={0,1,0};panel.normal={0,0,1};
    for(int i=0;i<=24;++i){const double x=i*.5,z=i<=8?0:i<16?(i-8)*.25:2;
        panel.profile.push_back({x,z});if(i==0)panel.lengths.push_back(0);
        else {const auto a=panel.profile[i-1];panel.lengths.push_back(panel.lengths.back()+std::hypot(x-a.u,z-a.v));}}
    panel.pattern.outline={{0,0},{panel.lengths.back(),0},{panel.lengths.back(),8},{0,8}};
    panel.pattern.openings={{{1,1},{2,1},{2,3},{1,3}},{{3.5,4},{5.5,4},{5.5,6},{3.5,6}}};return panel;
}
}
KACHA_V2_TEST(gpt_fabrication, patch_compaction_preserves_dense_holes_folds_and_every_station){
    const auto panel=DensePanel();
    for(double progress:{0.,.37,1.}){
        const auto request=PatchRequest(panel,progress);Require(request.curves.size()>20,"fixture contains unnecessary sampling patches");
        const auto made=kernel::BuildGptSurface(request,{});Require(made.HasValue(),made.FirstMessageJa());
        const auto mesh=kernel::BuildShapeMesh(made.Value().handle,0,0);Require(mesh.HasValue(),mesh.FirstMessageJa());
        Require(mesh.Value().faceCount==(progress==0?1:3),"only physical planar pieces remain");
        RequireNear(made.Value().areaMm2,panel.lengths.back()*8-6,1e-6,"two windows and complete material area are retained");
        for(const auto& patch:fabrication::GptPanelPatches(panel,progress)){
            geometry::Vector3 center{};for(const auto& p:patch){center=center+p;const auto d=kernel::DistanceToShapeSurface(made.Value().handle,p);Require(d.HasValue()&&d.Value()<1e-6,"every station stays on exact surface");}
            const auto d=kernel::DistanceToShapeSurface(made.Value().handle,center*.25);Require(d.HasValue()&&d.Value()<1e-6,"every patch interior stays on exact surface");
        }
        for(const auto hole:{geometry::Point2{1.5,2},geometry::Point2{4.5,5}}){const auto d=kernel::DistanceToShapeSurface(made.Value().handle,panel.Point(hole,progress));Require(d.HasValue()&&d.Value()>.45,"windows including one across a fold stay open");}
        const auto edges=kernel::OutputEdges(made.Value().handle,1e-6);Require(edges.HasValue(),edges.FirstMessageJa());
        Require(edges.Value().size()<=24,"sampling stations are not exported as artificial boundaries");
        const auto saved=kernel::CaptureOutputShape(made.Value().handle);Require(saved.HasValue(),saved.FirstMessageJa());
        const auto restored=kernel::RestoreOutputShape(saved.Value());Require(restored.HasValue(),restored.FirstMessageJa());
        Require(kernel::BuildShapeMesh(restored.Value(),0,0).Value().faceCount==mesh.Value().faceCount,"native save roundtrip keeps topology");
        kernel::ReleaseShape(restored.Value());kernel::ReleaseShape(made.Value().handle);
    }
}
KACHA_V2_TEST(gpt_fabrication, compact_patch_surfaces_remain_usable_for_extrusion_and_thickness){
    fabrication::GptApproxPanel panel;panel.u={1,0,0};panel.v={0,1,0};panel.normal={0,0,1};
    panel.profile={{0,0},{4,0},{4,4}};panel.lengths={0,4,8};panel.pattern.outline={{0,0},{8,0},{8,5},{0,5}};
    panel.pattern.openings={{{1,1},{2,1},{2,2},{1,2}}};
    for(int pose=0;pose<2;++pose){
    if(pose){panel.origin={12,-9,4};panel.u={0,1,0};panel.v={0,0,-1};panel.normal={-1,0,0};}
    for(double progress:{0.,.37,1.}){
        const auto made=kernel::BuildGptSurface(PatchRequest(panel,progress),{});Require(made.HasValue(),made.FirstMessageJa());
        const auto original=kernel::CaptureOutputShape(made.Value().handle);Require(original.HasValue(),original.FirstMessageJa());
        for(auto placement:{fabrication::ThicknessPlacement::Outside,fabrication::ThicknessPlacement::Inside,fabrication::ThicknessPlacement::Centered}){
            const auto solid=kernel::ThickenSurface(made.Value().handle,.2,placement,{});Require(solid.HasValue(),std::string("progress=")+std::to_string(progress)+" placement="+std::to_string(int(placement))+" "+solid.FirstMessageJa());
            const auto check=kernel::CheckSolidForExport(solid.Value().handle,1e-6);Require(check.HasValue()&&check.Value().Ok(),"thickness produces a valid closed material");
            Require(solid.Value().volumeMm3>7.5&&solid.Value().volumeMm3<8.1,"bending preserves sheet volume within the analytic corner envelope");
            if(progress==0)RequireNear(solid.Value().volumeMm3,7.8,1e-6,"flat material has analytic volume");
            const double lower=placement==fabrication::ThicknessPlacement::Outside?0:placement==fabrication::ThicknessPlacement::Inside?-.2:-.1;
            for(const double z:{lower,lower+.2}){
                const auto skin=kernel::DistanceToShapeSurface(solid.Value().handle,panel.Point({.5,3},progress)+panel.normal*z);
                Require(skin.HasValue()&&skin.Value()<1e-6,"placement preserves the specified source and opposite skins");
            }
            const auto hole=kernel::DistanceToShapeSurface(solid.Value().handle,panel.Point({1.5,1.5},progress));Require(hole.HasValue()&&hole.Value()>.3,"thickened window is not capped");
            kernel::ReleaseShape(solid.Value().handle);
        }
        if(progress<1&&pose==0){const auto extruded=kernel::BuildSurfaceExtrude(made.Value().handle,{0,0,1},0,.2,{});
            Require(extruded.HasValue(),extruded.FirstMessageJa());Require(extruded.Value().parts.size()==1,"connected profile extrudes as one material");
            const double expected=(19+20*std::cos(progress*std::acos(-1.)*.5))*.2;
            RequireNear(extruded.Value().totalVolumeMm3,expected,1e-6,"surface extrusion has analytic projected-area volume");
            for(const auto& part:extruded.Value().parts)kernel::ReleaseShape(part.handle);}
        Require(kernel::CaptureOutputShape(made.Value().handle).Value()==original.Value(),"downstream operations never modify the source skin");
        kernel::ReleaseShape(made.Value().handle);
    }
    }
}
KACHA_V2_TEST(gpt_fabrication, separate_patch_islands_are_not_joined_or_lost){
    fabrication::GptApproxPanel panel;panel.u={1,0,0};panel.v={0,1,0};panel.normal={0,0,1};
    panel.profile={{0,0},{2,0}};panel.lengths={0,2};panel.pattern.outline={{0,0},{2,0},{2,2},{0,2}};
    auto request=PatchRequest(panel,0);panel.origin={5,0,0};const auto other=PatchRequest(panel,0);
    request.curves.insert(request.curves.end(),other.curves.begin(),other.curves.end());
    const auto made=kernel::BuildGptSurface(request,{});Require(made.HasValue(),made.FirstMessageJa());
    RequireNear(made.Value().areaMm2,8,1e-6,"both islands remain");
    const auto extruded=kernel::BuildSurfaceExtrude(made.Value().handle,{0,0,1},0,.2,{});
    Require(extruded.HasValue(),extruded.FirstMessageJa());Require(extruded.Value().parts.size()==2,"separate material stays separate");
    for(const auto& part:extruded.Value().parts)kernel::ReleaseShape(part.handle);
    for(auto placement:{fabrication::ThicknessPlacement::Outside,fabrication::ThicknessPlacement::Inside,fabrication::ThicknessPlacement::Centered}){
        const auto solid=kernel::ThickenSurface(made.Value().handle,.2,placement,{});Require(solid.HasValue(),solid.FirstMessageJa());
        RequireNear(solid.Value().volumeMm3,1.6,1e-6,"thickness keeps both disconnected islands");
        kernel::ReleaseShape(solid.Value().handle);
    }
    kernel::ReleaseShape(made.Value().handle);
}
KACHA_V2_TEST(gpt_fabrication, patch_compaction_is_independent_of_pose_and_scale){
    for(const double scale:{.01,1.,100.}){
        auto panel=DensePanel();panel.origin={123,-57,19};panel.u={0,1,0};panel.v={0,0,1};panel.normal={1,0,0};
        for(auto& p:panel.profile){p.u*=scale;p.v*=scale;}for(auto& length:panel.lengths)length*=scale;
        for(auto& p:panel.pattern.outline){p.u*=scale;p.v*=scale;}
        for(auto& hole:panel.pattern.openings)for(auto& p:hole){p.u*=scale;p.v*=scale;}
        for(double progress:{0.,1.}){
            const auto made=kernel::BuildGptSurface(PatchRequest(panel,progress),{});Require(made.HasValue(),made.FirstMessageJa());
            const auto mesh=kernel::BuildShapeMesh(made.Value().handle,0,0);Require(mesh.HasValue(),mesh.FirstMessageJa());
            Require(mesh.Value().faceCount==(progress==0?1:3),"native topology is independent of drawing axes and model size");
            RequireNear(made.Value().areaMm2,panel.lengths.back()*8*scale-6*scale*scale,1e-7*std::max(1.,scale*scale),"scaled material and both windows keep their area");
            const auto hole=kernel::DistanceToShapeSurface(made.Value().handle,panel.Point({1.5*scale,2*scale},progress));
            Require(hole.HasValue()&&hole.Value()>.45*scale,"posed and scaled window stays open");kernel::ReleaseShape(made.Value().handle);
        }
    }
}
#endif

KACHA_V2_TEST_MAIN("gpt_fabrication_tests")
