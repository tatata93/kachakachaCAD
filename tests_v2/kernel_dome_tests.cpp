#include "kachakacha/base/TestHarness.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctDomePatch.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctFaceQuery.h"
#include "kachakacha/kernel/OcctFabricationRegion.h"
#include "kachakacha/kernel/OcctSurfaceRegion.h"
#include "kachakacha/kernel/OcctThicken.h"
#include "kachakacha/geometry/ArcBuilders.h"
#include "kachakacha/geometry/WireEdit.h"
#include <NCollection_Array2.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include "kachakacha/kernel/OcctShapeCache.h"
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp_Explorer.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepLib.hxx>
#include <Geom2d_BezierCurve.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <GCE2d_MakeSegment.hxx>
#include <algorithm>
#include <cmath>

using namespace kachakacha::v2;
using test::Require;
using test::RequireNear;
namespace {
occ::handle<Geom_BSplineSurface> Patch(bool equal = false, bool flat = false)
{
    NCollection_Array2<gp_Pnt> poles(1,4,1,4);
    for (int i=1;i<=4;++i) for (int j=1;j<=4;++j) {
        const double z = flat || j==1 || j==4 ? 0 : 6;
        poles.SetValue(i,j,gp_Pnt((i-1)*4,(j-1)*7,z*(equal ? 1 : (4-i)/3.0)));
    }
    TColStd_Array1OfReal knots(1,2); knots(1)=0; knots(2)=1;
    TColStd_Array1OfInteger mult(1,2); mult(1)=4; mult(2)=4;
    return new Geom_BSplineSurface(poles,knots,knots,mult,mult,3,3);
}

void Boundaries(const Geom_BSplineSurface& a, const Geom_BSplineSurface& b)
{
    for (int k=0;k<=100;++k) for (int side=0;side<4;++side) {
        const double t=k/100.0, u=side<2 ? t : side-2, v=side<2 ? side : t;
        RequireNear(a.Value(u,v).Distance(b.Value(u,v)),0,1.0e-10,"全境界を保つ");
    }
}

modeling::GuideSurfaceRequest Request(modeling::GuideSurfaceMethod method)
{
    using geometry::CurveSegment;
    modeling::GuideSurfaceRequest request;
    request.method=method; request.fourEdgeStyle=modeling::FourEdgeStyle::Dome;
    const std::vector<CurveSegment> edges={
        geometry::ArcThroughThreePoints({0,0,0},{0,10,6},{0,20,0}).Value(),
        CurveSegment::MakeLine({0,20,0},{12,20,0}).Value(),
        CurveSegment::MakeLine({12,20,0},{12,0,0}).Value(),
        CurveSegment::MakeLine({12,0,0},{0,0,0}).Value()};
    for (std::size_t i=0;i<edges.size();++i) {
        modeling::GuideChain chain; chain.role=modeling::ChainRole::BoundarySide;
        chain.index=static_cast<int>(i+1); chain.segments={edges[i]}; request.chains.push_back(chain);
    }
    return request;
}

auto Build(const modeling::GuideSurfaceRequest& request)
{
    geometry::GeometryTolerance tolerance; tolerance.modelLinearMm=1.0e-6;
    auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,tolerance);
    Require(analysis.HasValue(),"入力検査に通る: "+analysis.FirstSummaryJa());
    return kernel::BuildGuideSurface(request,analysis.Value(),tolerance);
}
}

KACHA_V2_TEST(dome, 断面から離れても丸みを広く伝え境界を保つ)
{
    const auto base=Patch(); const auto result=kernel::detail::DomePatch(base);
    Require(result.HasValue(),"ドームを作る"); Boundaries(*base,*result.Value());
    for (double u : {0.25,0.5,0.75,0.9}) {
        RequireNear(result.Value()->Value(u,0.5).Z(),4.5*(1-u*u),1e-10,"非線形に丸みを伝える");
        Require(result.Value()->Value(u,0.5).Z()>base->Value(u,0.5).Z(),"直線的に弱めない");
    }
}

