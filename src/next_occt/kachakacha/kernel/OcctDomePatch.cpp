#include "kachakacha/kernel/OcctDomePatch.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <gp_XYZ.hxx>
#include <algorithm>
#include <cmath>
#include <vector>

namespace kachakacha::v2::kernel::detail {
namespace {
struct Coefficients {
    std::vector<double> linear, square, cube;
};

Coefficients PolynomialCoefficients(const Geom_BSplineSurface& surface, bool uDirection)
{
    const int degree = uDirection ? surface.UDegree() : surface.VDegree();
    const int count = uDirection ? surface.NbUKnots() : surface.NbVKnots();
    const int poles = uDirection ? surface.NbUPoles() : surface.NbVPoles();
    const auto knot = [&](int k) { return uDirection ? surface.UKnot(k) : surface.VKnot(k); };
    const double first = knot(1), span = knot(count) - first;
    std::vector<double> knots;
    for (int k = 1; k <= count; ++k) {
        const int multiplicity = uDirection ? surface.UMultiplicity(k) : surface.VMultiplicity(k);
        for (int j = 0; j < multiplicity; ++j) knots.push_back((knot(k) - first) / span);
    }
    Coefficients out;
    // Polynomial blossom: the coefficient of t^n is the mean of all n-fold knot products.
    // This is exact for arbitrary nonuniform knots; Greville sampling of t^2 is not exact.
    for (int i = 0; i < poles; ++i) {
        double a = 0, b = 0, c = 0;
        for (int j = 1; j <= degree; ++j) {
            a += knots[i+j];
            for (int k = j+1; k <= degree; ++k) {
                b += knots[i+j]*knots[i+k];
                for (int l = k+1; l <= degree; ++l) c += knots[i+j]*knots[i+k]*knots[i+l];
            }
        }
        out.linear.push_back(a / degree);
        out.square.push_back(b * 2 / (degree*(degree-1)));
        out.cube.push_back(c * 6 / (degree*(degree-1)*(degree-2)));
    }
    return out;
}

double Bending(const Geom_BSplineSurface& surface, bool alongU, bool last)
{
    double u0, u1, v0, v1; surface.Bounds(u0,u1,v0,v1);
    const auto point = [&](double t) {
        return alongU ? surface.Value(u0+(u1-u0)*t, last ? v1 : v0).XYZ()
                      : surface.Value(last ? u1 : u0, v0+(v1-v0)*t).XYZ();
    };
    const auto a = point(0), b = point(1);
    const auto chord = b-a;
    double energy = 0;
    for (int k = 1; k < 32; ++k) {
        const double t = k / 32.0;
        auto residual = point(t) - a*(1-t) - b*t;
        if (chord.SquareModulus()>1.0e-24)
            residual -= chord*(residual.Dot(chord)/chord.SquareModulus());
        energy += residual.SquareModulus();
    }
    return energy / 31;
}

std::vector<double> Blend(const Coefficients& coefficients, double firstEnergy, double lastEnergy)
{
    std::vector<double> out;
    for (std::size_t i = 0; i < coefficients.linear.size(); ++i) {
        const double t = coefficients.linear[i], t2 = coefficients.square[i], t3 = coefficients.cube[i];
        const double sum = firstEnergy + lastEnergy;
        // Smoothly vary between t^2 and 2t-t^2 (no threshold jump for almost equal sides).
        const double imbalance = sum > 1.0e-18 ? (firstEnergy-lastEnergy) / sum : 0;
        const double strength = std::abs(imbalance);
        const double biased = imbalance >= 0 ? t2 : 2*t-t2;
        out.push_back(strength*biased + (1-strength)*(3*t2-2*t3));
    }
    return out;
}

bool Regular(const Geom_BSplineSurface& surface, const Geom_BSplineSurface& base)
{
    double u0,u1,v0,v1; surface.Bounds(u0,u1,v0,v1);
    for (int i = 1; i < 32; ++i) for (int j = 1; j < 32; ++j) {
        const double u = u0+(u1-u0)*i/32, v = v0+(v1-v0)*j/32;
        gp_Pnt p; gp_Vec du,dv,bu,bv;
        surface.D1(u,v,p,du,dv); base.D1(u,v,p,bu,bv);
        gp_Vec n = du.Crossed(dv);
        const gp_Vec reference = bu.Crossed(bv);
        if(n.SquareMagnitude()<1.0e-24) {
            // A repeated end control point can make the parameter stationary at a
            // smooth geometric join. Check local secants instead of rejecting it.
            du=gp_Vec(surface.Value(u-(u1-u0)*1e-5,v),surface.Value(u+(u1-u0)*1e-5,v));
            dv=gp_Vec(surface.Value(u,v-(v1-v0)*1e-5),surface.Value(u,v+(v1-v0)*1e-5));
            n=du.Crossed(dv);
        }
        if (!std::isfinite(n.SquareMagnitude()) || n.SquareMagnitude() < 1.0e-24) return false;
        if (reference.SquareMagnitude() > 1.0e-24 && n.Dot(reference) <= 0) return false;
    }
    return true;
}
} // namespace

base::Result<occ::handle<Geom_BSplineSurface>> DomePatch(const occ::handle<Geom_BSplineSurface>& base)
{
    using Out = base::Result<occ::handle<Geom_BSplineSurface>>;
    const auto fail = [](const char* reason) { return Out::Failure(base::MakeError("KER-S006",
        "滑らかなドーム（近似）を作れませんでした。", reason)); };
    if (base.IsNull() || base->IsURational() || base->IsVRational()
        || base->UDegree()<3 || base->VDegree()<3 || base->IsUPeriodic() || base->IsVPeriodic()
        || base->UMultiplicity(1)!=base->UDegree()+1 || base->UMultiplicity(base->NbUKnots())!=base->UDegree()+1
        || base->VMultiplicity(1)!=base->VDegree()+1 || base->VMultiplicity(base->NbVKnots())!=base->VDegree()+1)
        return fail("非有理の4側の境界が必要です。標準など別の張り方を選んでください。");
    auto surface = TransportDomeProfile(base);
    if(!surface.IsNull()) {
        if(!Regular(*surface,*base))return fail("断面を広げると面が折り返すか潰れます。境界を見直すか標準を選んでください。");
        return Out::Success(surface);
    }
    surface = occ::handle<Geom_BSplineSurface>::DownCast(base->Copy());
    const auto u = PolynomialCoefficients(*base, true), v = PolynomialCoefficients(*base, false);
    const auto f = Blend(u, Bending(*base,false,false), Bending(*base,false,true));
    const auto g = Blend(v, Bending(*base,true,false), Bending(*base,true,true));
    const int nu = base->NbUPoles(), nv = base->NbVPoles();
    const auto p00=base->Pole(1,1).XYZ(), p10=base->Pole(nu,1).XYZ();
    const auto p01=base->Pole(1,nv).XYZ(), p11=base->Pole(nu,nv).XYZ();
    for (int i = 2; i < nu; ++i) for (int j = 2; j < nv; ++j) {
        const double s=u.linear[i-1], t=v.linear[j-1], a=f[i-1], b=g[j-1];
        const auto left = base->Pole(1,j).XYZ() - p00*(1-t) - p01*t;
        const auto right = base->Pole(nu,j).XYZ() - p10*(1-t) - p11*t;
        const auto bottom = base->Pole(i,1).XYZ() - p00*(1-s) - p10*s;
        const auto top = base->Pole(i,nv).XYZ() - p01*(1-s) - p11*s;
        const auto bilinear = p00*((1-s)*(1-t)) + p10*(s*(1-t)) + p01*((1-s)*t) + p11*(s*t);
        surface->SetPole(i,j,gp_Pnt(bilinear+left*(1-a)+right*a+bottom*(1-b)+top*b));
    }
    if (!Regular(*surface,*base)) return fail("面の内側が折り返すか潰れます。境界を見直すか、標準など別の張り方を選んでください。");
    return Out::Success(surface);
}
} // namespace kachakacha::v2::kernel::detail
#endif
