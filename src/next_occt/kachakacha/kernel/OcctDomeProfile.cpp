#include "kachakacha/kernel/OcctDomePatch.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <Geom_BSplineCurve.hxx>
#include <NCollection_Array2.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <gp_XYZ.hxx>
#include <algorithm>
#include <cmath>
#include <map>

namespace kachakacha::v2::kernel::detail {
namespace {
bool StraightBottom(const Geom_BSplineSurface& base, const gp_XYZ& origin, const gp_XYZ& width)
{
    for(int i=1;i<=base.NbUPoles();++i)
        if((base.Pole(i,1).XYZ()-origin).Crossed(width).Modulus()>1e-6*width.Modulus())return false;
    return true;
}

void CommonKnots(Geom_BSplineCurve& a, Geom_BSplineCurve& b)
{
    const int degree=std::max(a.Degree(),b.Degree()); a.IncreaseDegree(degree); b.IncreaseDegree(degree);
    std::map<double,int> knots;
    for(const auto* curve:{&a,&b})for(int k=2;k<curve->NbKnots();++k)
        knots[curve->Knot(k)]=std::max(knots[curve->Knot(k)],curve->Multiplicity(k));
    for(const auto& [k,m]:knots) { a.InsertKnot(k,m,1e-12,false); b.InsertKnot(k,m,1e-12,false); }
}

occ::handle<Geom_BSplineSurface> Transport(const occ::handle<Geom_BSplineSurface>& base,bool& failed)
{
    const int nu=base->NbUPoles(),nv=base->NbVPoles();
    const auto b0=base->Pole(1,1).XYZ(),t0=base->Pole(1,nv).XYZ();
    const auto width=base->Pole(nu,1).XYZ()-b0,t1=base->Pole(nu,nv).XYZ();
    auto n=width.Crossed(t1-b0);
    if(n.Modulus()<1e-10 || !StraightBottom(*base,b0,width))return {};
    n.Normalize();
    const double height=(t0-b0).Dot(n);
    const auto along=t0-b0-n*height;
    if(std::abs(height)<1e-6 || along.SquareModulus()<1e-12)return {};
    const auto across=width.Normalized(),forward=along.Normalized();
    // Geometric coordinates, not the old Coons UV footprint, define correspondence.
    // The upper section is perpendicular to the central section and the skirt is planar.
    for(int i=1;i<=nu;++i) {
        if(std::abs((base->Pole(i,1).XYZ()-b0).Dot(n))>1e-6)return {};
        if(std::abs((base->Pole(i,nv).XYZ()-t0).Dot(forward))>1e-6)return {};
    }
    for(int j=1;j<=nv;++j) {
        if(std::abs((base->Pole(1,j).XYZ()-b0).Dot(across))>1e-6)return {};
        if(std::abs((base->Pole(nu,j).XYZ()-b0).Dot(n))>1e-6)return {};
    }
    const double upperWidth=(t1-b0).Dot(across);
    if(upperWidth<=1e-6)return {};
    double u0,u1,v0,v1;base->Bounds(u0,u1,v0,v1);
    auto center=occ::handle<Geom_BSplineCurve>::DownCast(base->UIso(u0));
    auto top=occ::handle<Geom_BSplineCurve>::DownCast(base->VIso(v1));
    auto skirt=DomeSkirtWidth(*base,forward,across,failed);
    if(center.IsNull() || top.IsNull() || skirt.IsNull())return {};
    CommonKnots(*center,*skirt);
    NCollection_Array2<gp_Pnt> poles(1,top->NbPoles(),1,center->NbPoles());
    for(int i=1;i<=top->NbPoles();++i)for(int j=1;j<=center->NbPoles();++j) {
        const auto upper=top->Pole(i).XYZ()-b0,profile=center->Pole(j).XYZ()-b0;
        poles(i,j)=gp_Pnt(b0+across*(upper.Dot(across)*skirt->Pole(j).X()/upperWidth)
            +forward*profile.Dot(forward)+n*(upper.Dot(n)*profile.Dot(n)/height));
    }
    return new Geom_BSplineSurface(poles,top->Knots(),center->Knots(),top->Multiplicities(),
        center->Multiplicities(),top->Degree(),center->Degree());
}
}

occ::handle<Geom_BSplineSurface> TransportDomeProfile(const occ::handle<Geom_BSplineSurface>& base,bool& failed)
{
    failed=false;
    // The straight short side and two perpendicular section planes identify the
    // transport direction. A rounded skirt cannot accidentally become the short side.
    for(bool exchange:{false,true})for(bool reverse:{false,true})for(bool flip:{false,true}) {
        auto oriented=occ::handle<Geom_BSplineSurface>::DownCast(base->Copy());
        if(exchange)oriented->ExchangeUV(); if(reverse)oriented->UReverse(); if(flip)oriented->VReverse();
        auto result=Transport(oriented,failed);
        if(failed)return {};
        if(result.IsNull())continue;
        if(flip)result->VReverse(); if(reverse)result->UReverse(); if(exchange)result->ExchangeUV();
        return result;
    }
    return {};
}
}
#endif