KACHA_V2_TEST(fabrication_surface, matched_polyline_rails_keep_every_corresponding_vertex)
{
    const std::vector<geometry::Vector3> a{{0,0,0},{1,0,3},{2,0,4},{10,0,2},{20,0,0}};
    const std::vector<geometry::Vector3> b{{0,5,0},{.1,5,2},{1,5,3},{15,5,1},{20,5,0}};
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::RuledSections;
    request.keepSectionOrder=true;
    for(const auto* rail:{&a,&b}) {
        modeling::GuideChain chain;chain.role=modeling::ChainRole::Section;chain.index=static_cast<int>(request.chains.size()+1);
        for(std::size_t i=1;i<rail->size();++i)chain.segments.push_back(geometry::CurveSegment::MakeLine((*rail)[i-1],(*rail)[i]).Value());
        request.chains.push_back(chain);
    }
    const auto made=Build(request);Require(made.HasValue(),made.FirstMessageJa());
    const auto count=kernel::ShapeFaceCount(made.Value().handle);
    Require(count.HasValue() && count.Value()==1,"single editable source surface rather than first loft facet");
    const auto value=kernel::FabricationEvaluatorOf(made.Value().handle);Require(value.HasValue(),value.FirstMessageJa());
    for(std::size_t segment=0;segment+1<a.size();++segment)for(double f:{0.0,.25,.5,.75,1.0})for(double side:{0.0,.5,1.0}) {
        const auto p=a[segment]*(1-f)+a[segment+1]*f,q=b[segment]*(1-f)+b[segment+1]*f;
        const auto expected=p*(1-side)+q*side;
        RequireNear(geometry::Distance(value.Value()(side,(segment+f)/(a.size()-1)),expected),0,1e-9,
            "native surface, long rails and short ends share the same correspondence");
    }
    const auto sampled=kernel::FaceSamplesOf(made.Value().handle,0,9,13);
    Require(sampled.HasValue(),sampled.FirstMessageJa());
    for(int row=0;row<9;++row)for(int col=0;col<13;++col)
        RequireNear(geometry::Distance(sampled.Value().samples.At(row,col),value.Value()(col/12.0,row/8.0)),0,1e-9,
            "native evaluator and display samples use the same UV convention");
    kernel::ReleaseShape(made.Value().handle);
}

KACHA_V2_TEST(fabrication_surface, trimmed_face_does_not_silently_become_uv_rectangle)
{
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::PlanarBoundary;
    for(const auto& points:std::vector<std::vector<geometry::Vector3>>{
        {{0,0,0},{10,0,0},{8,8,0},{2,8,0},{0,0,0}},
        {{4,2,0},{4,4,0},{6,4,0},{6,2,0},{4,2,0}}}) {
        modeling::GuideChain chain;chain.index=1;chain.closed=true;
        chain.role=request.chains.empty() ? modeling::ChainRole::OuterBoundary : modeling::ChainRole::HoleBoundary;
        for(std::size_t i=1;i<points.size();++i)chain.segments.push_back(geometry::CurveSegment::MakeLine(points[i-1],points[i]).Value());
        request.chains.push_back(chain);
    }
    const auto made=Build(request);Require(made.HasValue(),made.FirstMessageJa());
    const auto value=kernel::FabricationEvaluatorOf(made.Value().handle);
    Require(!value.HasValue() && value.FirstCode()=="FAB-B006","UV rectangle must not fill a hole or cross the outline");
    const auto boundary=kernel::FaceBoundaryOf(made.Value().handle,0,geometry::GeometryTolerance{});
    Require(boundary.HasValue() && boundary.Value().holeLoops.size()==1,"planar fallback retains actual hole and perimeter");
    kernel::ReleaseShape(made.Value().handle);
}

KACHA_V2_TEST(fabrication_surface, curved_trim_follows_actual_outer_boundary)
{
    const auto surface=Patch();
    const std::vector<gp_Pnt2d> points{{0,0},{1,0},{1,.7},{.8,1},{.2,1},{0,.7}};
    BRepBuilderAPI_MakeWire wire;
    for(std::size_t i=0;i<points.size();++i)
        wire.Add(BRepBuilderAPI_MakeEdge(GCE2d_MakeSegment(points[i],points[(i+1)%points.size()]).Value(),surface).Edge());
    const auto face=BRepBuilderAPI_MakeFace(surface,wire.Wire(),true).Face();
    const auto handle=kernel::StoreShape(face);
    const auto value=kernel::FabricationEvaluatorOf(handle);
    Require(value.HasValue(),value.FirstMessageJa());
    kernel::ReleaseShape(handle); // The evaluator must own its curves and survive cache removal.
    for(int i=0;i<=100;++i)for(int j=0;j<=20;++j) {
        const double x=i/100.0,across=j/20.0;
        const double top=x<.2 ? .7+1.5*x : x>.8 ? .7+1.5*(1-x) : 1;
        const auto expected=surface->Value(x,top*across);
        const auto actual=value.Value()(across,x);
        RequireNear(geometry::Distance(actual,{expected.X(),expected.Y(),expected.Z()}),0,1e-7,
            "native curved trim covers its perimeter and interior without a UV rectangle");
    }
}

KACHA_V2_TEST(dome, 非一様ノットと次数上げでも同じ内部形状)
{
    const auto base=Patch(); base->IncreaseDegree(5,4);
    base->InsertUKnot(0.17,2,1e-12); base->InsertUKnot(0.72,1,1e-12); base->InsertVKnot(0.63,1,1e-12);
    const auto result=kernel::detail::DomePatch(base); Require(result.HasValue(),"非一様ノットを扱う");
    Boundaries(*base,*result.Value());
    for (int i=1;i<20;++i) for(int j=1;j<20;++j) {
        const double u=i/20.0,v=j/20.0;
        RequireNear(result.Value()->Value(u,v).Z(),18*v*(1-v)*(1-u*u),1e-10,"多項式を正確に表現");
    }
}

