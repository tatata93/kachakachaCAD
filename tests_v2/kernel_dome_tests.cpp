#include "kachakacha/base/TestHarness.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctDomePatch.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/geometry/ArcBuilders.h"
#include "kachakacha/geometry/WireEdit.h"
#include <NCollection_Array2.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Ax1.hxx>
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
    Boundaries(*base,*result.Value());
    for(double u:{0.25,0.5,0.75,0.9})for(double v:{0.1,0.4,0.7,0.9}) {
        const auto p=result.Value()->Value(u,v),old=base->Value(u,v);
        RequireNear(p.X(),old.X(),1e-10,"丸い外周の足元を保つ");
        RequireNear(p.Y(),old.Y(),1e-10,"外周の対応を保つ");
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
#endif
KACHA_V2_TEST_MAIN("kernel_dome_tests")
