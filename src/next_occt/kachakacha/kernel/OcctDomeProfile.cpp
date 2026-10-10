#include "kachakacha/kernel/OcctDomePatch.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <gp_XYZ.hxx>
#include <cmath>

namespace kachakacha::v2::kernel::detail {
namespace {
double ProfileBend(const Geom_BSplineSurface& base)
{
    double u0,u1,v0,v1; base.Bounds(u0,u1,v0,v1);
    const auto start=base.Value(u0,v0).XYZ(),chord=base.Value(u0,v1).XYZ()-start;
    if(chord.SquareModulus()<1e-12)return 0;
    double bend=0;
    for(int k=1;k<20;++k) {
        const auto p=base.Value(u0,v0+(v1-v0)*k/20.0).XYZ()-start;
        bend+=p.Crossed(chord).SquareModulus()/chord.SquareModulus();
    }
    return bend/19;
}

occ::handle<Geom_BSplineSurface> Transport(const occ::handle<Geom_BSplineSurface>& base)
{
    const int nu=base->NbUPoles(),nv=base->NbVPoles();
    const auto b0=base->Pole(1,1).XYZ(),t0=base->Pole(1,nv).XYZ();
    const auto b1=base->Pole(nu,1).XYZ(),t1=base->Pole(nu,nv).XYZ();
    auto n=(b1-b0).Crossed(t1-b0);
    if(n.Modulus()<1e-10)return {};
    n.Normalize();
    const double height=(t0-b0).Dot(n);
    const auto along=t0-b0-n*height;
    if(std::abs(height)<1e-6 || along.SquareModulus()<1e-12)return {};
    const auto opposite=t1-b1;
    if(opposite.SquareModulus()<1e-12)return {};
    const double tolerance=1e-6;
    // The skirt and the opposite side must lie on one plane. The curved
    // section must lie in its own plane. Otherwise retain the general dome blend.
    for(int i=1;i<=nu;++i)
        if(std::abs((base->Pole(i,1).XYZ()-b0).Dot(n))>tolerance)return {};
    bool straight=true;
    for(int j=1;j<=nv;++j) {
        const auto p=base->Pole(nu,j).XYZ()-b1;
        if(std::abs((base->Pole(nu,j).XYZ()-b0).Dot(n))>tolerance)return {};
        straight=straight && p.Crossed(opposite).Modulus()/opposite.Modulus()<=tolerance;
        if(std::abs((base->Pole(1,j).XYZ()-b0).Dot(along.Crossed(n).Normalized()))>tolerance)return {};
    }
    // Preserve the entire curved section (not only its distance from a chord).
    // Its position along the base and height are independent profile coordinates.
    // Reparameterizing the opposite straight side leaves the geometric edge intact.
    auto out=occ::handle<Geom_BSplineSurface>::DownCast(base->Copy());
    for(int j=1;j<=nv;++j) {
        const auto profile=base->Pole(1,j).XYZ()-b0;
        const double t=profile.Dot(along)/along.SquareModulus(),h=profile.Dot(n)/height;
        if(t < -tolerance || t > 1+tolerance || h < -tolerance || h > 1+tolerance)return {};
        for(int i=1;i<=nu;++i) {
            const auto bottom=base->Pole(i,1).XYZ(),top=base->Pole(i,nv).XYZ();
            if(straight)out->SetPole(i,j,gp_Pnt(bottom*(1-t)+top*t+n*((top-bottom).Dot(n)*(h-t))));
            else {
                // A rounded skirt can be grouped with the opposite side. Keep
                // its footprint and all four boundaries; transport profile height
                // multiplicatively instead of fading only its chord residual.
                const auto p=base->Pole(i,j).XYZ();
                out->SetPole(i,j,gp_Pnt(p+n*((top-b0).Dot(n)*h-(p-b0).Dot(n))));
            }
        }
    }
    return out;
}
}

occ::handle<Geom_BSplineSurface> TransportDomeProfile(const occ::handle<Geom_BSplineSurface>& base)
{
    occ::handle<Geom_BSplineSurface> best; double strongest=0;
    for(bool exchange:{false,true})for(bool reverse:{false,true})for(bool flip:{false,true}) {
        auto oriented=occ::handle<Geom_BSplineSurface>::DownCast(base->Copy());
        if(exchange)oriented->ExchangeUV();
        if(reverse)oriented->UReverse();
        if(flip)oriented->VReverse();
        auto result=Transport(oriented);
        if(result.IsNull())continue;
        const double bend=ProfileBend(*oriented);
        // Choosing the first eligible side would depend on the ring's start
        // direction. Transport the most curved section, not a straight top edge.
        if(bend<=strongest+1e-12)continue;
        if(flip)result->VReverse();
        if(reverse)result->UReverse();
        if(exchange)result->ExchangeUV();
        best=result; strongest=bend;
    }
    return best;
}
}
#endif