KACHA_V2_TEST(dome, 平面と同じ対向断面を不要に膨らませない)
{
    for (const auto base : {Patch(true),Patch(false,true)}) {
        const auto result=kernel::detail::DomePatch(base); Require(result.HasValue(),"作れる");
        for(int i=0;i<=20;++i)for(int j=0;j<=20;++j)
            RequireNear(base->Value(i/20.0,j/20.0).Distance(result.Value()->Value(i/20.0,j/20.0)),0,1e-10,"形を保持");
    }
}

KACHA_V2_TEST(dome, 方向反転とUV交換でも同じドーム)
{
    const auto base=Patch(); const auto original=kernel::detail::DomePatch(base).Value();
    base->UReverse(); base->ExchangeUV();
    const auto result=kernel::detail::DomePatch(base); Require(result.HasValue(),"逆順で作れる");
    for(int i=0;i<=20;++i)for(int j=0;j<=20;++j)
        RequireNear(original->Value(i/20.0,j/20.0).Distance(result.Value()->Value(j/20.0,1-i/20.0)),0,1e-10,"開始辺に依存しない");
}

KACHA_V2_TEST(dome, 四辺面と境界面で円弧への偏差を実測)
{
    for(const auto method:{modeling::GuideSurfaceMethod::FourEdgePatch,modeling::GuideSurfaceMethod::BoundaryFill}) {
        auto request=Request(method); const auto result=Build(request);
        std::string why; for(const auto& d:result.Diagnostics()) why+=d.summaryJa+" "+d.detailsJa;
        Require(result.HasValue(),"実カーネルで作れる: "+why);
        Require(result.Value().maximumDeviationMm<1e-4,"元の円弧から外れない");
        std::reverse(request.chains.begin(),request.chains.end());
        for(auto& chain:request.chains) chain.segments[0]=geometry::ReverseCurve(chain.segments[0]).Value();
        Require(Build(request).HasValue(),"輪の順番と負の掃引も扱う");
    }
}

KACHA_V2_TEST(dome, 内部ガイドや支持面G1を黙って無視しない)
{
    for(const auto method:{modeling::GuideSurfaceMethod::FourEdgePatch,modeling::GuideSurfaceMethod::BoundaryFill}) {
        auto request=Request(method);
        modeling::GuideChain guide; guide.role=modeling::ChainRole::GuideU; guide.index=1;
        guide.segments={geometry::CurveSegment::MakeLine({0,10,6},{12,10,0}).Value()};
        request.chains.push_back(guide);
        const auto result=Build(request); Require(!result.HasValue(),"ガイドを捨てず拒否");
        Require(!result.Diagnostics().empty(),"理由を出す");
    }
}

KACHA_V2_TEST(dome, 支持面の接線条件を従来方式へ黙って戻さない)
{
    auto plane=Request(modeling::GuideSurfaceMethod::FourEdgePatch);
    plane.fourEdgeStyle=modeling::FourEdgeStyle::Coons;
    plane.chains[0].segments={geometry::CurveSegment::MakeLine({0,0,0},{0,20,0}).Value()};
    const auto support=Build(plane); Require(support.HasValue(),"支持平面を作る");
    for(const auto method:{modeling::GuideSurfaceMethod::FourEdgePatch,modeling::GuideSurfaceMethod::BoundaryFill}) {
        auto request=Request(method);
        request.chains[1].continuity=modeling::SurfaceContinuity::G1;
        request.chains[1].supportSurfaceId=base::DeterministicIdGenerator{3}.NextTyped<base::IdKind::Entity>();
        request.chains[1].supportShapeHandle=support.Value().handle.value;
        const auto result=Build(request);
        Require(!result.HasValue(),"G1を省略せず拒否");
        Require(result.Diagnostics().front().summaryJa.find("G1/G2")!=std::string::npos,"対応する張り方への案内");
    }
}

KACHA_V2_TEST(dome, 斜めの3D配置と移動でも同じ形状)
{
    const auto base=Patch(); const auto original=kernel::detail::DomePatch(base).Value();
    gp_Trsf transform; transform.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),0.83);
    transform.SetTranslationPart(gp_Vec(300,-120,42)); base->Transform(transform);
    const auto result=kernel::detail::DomePatch(base); Require(result.HasValue(),"任意3D方向で作れる");
    for(int i=0;i<=20;++i)for(int j=0;j<=20;++j) {
        auto point=original->Value(i/20.0,j/20.0); point.Transform(transform);
        RequireNear(point.Distance(result.Value()->Value(i/20.0,j/20.0)),0,1e-9,"座標軸に依存しない");
    }
}

