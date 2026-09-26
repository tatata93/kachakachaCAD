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
bool Fit(GptApproxPanel& panel, const std::vector<Vector3>& points,int passes=1)
{
    constexpr int count=17;
    double lo=points.front().x, hi=lo;
    for (const auto& p:points) { lo=std::min(lo,p.x); hi=std::max(hi,p.x); }
    if (hi-lo < 1e-8) { return false; }
    std::vector<double> heights(count,0), weights(points.size(),1);
    for (int pass=0;pass<passes;++pass) {
    std::vector<double> diagonal(count,.000001), off(count-1,0), rhs(count,0);
    for (std::size_t sample=0;sample<points.size();++sample) {
        const auto& p=points[sample]; const double weight=weights[sample];
        const double t=std::clamp((p.x-lo)/(hi-lo)*(count-1),0.0,double(count-1));
        const int i=std::min(count-2,int(t)); const double f=t-i;
        diagonal[i]+=weight*(1-f)*(1-f); diagonal[i+1]+=weight*f*f;
        off[i]+=weight*(1-f)*f; rhs[i]+=weight*(1-f)*p.z; rhs[i+1]+=weight*f*p.z;
    }
    for (int i=0;i<count-1;++i) { diagonal[i]+=.01; diagonal[i+1]+=.01; off[i]-=.01; }
    for (int i=1;i<count;++i) {
        const double factor=off[i-1]/diagonal[i-1];
        diagonal[i]-=factor*off[i-1]; rhs[i]-=factor*rhs[i-1];
    }
    rhs.back()/=diagonal.back();
    for (int i=count-2;i>=0;--i) { rhs[i]=(rhs[i]-off[i]*rhs[i+1])/diagonal[i]; }
    for (int i=0;i<count;++i) { heights[i]=pass ? (heights[i]+rhs[i])*.5 : rhs[i]; }
    double maxError=1e-10;
    for (std::size_t sample=0;sample<points.size();++sample) {
        const auto& p=points[sample];
        const double t=std::clamp((p.x-lo)/(hi-lo)*(count-1),0.0,double(count-1));
        const int i=std::min(count-2,int(t)); const double f=t-i;
        weights[sample]=std::abs(p.z-heights[i]*(1-f)-heights[i+1]*f);
        maxError=std::max(maxError,weights[sample]);
    }
    for (auto& weight:weights) { weight=1+100*std::pow(weight/maxError,4); }
    }
    auto& rhs=heights;
    double minResidual=std::numeric_limits<double>::infinity(), maxResidual=-minResidual;
    for (const auto& p:points) {
        const double t=std::clamp((p.x-lo)/(hi-lo)*(count-1),0.0,double(count-1));
        const int i=std::min(count-2,int(t)); const double f=t-i;
        const double residual=p.z-rhs[i]*(1-f)-rhs[i+1]*f;
        minResidual=std::min(minResidual,residual); maxResidual=std::max(maxResidual,residual);
    }
    for (auto& height:rhs) { height+=(minResidual+maxResidual)*.5; }
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
std::vector<Point2> FlattenLoop(const GptApproxPanel& panel, const std::vector<Vector3>& loop,double slope)
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
        for (double t:parameters) {
            const auto local=a+(b-a)*t;
            auto p=Flatten(panel,local);
            const double fitted=Dot(panel.Point(p,1)-panel.origin,panel.normal);
            p.v=local.y-slope*local.z+slope*fitted;
            if (out.empty() || std::hypot(p.u-out.back().u,p.v-out.back().v)>1e-9) { out.push_back(p); }
        }
        a=b;
    }
    if (out.size()>1 && std::hypot(out.front().u-out.back().u,out.front().v-out.back().v)<1e-9) { out.pop_back(); }
    return out;
}
bool ValidPattern(const PatternPanel& pattern)
{
    if (pattern.outline.size()<3 || geometry::HasSelfIntersection(pattern.outline,1e-8)) { return false; }
    for (std::size_t i=0;i<pattern.openings.size();++i) {
        const auto& hole=pattern.openings[i];
        if (hole.size()<3 || geometry::HasSelfIntersection(hole,1e-8)
            || geometry::LoopsIntersect(pattern.outline,hole,1e-8)) { return false; }
        for (const auto& p:hole) { if (!geometry::ContainsPoint(pattern.outline,p)) { return false; } }
        for (std::size_t j=0;j<i;++j) {
            const auto& other=pattern.openings[j];
            if (geometry::LoopsIntersect(hole,other,1e-8) || geometry::ContainsPoint(hole,other.front())
                || geometry::ContainsPoint(other,hole.front())) { return false; }
        }
    }
    return true;
}
std::pair<double,double> MeasureSeam(GptApproxResult& result,const GptApproxPanel& panel,
    const std::vector<Vector3>& loop,const GptApproxPanel& frame,double cut)
{
    if (result.panels.empty()) { return {0,0}; }
    double low=std::numeric_limits<double>::infinity(),high=-low;
    const auto& previous=result.panels.back();
    const auto map=[&](const GptApproxPanel& p,Vector3 world) {
        const auto d=world-p.origin;
        auto flat=Flatten(p,{Dot(d,p.u),Dot(d,p.v),Dot(d,p.normal)});
        auto fitted=p.Point(flat,1);
        flat.v+=(cut-Dot(fitted-frame.origin,frame.v))/Dot(p.v,frame.v);
        return p.Point(flat,1);
    };
    auto a=loop.back();
    for (const auto& b:loop) {
        if (std::abs(a.y-cut)<1e-7 && std::abs(b.y-cut)<1e-7) {
            std::vector<double> positions{0,1};
            if (std::abs(b.x-a.x)>1e-12) {
                for (const auto* section:{&panel.profile,&previous.profile}) {
                    for (const auto& knot:*section) {
                        const double t=(knot.u-a.x)/(b.x-a.x);
                        if (t>0 && t<1) { positions.push_back(t); }
                    }
                }
            }
            for (double t:positions) {
                const auto p=a+(b-a)*t;
                const auto world=frame.origin+frame.u*p.x+frame.v*p.y+frame.normal*p.z;
                const auto difference=map(previous,world)-map(panel,world);
                const double gap=Dot(difference,frame.normal);
                if ((difference-frame.normal*gap).Length()>1e-6) {
                    low=-std::numeric_limits<double>::infinity(); high=-low; result.seamGapMm=high;
                }
                low=std::min(low,gap); high=std::max(high,gap);
                result.seamGapMm=std::max(result.seamGapMm,std::abs(gap));
            }
        }
        a=b;
    }
    return {low,high};
}
double RulingSlope(const GptApproxPanel& frame,const std::vector<Vector3>& points)
{
    double mean=0; for (const auto& p:points) { mean+=p.y; } mean/=points.size();
    double denominator=0; for (const auto& p:points) { denominator+=(p.y-mean)*(p.y-mean); }
    if (denominator<1e-12) { return 0; }
    double slope=0;
    for (int iteration=0;iteration<24;++iteration) {
        auto adjusted=points;
        for (auto& p:adjusted) { p.z-=slope*(p.y-mean); }
        auto panel=frame;
        if (!Fit(panel,adjusted)) { return slope; }
        double covariance=0;
        for (const auto& p:adjusted) {
            const double fitted=Dot(panel.Point(Flatten(panel,p),1)-panel.origin,panel.normal);
            covariance+=(p.y-mean)*(p.z-fitted);
        }
        const double change=covariance/denominator; slope+=change;
        if (std::abs(change)<1e-7) { break; }
    }
    return slope;
}
struct PanelBounds { double minError=0,maxError=0,sumError=0,slope=0; };
bool BalancePanels(GptApproxResult& result,const std::vector<PanelBounds>& bounds,
    const std::vector<std::pair<double,double>>& seams,double tolerance)
{
    std::vector<std::pair<double,double>> allowed;
    for (std::size_t i=0;i<bounds.size();++i) {
        double low=bounds[i].maxError-tolerance,high=bounds[i].minError+tolerance;
        if (i) {
            if (!std::isfinite(seams[i].first+seams[i].second)) { return false; }
            low=std::max(low,allowed.back().first+seams[i].second-tolerance);
            high=std::min(high,allowed.back().second+seams[i].first+tolerance);
        }
        if (low>high) { return false; } allowed.push_back({low,high});
    }
    std::vector<double> offsets(bounds.size(),0);
    for (std::size_t reverse=bounds.size();reverse>0;--reverse) {
        const auto i=reverse-1; auto [low,high]=allowed[i];
        if (i+1<bounds.size()) {
            low=std::max(low,offsets[i+1]-seams[i+1].first-tolerance);
            high=std::min(high,offsets[i+1]-seams[i+1].second+tolerance);
        }
        if (low>high+1e-9) { return false; }
        offsets[i]=std::clamp(0.0,low,std::max(low,high));
    }
    result.maximumMm=0; result.seamGapMm=0; double square=0; std::size_t samples=0;
    for (std::size_t i=0;i<bounds.size();++i) {
        auto& panel=result.panels[i]; const double delta=offsets[i];
        const double shift=delta/std::sqrt(1+bounds[i].slope*bounds[i].slope);
        for (auto& p:panel.profile) { p.v+=shift; }
        for (auto& p:panel.pattern.outline) { p.v+=bounds[i].slope*shift; }
        for (auto& hole:panel.pattern.openings) { for (auto& p:hole) { p.v+=bounds[i].slope*shift; } }
        panel.maximumMm=std::max(std::abs(bounds[i].minError-delta),std::abs(bounds[i].maxError-delta));
        panel.squaredMm=std::max(0.0,panel.squaredMm-2*delta*bounds[i].sumError+panel.sampleCount*delta*delta);
        result.maximumMm=std::max(result.maximumMm,panel.maximumMm); square+=panel.squaredMm; samples+=panel.sampleCount;
        if (i) { result.seamGapMm=std::max({result.seamGapMm,
            std::abs(seams[i].first+offsets[i-1]-delta),std::abs(seams[i].second+offsets[i-1]-delta)}); }
    }
    result.rmsMm=std::sqrt(square/std::max(std::size_t(1),samples));
    return true;
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
    std::vector<PanelBounds> bounds; std::vector<std::pair<double,double>> seams;
    for (int index=0;index<count;++index) {
        const double a=low+(high-low)*index/count, b=low+(high-low)*(index+1)/count;
        auto loop=Clip(Clip(boundary,a,true),b,false);
        const auto seamLoop=loop;
        if (loop.size()<3) { return false; }
        std::vector<Vector3> inside;
        for (const auto& p:samples) { if (p.y>=a-1e-8 && p.y<=b+1e-8) { inside.push_back(p); } }
        inside.insert(inside.end(),loop.begin(),loop.end());
        if (inside.size()<3) { return false; }
        auto panel=frame;
        if (!Fit(panel,inside)) { return false; }
        const double slope=RulingSlope(frame,inside);
        const double scale=std::sqrt(1+slope*slope);
        const auto tilted=[&](Vector3 p) { return Vector3{p.x,(p.y+slope*p.z)/scale,(p.z-slope*p.y)/scale}; };
        panel.v=(frame.v+frame.normal*slope)*(1/scale);
        panel.normal=(frame.normal-frame.v*slope)*(1/scale);
        panel.profile.clear(); panel.lengths.clear();
        for (auto& p:inside) { p=tilted(p); }
        for (auto& p:loop) { p=tilted(p); }
        if (!Fit(panel,inside,10)) { return false; }
        panel.pattern.outline=FlattenLoop(panel,loop,slope);
        for (const auto& hole:source.holes) {
            std::vector<Vector3> h; double minY=high, maxY=low;
            for (const auto& p:hole) { h.push_back(local(p)); minY=std::min(minY,h.back().y); maxY=std::max(maxY,h.back().y); }
            if (maxY<a || minY>b) { continue; }
            // A seam must not turn a hole into a closed opening outside a panel.
            if (minY<a || maxY>b) { return false; }
            for (auto& p:h) { p=tilted(p); }
            panel.pattern.openings.push_back(FlattenLoop(panel,h,slope));
            inside.insert(inside.end(),h.begin(),h.end());
        }
        if (!ValidPattern(panel.pattern)) { return false; }
        PanelBounds bound{std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),0,slope};
        for (const auto& p:inside) {
            const auto flat=Flatten(panel,p);
            const double signedError=(p.z-Dot(panel.Point(flat,1)-panel.origin,panel.normal))*scale;
            bound.minError=std::min(bound.minError,signedError); bound.maxError=std::max(bound.maxError,signedError);
            bound.sumError+=signedError; const double error=std::abs(signedError);
            panel.maximumMm=std::max(panel.maximumMm,error); panel.squaredMm+=error*error; ++panel.sampleCount;
        }
        panel.pattern.panelId="GPT 部材"+std::to_string(index+1);
        result.maximumMm=std::max(result.maximumMm,panel.maximumMm);
        square+=panel.squaredMm; total+=panel.sampleCount;
        seams.push_back(MeasureSeam(result,panel,seamLoop,frame,a)); bounds.push_back(bound);
        result.panels.push_back(std::move(panel));
    }
    result.rmsMm=std::sqrt(square/std::max(std::size_t(1),total));
    (void)BalancePanels(result,bounds,seams,options.toleranceMm*(1-1e-8));
    result.reached=result.maximumMm<=options.toleranceMm && result.seamGapMm<=options.toleranceMm;
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
    const auto finite=[](const std::vector<Vector3>& points) {
        return std::all_of(points.begin(),points.end(),[](Vector3 p) {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        });
    };
    if (!finite(source.samples) || !finite(source.boundary)
        || std::any_of(source.holes.begin(),source.holes.end(),[&](const auto& h) { return h.size()<3 || !finite(h); })) {
        return Out::Failure(base::MakeError("GPT-F001","面の標本または輪郭が不正です。","元の面を確認してください。"));
    }
    const auto plane=geometry::FitPlane(source.samples);
    if (!plane.valid) { return Out::Failure(base::MakeError("GPT-F002","面の方向を決められません。","面を小さい領域に分けてください。")); }
    GptApproxResult best; best.maximumMm=std::numeric_limits<double>::infinity();
    for (int count=1;count<=options.maximumPanels;++count) {
        for (int direction=0;direction<(options.direction==2 ? 4 : 2);++direction) {
            if (options.direction!=2 && options.direction!=direction) { continue; }
            GptApproxPanel frame; frame.origin=plane.origin; frame.normal=plane.normal;
            Vector3 reference=std::abs(plane.normal.x)<.8 ? Vector3{1,0,0} : Vector3{0,1,0};
            frame.u=Normalized(reference-plane.normal*Dot(reference,plane.normal));
            frame.v=Normalized(Cross(plane.normal,frame.u));
            const double angles[]{0,1.5707963267948966,.7853981633974483,2.356194490192345};
            const auto oldU=frame.u, oldV=frame.v;
            frame.u=oldU*std::cos(angles[direction])+oldV*std::sin(angles[direction]);
            frame.v=oldV*std::cos(angles[direction])-oldU*std::sin(angles[direction]);
            GptApproxResult candidate; candidate.direction=direction;
            if (!BuildCandidate(source,options,frame,count,candidate)) { continue; }
            if ((candidate.reached && !best.reached) || (candidate.reached==best.reached
                && std::max(candidate.maximumMm,candidate.seamGapMm)<std::max(best.maximumMm,best.seamGapMm))) { best=std::move(candidate); }
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
