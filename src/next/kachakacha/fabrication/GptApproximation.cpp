#include "kachakacha/fabrication/GptApproximation.h"
#include "kachakacha/geometry/CurveSampling.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace kachakacha::v2::fabrication {
using geometry::Vector3;
using geometry::Point2;
using geometry::Dot;
using geometry::Cross;
using geometry::Normalized;
namespace {
using Out = base::Result<GptApproxResult>;
std::vector<Vector3> Clip(const std::vector<Vector3>& input, double cut, bool above)
{
    std::vector<Vector3> out;
    if (input.empty()) { return out; }
    auto a = input.back();
    bool ia = above ? a.y >= cut : a.y <= cut;
    for (const auto& b : input) {
        const bool ib = above ? b.y >= cut : b.y <= cut;
        if (ia != ib) { out.push_back(a + (b-a)*((cut-a.y)/(b.y-a.y))); }
        if (ib) { out.push_back(b); }
        a=b; ia=ib;
    }
    return out;
}
// Least-squares linear hat basis. Smoothness regularization prevents empty-bin spikes.
bool Fit(GptApproxPanel& panel, const std::vector<Vector3>& points)
{
    constexpr int count=33;
    double lo=points.front().x, hi=lo;
    for (const auto& p:points) { lo=std::min(lo,p.x); hi=std::max(hi,p.x); }
    if (hi-lo < 1e-8) { return false; }
    std::vector<double> diagonal(count,.000001), off(count-1,0), rhs(count,0);
    for (const auto& p:points) {
        const double t=std::clamp((p.x-lo)/(hi-lo)*(count-1),0.0,double(count-1));
        const int i=std::min(count-2,int(t)); const double f=t-i;
        diagonal[i]+=(1-f)*(1-f); diagonal[i+1]+=f*f;
        off[i]+=(1-f)*f; rhs[i]+=(1-f)*p.z; rhs[i+1]+=f*p.z;
    }
    for (int i=0;i<count-1;++i) { diagonal[i]+=.01; diagonal[i+1]+=.01; off[i]-=.01; }
    for (int i=1;i<count;++i) {
        const double factor=off[i-1]/diagonal[i-1];
        diagonal[i]-=factor*off[i-1]; rhs[i]-=factor*rhs[i-1];
    }
    rhs.back()/=diagonal.back();
    for (int i=count-2;i>=0;--i) { rhs[i]=(rhs[i]-off[i]*rhs[i+1])/diagonal[i]; }
    panel.lengths.push_back(0);
    for (int i=0;i<count;++i) {
        panel.profile.push_back({lo+(hi-lo)*i/(count-1),rhs[i]});
        if (i) {
            const auto a=panel.profile[i-1], b=panel.profile[i];
            panel.lengths.push_back(panel.lengths.back()+std::hypot(b.u-a.u,b.v-a.v));
        }
    }
    return true;
}
Point2 Flatten(const GptApproxPanel& panel, const Vector3& p, double* error=nullptr)
{
    const auto& profile=panel.profile;
    const double t=std::clamp((p.x-profile.front().u)/(profile.back().u-profile.front().u)
        *(profile.size()-1),0.0,double(profile.size()-1));
    const auto i=std::min(profile.size()-2,static_cast<std::size_t>(t));
    const double f=t-i;
    if (error) { *error=std::abs(p.z-profile[i].v*(1-f)-profile[i+1].v*f); }
    return {panel.lengths[i]*(1-f)+panel.lengths[i+1]*f,p.y};
}
std::vector<Point2> FlattenLoop(const GptApproxPanel& panel, const std::vector<Vector3>& loop)
{
    std::vector<Point2> out;
    if (loop.empty()) { return out; }
    auto a=loop.back();
    for (const auto& b:loop) {
        std::vector<double> parameters{0};
        if (std::abs(b.x-a.x)>1e-12) {
            for (const auto& p:panel.profile) {
                const double t=(p.u-a.x)/(b.x-a.x);
                if (t>1e-9 && t<1-1e-9) { parameters.push_back(t); }
            }
        }
        std::sort(parameters.begin(),parameters.end());
        for (double t:parameters) { out.push_back(Flatten(panel,a+(b-a)*t)); }
        a=b;
    }
    return out;
}
bool BuildCandidate(const GptApproxSource& source, const GptApproxOptions& options,
    GptApproxPanel frame, int count, GptApproxResult& result)
{
    const auto local=[&](Vector3 p) { p=p-frame.origin; return Vector3{Dot(p,frame.u),Dot(p,frame.v),Dot(p,frame.normal)}; };
    std::vector<Vector3> boundary, samples;
    for (const auto& p:source.boundary) { boundary.push_back(local(p)); }
    for (const auto& p:source.samples) { samples.push_back(local(p)); }
    samples.insert(samples.end(),boundary.begin(),boundary.end());
    double low=boundary.front().y, high=low;
    for (const auto& p:boundary) { low=std::min(low,p.y); high=std::max(high,p.y); }
    if ((high-low)/count < options.minimumWidthMm) { return false; }
    double square=0; std::size_t total=0;
    for (int index=0;index<count;++index) {
        const double a=low+(high-low)*index/count, b=low+(high-low)*(index+1)/count;
        auto loop=Clip(Clip(boundary,a,true),b,false);
        if (loop.size()<3) { return false; }
        std::vector<Vector3> inside;
        for (const auto& p:samples) { if (p.y>=a-1e-8 && p.y<=b+1e-8) { inside.push_back(p); } }
        inside.insert(inside.end(),loop.begin(),loop.end());
        if (inside.size()<3) { return false; }
        auto panel=frame;
        if (!Fit(panel,inside)) { return false; }
        // Tilt each ruling to the local axial slope before fitting its cross section.
        double sy=0, sz=0, syy=0, syz=0;
        for (const auto& p:inside) {
            const auto flat=Flatten(panel,p);
            const auto world=panel.Point(flat,1);
            const double residual=p.z-Dot(world-frame.origin,frame.normal);
            sy+=p.y; sz+=residual; syy+=p.y*p.y; syz+=p.y*residual;
        }
        const double n=double(inside.size()), denominator=syy-sy*sy/n;
        const double slope=denominator>1e-12 ? (syz-sy*sz/n)/denominator : 0;
        const double scale=std::sqrt(1+slope*slope);
        const auto tilted=[&](Vector3 p) { return Vector3{p.x,(p.y+slope*p.z)/scale,(p.z-slope*p.y)/scale}; };
        panel.v=(frame.v+frame.normal*slope)*(1/scale);
        panel.normal=(frame.normal-frame.v*slope)*(1/scale);
        panel.profile.clear(); panel.lengths.clear();
        for (auto& p:inside) { p=tilted(p); }
        for (auto& p:loop) { p=tilted(p); }
        if (!Fit(panel,inside)) { return false; }
        panel.pattern.outline=FlattenLoop(panel,loop);
        for (const auto& hole:source.holes) {
            std::vector<Vector3> h; double minY=high, maxY=low;
            for (const auto& p:hole) { h.push_back(local(p)); minY=std::min(minY,h.back().y); maxY=std::max(maxY,h.back().y); }
            if (maxY<a || minY>b) { continue; }
            // A seam must not turn a hole into a closed opening outside a panel.
            if (minY<a || maxY>b) { return false; }
            for (auto& p:h) { p=tilted(p); }
            panel.pattern.openings.push_back(FlattenLoop(panel,h));
            inside.insert(inside.end(),h.begin(),h.end());
        }
        for (const auto& p:inside) {
            double error=0; (void)Flatten(panel,p,&error);
            panel.maximumMm=std::max(panel.maximumMm,error); panel.squaredMm+=error*error; ++panel.sampleCount;
        }
        panel.pattern.panelId="GPT 部材"+std::to_string(index+1);
        result.maximumMm=std::max(result.maximumMm,panel.maximumMm);
        square+=panel.squaredMm; total+=panel.sampleCount;
        result.panels.push_back(std::move(panel));
    }
    result.rmsMm=std::sqrt(square/std::max(std::size_t(1),total));
    result.reached=result.maximumMm<=options.toleranceMm;
    return true;
}
}