KACHA_V2_TEST(dome, 直線4辺のねじれた鞍形は不要に変形しない)
{
    const auto base=Patch(false,true);
    for(int i=1;i<=4;++i)for(int j=1;j<=4;++j) {
        auto p=base->Pole(i,j); p.SetZ(p.X()*p.Y()*0.1); base->SetPole(i,j,p);
    }
    const auto result=kernel::detail::DomePatch(base); Require(result.HasValue(),"鞍形を作れる");
    for(int i=0;i<=20;++i)for(int j=0;j<=20;++j)
        RequireNear(base->Value(i/20.0,j/20.0).Distance(result.Value()->Value(i/20.0,j/20.0)),0,1e-10,"直線の輪を保持");
}

KACHA_V2_TEST(dome, 潰れた面は診断を返して確定しない)
{
    const auto base=Patch(false,true);
    for(int i=1;i<=4;++i)for(int j=1;j<=4;++j) {
        auto p=base->Pole(i,j); p.SetX(0); base->SetPole(i,j,p);
    }
    const auto result=kernel::detail::DomePatch(base);
    Require(!result.HasValue(),"ゼロ面積を拒否");
    Require(result.Diagnostics().front().detailsJa.find("潰れ")!=std::string::npos,"失敗理由を明示");
}

KACHA_V2_TEST(dome, 片側が裾の直線なら断面全体を広げる)
{
    const auto base=Patch(false,true);
    const double y[]={0,0,10,21},z[]={0,6,6,6};
    for(int i=1;i<=4;++i)for(int j=1;j<=4;++j) {
        const double u=(i-1)/3.0;
        base->SetPole(i,j,gp_Pnt(12*u,y[j-1]*(1-u)+(j-1)*7*u,z[j-1]*(1-u)));
    }
    const auto result=kernel::detail::DomePatch(base); Require(result.HasValue(),"屋根の断面を広げる");
    for(double u:{0.25,0.5,0.75,0.9})for(double v:{0.1,0.4,0.7,0.9}) {
        const auto center=base->Value(0,v),p=result.Value()->Value(u,v);
        RequireNear(p.X(),12*u,1e-10,"選択した側の幅");
        RequireNear(p.Y(),center.Y(),1e-10,"断面の曲がる位置を保持");
        RequireNear(p.Z(),center.Z()*(1-u),1e-10,"外側まで断面全体を拡縮して伝える");
    }
    for(int k=0;k<=100;++k) {
        const double t=k/100.0;
        RequireNear(base->Value(t,0).Distance(result.Value()->Value(t,0)),0,1e-10,"裾を保持");
        RequireNear(base->Value(t,1).Distance(result.Value()->Value(t,1)),0,1e-10,"上の輪郭を保持");
        RequireNear(base->Value(0,t).Distance(result.Value()->Value(0,t)),0,1e-10,"中央断面を保持");
        RequireNear(result.Value()->Value(1,t).X(),12,1e-10,"反対側は同じ直線上");
        RequireNear(result.Value()->Value(1,t).Z(),0,1e-10,"反対側の幾何を変更しない");
    }
    const auto original=result.Value(); base->UReverse();base->ExchangeUV();
    const auto reversed=kernel::detail::DomePatch(base);Require(reversed.HasValue(),"屋根の逆順も作れる");
    for(int i=0;i<=20;++i)for(int j=0;j<=20;++j)
        RequireNear(original->Value(i/20.0,j/20.0).Distance(reversed.Value()->Value(j/20.0,1-i/20.0)),0,1e-10,"屋根の開始方向に依存しない");
}

KACHA_V2_TEST(dome, 丸い裾の角を含む境界でも断面高さを広げる)
{
    const auto base=Patch(false,true);
    const double y[]={0,0,10,21},z[]={0,6,6,6},top[]={6,6,5,0},farX[]={12,14,12,12};
    for(int i=1;i<=4;++i)for(int j=1;j<=4;++j) {
        const double u=(i-1)/3.0,v=(j-1)/3.0;
        base->SetPole(i,j,gp_Pnt(12*u+u*(farX[j-1]-12),y[j-1]*(1-u)+21*v*u,
            top[i-1]*v+(1-u)*(z[j-1]-6*v)));
    }
    const auto result=kernel::detail::DomePatch(base); Require(result.HasValue(),"丸い裾を含む屋根を作る");
    const auto originalSkirt=base->UIso(1);
    for(int k=0;k<=100;++k) {
        const double t=k/100.0;
        RequireNear(base->Value(0,t).Distance(result.Value()->Value(0,t)),0,1e-10,"中央断面を保持");
        RequireNear(base->Value(t,1).Distance(result.Value()->Value(t,1)),0,1e-10,"上の輪郭を保持");
        GeomAPI_ProjectPointOnCurve project(result.Value()->Value(1,t),originalSkirt);
        Require(project.NbPoints()>0,"裾へ投影できる");
        Require(project.LowerDistance()<2e-7,"パラメータ配分が変わっても同じ裾");
    }
    for(double u:{0.25,0.5,0.75,0.9})for(double v:{0.1,0.4,0.7,0.9}) {
        const auto p=result.Value()->Value(u,v),old=base->Value(u,v);
        RequireNear(p.X(),u*result.Value()->Value(1,v).X(),1e-10,"同じ進み位置の裾幅を使う");
        RequireNear(p.Y(),base->Value(0,v).Y(),1e-10,"中央断面の実際の位置で対応する");
        RequireNear(p.Z(),base->Value(u,1).Z()*base->Value(0,v).Z()/6,1e-10,"全幅に中央断面の高さを伝える");
        Require(p.Z()>old.Z(),"中央付近だけで弱まらない");
    }
    for(bool exchange:{false,true})for(bool reverse:{false,true})for(bool flip:{false,true}) {
        auto input=occ::handle<Geom_BSplineSurface>::DownCast(base->Copy());
        if(exchange)input->ExchangeUV(); if(reverse)input->UReverse(); if(flip)input->VReverse();
        const auto changed=kernel::detail::DomePatch(input); Require(changed.HasValue(),"全方向の開始辺に対応");
        auto restored=changed.Value();
        if(flip)restored->VReverse(); if(reverse)restored->UReverse(); if(exchange)restored->ExchangeUV();
        for(int i=0;i<=20;++i)for(int j=0;j<=20;++j)
            RequireNear(result.Value()->Value(i/20.0,j/20.0).Distance(restored->Value(i/20.0,j/20.0)),0,1e-10,"開始方向に依存しない丸い裾");
    }
}

