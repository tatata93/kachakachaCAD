// Offsetting a folded guide must retain its material boundaries and signed
// normal distance, so the same guide can be used as a sheet's neutral surface.
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctThicken.h"
#include "kachakacha/kernel/OcctFabricationRegion.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepLib.hxx>
#include <BRep_Tool.hxx>
#include <Geom2d_Line.hxx>
#include <Geom_BSplineSurface.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Vec.hxx>
#include <gp_Ax2d.hxx>
#include <gp_Dir2d.hxx>
#include <gp_Vec2d.hxx>
#include <gp_Trsf.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <array>
#include <cmath>
#include <vector>
using namespace kachakacha::v2;
using test::Require;
using test::RequireNear;
namespace {
auto FoldedFace(const std::vector<gp_Pnt>& profile={{0,0,0},{5,0,0},{8,0,3}},bool warped=false)
{
    const int count=static_cast<int>(profile.size());
    NCollection_Array2<gp_Pnt> poles(1,count,1,2);
    for(int i=1;i<=count;++i)for(int j=1;j<=2;++j)
        poles.SetValue(i,j,profile[i-1].Translated(gp_Vec(0,(j-1)*10,0)));
    if(warped)poles.SetValue(2,2,poles(2,2).Translated(gp_Vec(0,0,.25)));
    NCollection_Array1<double> u(1,count),v(1,2);
    NCollection_Array1<int> um(1,count),vm(1,2);
    for(int i=1;i<=count;++i){u(i)=double(i-1)/(count-1);um(i)=i==1||i==count?2:1;}
    v(1)=0;v(2)=1;vm(1)=vm(2)=2;
    const occ::handle<Geom_BSplineSurface> surface=new Geom_BSplineSurface(poles,u,v,um,vm,1,1);
    return BRepBuilderAPI_MakeFace(surface,1e-7).Face();
}
auto Offset(modeling::KernelShapeHandle source,double distance)
{
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::OffsetGuide;
    request.offsetDistanceMm=distance;modeling::GuideChain chain;
    chain.role=modeling::ChainRole::SourceSurface;chain.index=1;request.chains.push_back(chain);
    const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,{});
    Require(analysis.HasValue(),analysis.FirstSummaryJa());
    return kernel::BuildGuideSurface(request,analysis.Value(),{},source);
}
TopoDS_Wire UvWire(const occ::handle<Geom_Surface>& surface,const std::vector<gp_Pnt2d>& points)
{
    BRepBuilderAPI_MakeWire wire;
    for(std::size_t i=0;i<points.size();++i){
        const gp_Vec2d delta(points[i],points[(i+1)%points.size()]);
        const occ::handle<Geom2d_Line> line=new Geom2d_Line(gp_Ax2d(points[i],gp_Dir2d(delta)));
        BRepBuilderAPI_MakeEdge edge(line,surface,0,delta.Magnitude());
        Require(edge.IsDone(),"native boundary edge");wire.Add(edge.Edge());
    }
    Require(wire.IsDone(),"connected native boundary");return wire.Wire();
}
TopoDS_Face PerforatedFace(bool notch)
{
    const auto surface=BRep_Tool::Surface(FoldedFace());
    const std::vector<gp_Pnt2d> outer=notch
        ?std::vector<gp_Pnt2d>{{0,0},{1,0},{1,.4},{.8,.4},{.8,.6},{1,.6},{1,1},{0,1}}
        :std::vector<gp_Pnt2d>{{0,0},{1,0},{1,1},{0,1}};
    BRepBuilderAPI_MakeFace face(surface,UvWire(surface,outer),true);
    for(const auto& hole:std::vector<std::vector<gp_Pnt2d>>{
            {{.1,.15},{.1,.35},{.3,.35},{.3,.15}},
            {{.45,.55},{.45,.85},{.7,.85},{.7,.55}}})face.Add(UvWire(surface,hole));
    auto made=face.Face();Require(BRepLib::BuildCurves3d(made,1e-7),"boundary curves built");
    Require(BRepCheck_Analyzer(made).IsValid(),"valid perforated source");return made;
}
TopoDS_Face Placed(TopoDS_Face face,bool located)
{
    if(!located)return face;
    gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.7);
    transform.SetTranslationPart(gp_Vec(100,-200,300));
    // Keep a real native location instead of baking transformed poles into
    // the fixture; this catches dropping the source face's placement.
    return TopoDS::Face(BRepBuilderAPI_Transform(face,transform,false).Shape());
}
}
KACHA_V2_TEST(kernel_folded_offset, signed_miter_distance_and_source_preservation)
{
    for(const bool located:{false,true})for(const bool reverse:{false,true})for(const double distance:{-.05,.05}){
        auto face=Placed(FoldedFace(),located);if(reverse)face.Reverse();
        const auto source=kernel::StoreShape(face);const auto before=kernel::CaptureOutputShape(source);
        const auto moved=Offset(source,distance);
        Require(moved.HasValue(),moved.FirstSummaryJa()+" "+moved.FirstMessageJa());
        TopoDS_Shape native;Require(kernel::LookupShape(moved.Value().handle,native),"offset exists");
        Require(BRepCheck_Analyzer(native).IsValid(),"native offset is valid");
        TopExp_Explorer faces(native,TopAbs_FACE);Require(faces.More(),"offset has a face");
        const auto surface=BRep_Tool::Surface(TopoDS::Face(faces.Current()));faces.Next();
        Require(!faces.More(),"a folded sheet remains one guide face");
        const auto original=BRep_Tool::Surface(face);
        for(const double u:{.25,.5,.75}){
            const auto delta=gp_Vec(original->Value(u,.37),surface->Value(u,.37));
            for(const double side:u==.5?std::array{.25,.75}:std::array{u,u}){
                gp_Pnt p;gp_Vec du,dv;original->D1(side,.37,p,du,dv);
                auto normal=du.Crossed(dv).Normalized();if(reverse)normal.Reverse();
                RequireNear(delta.Dot(normal),distance,1e-7,"specified distance on both sides of each fold");
            }
        }
        Require(before.HasValue()&&before.Value()==kernel::CaptureOutputShape(source).Value(),"source unchanged");
        Require(kernel::OutputStep({moved.Value().handle}).HasValue(),"native offset exports to STEP");
        kernel::ReleaseShape(moved.Value().handle);kernel::ReleaseShape(source);
    }
}
KACHA_V2_TEST(kernel_folded_offset, multiple_holes_crossing_a_fold_and_edge_notches_survive)
{
    for(const bool notch:{false,true})for(const bool located:{false,true})
        for(const bool reverse:{false,true})for(const double distance:{-.05,.05}){
        auto face=Placed(PerforatedFace(notch),located);if(reverse)face.Reverse();
        const auto source=kernel::StoreShape(face);const auto before=kernel::CaptureOutputShape(source);
        const auto moved=Offset(source,distance);Require(moved.HasValue(),moved.FirstMessageJa());
        TopoDS_Shape shape;Require(kernel::LookupShape(moved.Value().handle,shape),"offset native shape");
        TopExp_Explorer faces(shape,TopAbs_FACE);Require(faces.More(),"offset face");const auto result=TopoDS::Face(faces.Current());
        int wires=0;for(TopExp_Explorer w(result,TopAbs_WIRE);w.More();w.Next())++wires;
        Require(wires==3,"both holes and outer boundary retained");
        for(const auto point:{gp_Pnt2d(.2,.25),gp_Pnt2d(.55,.7)})
            Require(BRepClass_FaceClassifier(result,point,1e-7).State()==TopAbs_OUT,"hole remains empty across fold");
        Require(BRepClass_FaceClassifier(result,gp_Pnt2d(.7,.45),1e-7).State()==TopAbs_IN,"unselected material retained");
        Require(BRepClass_FaceClassifier(result,gp_Pnt2d(.9,.5),1e-7).State()==(notch?TopAbs_OUT:TopAbs_IN),"edge notch retained");
        const auto solid=kernel::ThickenSurface(moved.Value().handle,.1,fabrication::ThicknessPlacement::Centered,{});
        Require(solid.HasValue(),solid.FirstMessageJa());
        Require(solid.Value().volumeMm3>0&&solid.Value().volumeMm3<9.25,"positive material volume excludes both holes");
        Require(kernel::OutputStep({solid.Value().handle}).HasValue(),"perforated folded part exports to STEP");
        Require(before.Value()==kernel::CaptureOutputShape(source).Value(),"native source curves and trims unchanged");
        kernel::ReleaseShape(solid.Value().handle);kernel::ReleaseShape(moved.Value().handle);kernel::ReleaseShape(source);
    }
}
KACHA_V2_TEST(kernel_folded_offset, collapsing_and_warped_folds_do_not_force_success)
{
    for(const bool warped:{false,true}){
        const auto face=warped?FoldedFace({{0,0,0},{5,0,0},{8,0,3}},true)
            :FoldedFace({{0,0,0},{0,0,4},{2,0,4},{2,0,0}});
        const auto source=kernel::StoreShape(face);const auto before=kernel::CaptureOutputShape(source);
        const auto moved=Offset(source,warped?.1:-1.1);
        Require(!moved.HasValue(),"unsupported warped or collapsed geometry must be refused");
        Require(before.Value()==kernel::CaptureOutputShape(source).Value(),"refusal preserves source");
        kernel::ReleaseShape(source);
    }
    const auto source=kernel::StoreShape(FoldedFace());const auto before=kernel::CaptureOutputShape(source);
    for(const double distance:{-1e300,1e300})
        Require(!Offset(source,distance).HasValue(),"overflowing facet arithmetic is refused before native construction");
    Require(before.Value()==kernel::CaptureOutputShape(source).Value(),"overflow refusal preserves source");
    kernel::ReleaseShape(source);
}
KACHA_V2_TEST(kernel_folded_offset, original_folded_material_boundary_does_not_exhaust_sampling)
{
    const auto source=kernel::StoreShape(PerforatedFace(false));
    const auto before=kernel::CaptureOutputShape(source);
    const double budget=geometry::GeometryTolerance{}.modelLinearMm*.1;
    const auto region=kernel::FabricationRegionOf(source,0,budget);
    Require(region.HasValue(),region.FirstMessageJa());
    Require(region.Value().loops.size()==3,"outer and both holes sampled without changing the tolerance");
    Require(region.Value().boundarySamplingDeviationMm<=budget,"original boundary sampling budget retained");
    Require(before.Value()==kernel::CaptureOutputShape(source).Value(),"source untouched");
    kernel::ReleaseShape(source);
}
#endif
KACHA_V2_TEST_MAIN("kernel_folded_offset_tests")