base::Result<GptApproxResult> ApproximateGpt(const GptApproxSource& source,const GptApproxOptions& options)
{
    if (source.samples.size()<3 || source.boundary.size()<3 || !std::isfinite(options.toleranceMm)
        || options.toleranceMm<=0 || !std::isfinite(options.minimumWidthMm) || options.minimumWidthMm<=0
        || options.maximumPanels<1 || options.maximumPanels>64 || options.direction<0 || options.direction>2) {
        return Out::Failure(base::MakeError("GPT-F001","近似の対象または条件が不正です。","面・許容偏差・部材数・最小幅を確認してください。"));
    }
    for (const auto& p:source.samples) {
        if (!std::isfinite(p.x+p.y+p.z)) { return Out::Failure(base::MakeError("GPT-F001","面の標本に不正な値があります。","元の面を確認してください。")); }
    }
    const auto plane=geometry::FitPlane(source.samples);
    if (!plane.valid) { return Out::Failure(base::MakeError("GPT-F002","面の方向を決められません。","面を小さい領域に分けてください。")); }
    GptApproxResult best; best.maximumMm=std::numeric_limits<double>::infinity();
    for (int count=1;count<=options.maximumPanels;++count) {
        for (int direction=0;direction<2;++direction) {
            if (options.direction!=2 && options.direction!=direction) { continue; }
            GptApproxPanel frame; frame.origin=plane.origin; frame.normal=plane.normal;
            Vector3 reference=std::abs(plane.normal.x)<.8 ? Vector3{1,0,0} : Vector3{0,1,0};
            frame.u=Normalized(reference-plane.normal*Dot(reference,plane.normal));
            frame.v=Normalized(Cross(plane.normal,frame.u));
            if (direction) { std::swap(frame.u,frame.v); frame.normal=frame.normal*-1; }
            GptApproxResult candidate; candidate.direction=direction;
            if (!BuildCandidate(source,options,frame,count,candidate)) { continue; }
            if (candidate.maximumMm<best.maximumMm) { best=std::move(candidate); }
        }
        if (best.reached) { return Out::Success(std::move(best)); }
    }
    if (best.panels.empty()) { return Out::Failure(base::MakeError("GPT-F003","この条件では部材を作れません。","最小幅を小さくするか、開口をまたがない方向を指定してください。")); }
    return Out::Success(std::move(best));
}

geometry::Vector3 GptApproxPanel::Point(Point2 flat,double progress) const
{
    progress=std::clamp(progress,0.0,1.0);
    double x=profile.front().u, z=profile.front().v;
    const double length=std::clamp(flat.u,0.0,lengths.back());
    for (std::size_t i=1;i<profile.size();++i) {
        const double step=std::clamp(length-lengths[i-1],0.0,lengths[i]-lengths[i-1]);
        const double angle=std::atan2(profile[i].v-profile[i-1].v,profile[i].u-profile[i-1].u)*progress;
        x+=step*std::cos(angle); z+=step*std::sin(angle);
    }
    return origin+u*x+v*flat.v+normal*z;
}
std::vector<Vector3> GptPanelLoop(const GptApproxPanel& panel,const std::vector<Point2>& loop,double progress)
{
    std::vector<Vector3> out;
    for (const auto& p:loop) { out.push_back(panel.Point(p,progress)); }
    if (!out.empty()) { out.push_back(out.front()); }
    return out;
}
}