KACHA_V2_TEST(dome, 鏡像と非対称の裾でも不要な横方向の山を作らない)
{
    for(double bulge:{0.0,2.0,4.0}) {
        const auto base=Patch(false,true);
        const double y[]={0,0,10,21},z[]={0,6,6,6},top[]={6,6,5,0},skirt[]={12,12+bulge,12,12};
        for(int i=1;i<=4;++i)for(int j=1;j<=4;++j) {
            const double u=(i-1)/3.0,v=(j-1)/3.0;
            base->SetPole(i,j,gp_Pnt(u*skirt[j-1],y[j-1]*(1-u)+21*v*u,
                top[i-1]*v+(1-u)*(z[j-1]-6*v)));
        }
        const auto original=kernel::detail::DomePatch(base);Require(original.HasValue(),"非対称な裾を扱う");
        gp_Trsf mirror;mirror.SetMirror(gp_Ax2(gp_Pnt(),gp_Dir(1,0,0)));
        base->Transform(mirror);
        const auto reflected=kernel::detail::DomePatch(base);Require(reflected.HasValue(),"鏡像も作れる");
        for(int j=1;j<100;++j) {
            double previous=1e10;
            for(int i=0;i<=100;++i) {
                const auto p=original.Value()->Value(i/100.0,j/100.0);
                Require(p.Z()<=previous+1e-9,"同じ進み位置に余計な山がない");previous=p.Z();
                auto expected=p;expected.Transform(mirror);
                RequireNear(expected.Distance(reflected.Value()->Value(i/100.0,j/100.0)),0,1e-8,"鏡対称を保持");
            }
        }
    }
}

KACHA_V2_TEST(dome, 複合8辺の屋根の実位置の勾配にこぶがない)
{
    using geometry::CurveSegment;
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::BoundaryFill;
    request.fourEdgeStyle=modeling::FourEdgeStyle::Dome;
    const auto top=CurveSegment::MakeCircularArc({0,0,-63.35728066542623},{0,1,0},{0,0,1},
        95.85728066542625,0,0.14657473619151).Value();
    const auto corner=CurveSegment::MakeCircularArc({13.5,0,27},{0,1,0},{1,0,0},4.5,0,-1.459455312453022).Value();
    const auto skirt=CurveSegment::MakeCircularArc({15,-23.239601355301925,27},{0,0,-1},
        {0.08304547985373978,-0.9965457582448798,0},3,0,-1.4876550949064555).Value();
    const std::vector<CurveSegment> curves={
        CurveSegment::MakeCubicBezier({{0,-27,27},{0,-27,32.5},{0,-10,32.5},{0,-10,32.5}}).Value(),
        CurveSegment::MakeLine({0,-10,32.5},{0,0,32.5}).Value(),top,corner,
        CurveSegment::MakeLine({18,0,27},skirt.EndPoint()).Value(),skirt,
        CurveSegment::MakeLine(skirt.StartPoint(),{6,-27,27}).Value(),
        CurveSegment::MakeLine({6,-27,27},{0,-27,27}).Value()};
    for(std::size_t i=0;i<curves.size();++i) {
        modeling::GuideChain chain;chain.role=modeling::ChainRole::BoundarySide;
        chain.index=static_cast<int>(i+1);chain.segments={curves[i]};request.chains.push_back(chain);
    }
    const auto built=Build(request);Require(built.HasValue(),"8辺の実曲面を作る: "+built.FirstSummaryJa());
    Require(built.Value().maximumDeviationMm<1e-5,"全ての元境界に沿う");
    TopoDS_Shape shape;Require(kernel::LookupShape(built.Value().handle,shape),"正本形状");
    TopExp_Explorer faces(shape,TopAbs_FACE);Require(faces.More(),"フェイス");
    const auto surface=BRep_Tool::Surface(TopoDS::Face(faces.Current()));
    double u0,u1,v0,v1;surface->Bounds(u0,u1,v0,v1);int checked=0;
    for(int i=1;i<40;++i)for(int j=1;j<40;++j) {
        gp_Pnt p;gp_Vec du,dv;surface->D1(u0+(u1-u0)*i/40,v0+(v1-v0)*j/40,p,du,dv);
        const double determinant=du.X()*dv.Y()-dv.X()*du.Y();
        if(std::abs(determinant)<1e-10)continue;
        const double slopeX=(du.Z()*dv.Y()-dv.Z()*du.Y())/determinant;
        Require(slopeX<=1e-7,"中央から裾まで高さが増える局所的なこぶを作らない");++checked;
    }
    Require(checked>1000,"屋根内部を広く検査");kernel::ReleaseShape(built.Value().handle);
}
KACHA_V2_TEST(fabrication_surface, trim_sampling_measures_uv_linearization_as_well_as_world_chords)
{
    // S(u,v)=(u,v+u*u,0), trim UV=(t,-t*t). The real edge is straight,
    // but a UV chord is not. Measuring only its 3D chord misses this error.
    NCollection_Array2<gp_Pnt> poles(1,3,1,2);
    for(int i=1;i<=3;++i)for(int j=1;j<=2;++j)poles.SetValue(i,j,gp_Pnt((i-1)*.5,(j==1?-1.:1.)+(i==3?1.:0.),0));
    TColStd_Array1OfReal uk(1,2),vk(1,2);uk(1)=0;uk(2)=1;vk(1)=-1;vk(2)=1;
    TColStd_Array1OfInteger um(1,2),vm(1,2);um(1)=um(2)=3;vm(1)=vm(2)=2;
    occ::handle<Geom_BSplineSurface> surface=new Geom_BSplineSurface(poles,uk,vk,um,vm,2,1);
    TColgp_Array1OfPnt2d control(1,3);control(1)=gp_Pnt2d(0,0);control(2)=gp_Pnt2d(.5,0);control(3)=gp_Pnt2d(1,-1);
    occ::handle<Geom2d_BezierCurve> curved=new Geom2d_BezierCurve(control);
    BRepBuilderAPI_MakeWire wire;wire.Add(BRepBuilderAPI_MakeEdge(curved,surface).Edge());
    for(const auto ends:std::vector<std::pair<gp_Pnt2d,gp_Pnt2d>>{{{1,-1},{1,1}},{{1,1},{0,1}},{{0,1},{0,0}}})
        wire.Add(BRepBuilderAPI_MakeEdge(GCE2d_MakeSegment(ends.first,ends.second).Value(),surface).Edge());
    auto face=BRepBuilderAPI_MakeFace(surface,wire.Wire(),true).Face();BRepLib::BuildCurves3d(face);
    const auto handle=kernel::StoreShape(face);const auto region=kernel::FabricationRegionOf(handle,0,.001);
    Require(region.HasValue(),region.FirstSummaryJa()+" "+region.FirstDiagnostic().detailsJa);
    int checked=0;const auto& loop=region.Value().loops.front();
    for(std::size_t i=0;i<loop.size();++i){const auto a=loop[i],b=loop[(i+1)%loop.size()];
        const auto wa=region.Value().evaluate(a.u,a.v),wb=region.Value().evaluate(b.u,b.v);
        if(std::abs(wa.y)>1e-9||std::abs(wb.y)>1e-9)continue;
        const auto midpoint=region.Value().evaluate((a.u+b.u)*.5,(a.v+b.v)*.5);
        Require(std::abs(midpoint.y)<=.001,"actual UV chord stays within the requested boundary deviation");++checked;
    }
    Require(checked>=16,"refine despite a straight 3D source boundary");kernel::ReleaseShape(handle);
}
KACHA_V2_TEST(fabrication_surface, curved_material_chart_retains_concave_outline_and_two_holes)
{
    const auto surface=Patch();
    const std::vector<std::vector<gp_Pnt2d>> loops{
        {{0,0},{1,0},{1,1},{0,1},{0,.8},{.3,.8},{.3,.2},{0,.2}},
        {{.45,.25},{.45,.4},{.6,.4},{.6,.25}},
        {{.7,.6},{.7,.8},{.9,.8},{.9,.6}}};
    BRepBuilderAPI_MakeFace face;
    for(std::size_t i=0;i<loops.size();++i){BRepBuilderAPI_MakeWire wire;
        for(std::size_t j=0;j<loops[i].size();++j)wire.Add(BRepBuilderAPI_MakeEdge(GCE2d_MakeSegment(loops[i][j],loops[i][(j+1)%loops[i].size()]).Value(),surface).Edge());
        if(i==0)face.Init(BRepBuilderAPI_MakeFace(surface,wire.Wire(),true).Face());else face.Add(wire.Wire());
    }
    auto native=face.Face();BRepLib::BuildCurves3d(native);
    const auto handle=kernel::StoreShape(native);
    const auto region=kernel::FabricationRegionOf(handle,0,.001);
    Require(region.HasValue(),region.FirstSummaryJa()+" "+region.FirstDiagnostic().detailsJa);Require(region.Value().loops.size()==3,"outer boundary and both holes accompany the support rectangle");
    Require(region.Value().samples.Valid()&&region.Value().boundarySamplingDeviationMm<=.001,"finite support samples and measured boundary error");
    kernel::ReleaseShape(handle);
    for(double u:{0.,.25,.5,1.})for(double v:{0.,.3,.6,1.}){
        const auto expected=surface->Value(v,u);const auto actual=region.Value().evaluate(u,v);
        RequireNear(geometry::Distance(actual,{expected.X(),expected.Y(),expected.Z()}),0,1e-9,"evaluator survives release and retains normalized UV");
    }
}

KACHA_V2_TEST(fabrication_surface, direct_material_face_keeps_holes_and_rejects_invalid_regions)
{
    using C=geometry::CurveSegment;using V=geometry::Vector3;
    const auto rectangle=[](double x,double y,double w,double h){return std::vector<C>{
        C::MakeLine({x,y,0},{x+w,y,0}).Value(),C::MakeLine({x+w,y,0},{x+w,y+h,0}).Value(),
        C::MakeLine({x+w,y+h,0},{x,y+h,0}).Value(),C::MakeLine({x,y+h,0},{x,y,0}).Value()};};
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::PlanarBoundary;
    modeling::GuideChain chain;chain.role=modeling::ChainRole::OuterBoundary;chain.closed=true;chain.index=1;chain.segments=rectangle(0,0,20,20);request.chains.push_back(chain);
    const geometry::GeometryTolerance tolerance;const auto support=Build(request);Require(support.HasValue(),support.FirstMessageJa());
    std::vector<std::vector<C>> loops{chain.segments,{C::MakeCircle({5,5,0},{0,0,-1},{1,0,0},2).Value()},rectangle(12,12,3,4)};
    const auto made=kernel::TrimFabricationFace(support.Value().handle,loops,tolerance);Require(made.HasValue(),made.FirstMessageJa());
    const auto boundary=kernel::FaceBoundaryOf(made.Value(),0,tolerance);Require(boundary.HasValue()&&boundary.Value().holeLoops.size()==2,"opposite circle orientation and rectangle are both holes");
    kernel::ReleaseShape(made.Value());loops.back()=rectangle(19,12,3,4);
    Require(!kernel::TrimFabricationFace(support.Value().handle,loops,tolerance).HasValue(),"outside hole is not a successful face");
    loops={chain.segments,rectangle(2,2,5,5),rectangle(4,4,5,5)};
    Require(!kernel::TrimFabricationFace(support.Value().handle,loops,tolerance).HasValue(),"overlapping holes are refused");
    kernel::ReleaseShape(support.Value().handle);
}

KACHA_V2_TEST(fabrication_surface, a_notch_without_enclosed_holes_is_not_filled_by_thickening)
{
    using C=geometry::CurveSegment;using V=geometry::Vector3;
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::RuledSections;request.keepSectionOrder=true;
    for(int r=0;r<2;++r){modeling::GuideChain chain;chain.role=modeling::ChainRole::Section;chain.index=r+1;
        chain.segments={C::MakeLine({0,20.*r,0},{20,20.*r,0}).Value()};request.chains.push_back(chain);}
    geometry::GeometryTolerance tolerance;const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,tolerance);Require(analysis.HasValue(),analysis.FirstMessageJa());
    const auto support=kernel::BuildGuideSurface(request,analysis.Value(),tolerance);Require(support.HasValue(),support.FirstMessageJa());
    const std::vector<V> points{{0,0,0},{20,0,0},{20,20,0},{0,20,0},{0,16,0},{14,16,0},{14,4,0},{0,4,0}};
    std::vector<C> boundary;for(std::size_t i=0;i<points.size();++i)boundary.push_back(C::MakeLine(points[i],points[(i+1)%points.size()]).Value());
    const auto trimmed=kernel::TrimFabricationFace(support.Value().handle,{boundary},tolerance);kernel::ReleaseShape(support.Value().handle);Require(trimmed.HasValue(),trimmed.FirstMessageJa());
    const auto solid=kernel::ThickenSurface(trimmed.Value(),.5,fabrication::ThicknessPlacement::Centered,tolerance);kernel::ReleaseShape(trimmed.Value());
    Require(solid.HasValue(),solid.FirstMessageJa());RequireNear(solid.Value().volumeMm3,116,1e-7,"C-shaped material volume excludes the edge notch");kernel::ReleaseShape(solid.Value().handle);
}

KACHA_V2_TEST(fabrication_surface, returning_bend_hole_keeps_consistent_side_wall_orientation)
{
    using C=geometry::CurveSegment;using V=geometry::Vector3;
    const std::vector<V> a{{0,0,0},{200,0,0},{199.5,.8660254037844386,0},{99.5,.8660254037844386,0}};
    std::vector<V> b;for(auto p:a)b.push_back(p+V{0,0,10});
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::RuledSections;request.keepSectionOrder=true;
    for(int r=0;r<2;++r){modeling::GuideChain chain;chain.role=modeling::ChainRole::Section;chain.index=r+1;const auto& points=r?b:a;
        for(std::size_t i=1;i<points.size();++i)chain.segments.push_back(C::MakeLine(points[i-1],points[i]).Value());request.chains.push_back(chain);}
    geometry::GeometryTolerance tolerance;const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,tolerance);Require(analysis.HasValue(),analysis.FirstMessageJa());
    const auto support=kernel::BuildGuideSurface(request,analysis.Value(),tolerance);Require(support.HasValue(),support.FirstMessageJa());
    std::vector<V> outer=a;for(auto i=b.rbegin();i!=b.rend();++i)outer.push_back(*i);
    const std::vector<V> hole{{10,0,3},{20,0,3},{20,0,7},{10,0,7}};
    std::vector<std::vector<C>> loops;for(const auto& points:{outer,hole}){std::vector<C> curves;
        for(std::size_t i=0;i<points.size();++i)curves.push_back(C::MakeLine(points[i],points[(i+1)%points.size()]).Value());loops.push_back(curves);}
    const auto face=kernel::TrimFabricationFace(support.Value().handle,loops,tolerance);kernel::ReleaseShape(support.Value().handle);Require(face.HasValue(),face.FirstMessageJa());
    const auto solid=kernel::ThickenSurface(face.Value(),.01,fabrication::ThicknessPlacement::Centered,tolerance);kernel::ReleaseShape(face.Value());
    Require(solid.HasValue(),solid.FirstMessageJa());RequireNear(solid.Value().volumeMm3,29.7,1e-6,"returning bend volume retains its opening");kernel::ReleaseShape(solid.Value().handle);
}
KACHA_V2_TEST(fabrication_surface, warped_faceted_sheet_retains_hole_and_normal_thickness)
{
    using C=geometry::CurveSegment;using V=geometry::Vector3;
    const std::vector<V> a{{0,0,0},{10,0,0},{15,5,0}},b{{0,0,8},{10,0,8},{15,5.5,9}};
    BRepBuilderAPI_Sewing sewing(1e-6);
    for(int i=0;i<2;++i)for(const auto& points:std::vector<std::vector<V>>{{a[i],a[i+1],b[i]},{b[i],a[i+1],b[i+1]}}){
        BRepBuilderAPI_MakePolygon polygon;for(auto p:points)polygon.Add(gp_Pnt(p.x,p.y,p.z));polygon.Close();
        BRepBuilderAPI_MakeFace face(polygon.Wire(),true);Require(face.IsDone(),"facet fixture");sewing.Add(face.Face());
    }
    sewing.Perform();const auto source=kernel::StoreShape(sewing.SewedShape());
    std::vector<V> outer=a;for(auto i=b.rbegin();i!=b.rend();++i)outer.push_back(*i);
    const std::vector<V> hole{{2,0,1},{4,0,1},{4,0,3},{2,0,3}};
    std::vector<std::vector<C>> loops;for(const auto& points:{outer,hole}){std::vector<C> curves;
        for(std::size_t i=0;i<points.size();++i)curves.push_back(C::MakeLine(points[i],points[(i+1)%points.size()]).Value());loops.push_back(curves);}
    geometry::GeometryTolerance tolerance;const auto cut=kernel::TrimSurfaceRegion({source},loops,tolerance,1e-6);Require(cut.HasValue(),cut.FirstMessageJa());
    for(auto placement:{fabrication::ThicknessPlacement::Outside,fabrication::ThicknessPlacement::Centered,fabrication::ThicknessPlacement::Inside}){
        const auto full=kernel::ThickenSurface(source,.2,placement,tolerance);Require(full.HasValue(),full.FirstMessageJa());
        const auto perforated=kernel::ThickenSurface(cut.Value(),.2,placement,tolerance);Require(perforated.HasValue(),perforated.FirstMessageJa());
        RequireNear(full.Value().volumeMm3-perforated.Value().volumeMm3,.8,1e-6,"4 square mm hole removes exactly normal thickness times area");
        kernel::ReleaseShape(full.Value().handle);kernel::ReleaseShape(perforated.Value().handle);
    }
    kernel::ReleaseShape(cut.Value());kernel::ReleaseShape(source);
}
#endif
KACHA_V2_TEST_MAIN("kernel_dome_tests")
