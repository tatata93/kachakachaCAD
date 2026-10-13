#include "kachakacha/kernel/OcctProjectionVisibility.h"
#include "kachakacha/geometry/WireEdit.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace kachakacha::v2::kernel::detail {
namespace {
using geometry::CurveSegment;
using Run=std::vector<CurveSegment>;
struct Candidate {Run curves;double first=0,last=0;};
double Parameter(const CurveSegment& curve,double sourceParameter,const ProjectionSourceMatch& match) {
    double a=0,b=1;
    for(int i=0;i<48;++i){const double t=(a+b)/2;if(match(curve.Evaluate(t)).parameter<sourceParameter)a=t;else b=t;}
    return (a+b)/2;
}
CurveSegment Trim(const CurveSegment& curve,double a,double b) {
    auto kept=curve;
    if(b<1-1e-12){const auto split=kept.Split(b);if(!split.HasValue())throw std::runtime_error(split.FirstMessageJa());kept=*split.Value().first;}
    if(a>1e-12){const auto split=kept.Split(a/b);if(!split.HasValue())throw std::runtime_error(split.FirstMessageJa());kept=*split.Value().second;}
    return kept;
}
Run Slice(const Candidate& candidate,double first,double last,const ProjectionSourceMatch& match) {
    Run output;
    for(const auto& curve:candidate.curves) {
        const double a=match(curve.StartPoint()).parameter,b=match(curve.EndPoint()).parameter;
        if(last<a+1e-10||first>b-1e-10)continue;
        const double low=first<=a+1e-10?0:Parameter(curve,first,match);
        const double high=last>=b-1e-10?1:Parameter(curve,last,match);
        if(high>low+1e-10)output.push_back(Trim(curve,low,high));
    }
    return output;
}
double Distance(const Candidate& candidate,double sourceParameter,const ProjectionSourceMatch& match) {
    for(const auto& curve:candidate.curves) {
        const double a=match(curve.StartPoint()).parameter,b=match(curve.EndPoint()).parameter;
        if(sourceParameter>=a-1e-9&&sourceParameter<=b+1e-9)
            return match(curve.Evaluate(Parameter(curve,sourceParameter,match))).distance;
    }
    return 1e100;
}
std::vector<Candidate> Candidates(const WireProjectionResult& result,const ProjectionSourceMatch& match) {
    std::vector<Candidate> candidates;
    for(auto run:result.wires) {
        if(run.empty())continue;
        if(match(run.front().StartPoint()).parameter>match(run.back().EndPoint()).parameter) {
            std::reverse(run.begin(),run.end());for(auto& curve:run){const auto r=geometry::ReverseCurve(curve);if(!r.HasValue())throw std::runtime_error(r.FirstMessageJa());curve=r.Value();}
        }
        const double first=match(run.front().StartPoint()).parameter,last=match(run.back().EndPoint()).parameter;
        if(last-first<1e-10)continue;
        // A folded or multi-valued projection is not silently assigned an arbitrary order.
        for(const auto& curve:run) {
            double previous=match(curve.StartPoint()).parameter;
            for(int k=1;k<=16;++k){const double next=match(curve.Evaluate(k/16.0)).parameter;
                if(next<previous-1e-6)throw std::runtime_error("投影が折り返しています。面を限定するか全交点を指定してください。");previous=next;}
        }
        candidates.push_back({std::move(run),first,last});
    }
    return candidates;
}
}
void SelectVisibleProjection(WireProjectionResult& result,const ProjectionSourceMatch& match,
    const WireProjectionOptions& options) {
    if(result.wires.size()<2)return;
    const auto candidates=Candidates(result,match);std::vector<double> cuts;
    for(const auto& candidate:candidates){cuts.push_back(candidate.first);cuts.push_back(candidate.last);}
    std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end(),[](double a,double b){return std::abs(a-b)<1e-9;}),cuts.end());
    std::vector<Run> selected;
    for(std::size_t i=1;i<cuts.size();++i) {
        const double first=cuts[i-1],last=cuts[i];if(last-first<1e-9)continue;
        std::vector<std::size_t> active;
        for(std::size_t j=0;j<candidates.size();++j)if(candidates[j].first<=first+1e-8&&candidates[j].last>=last-1e-8)active.push_back(j);
        if(active.empty())continue;
        const auto winner=[&](double t){std::size_t chosen=active.front();double best=Distance(candidates[chosen],t,match);
            for(const auto j:active){const double value=Distance(candidates[j],t,match);if(value+options.toleranceMm*.01<best){best=value;chosen=j;}}return chosen;};
        const auto emitSlice=[&](std::size_t index,double a,double b){auto run=Slice(candidates[index],a,b,match);if(!run.empty())selected.push_back(std::move(run));};
        std::function<void(double,double,int)> resolve;
        resolve=[&](double a,double b,int depth){const auto middle=winner((a+b)*.5);bool same=true;
            for(int k=0;k<=8;++k){const double t=a+(b-a)*(k+.01)/8.02;if(winner(t)!=middle){same=false;break;}}
            if(same||depth>=24||b-a<1e-8){emitSlice(middle,a,b);return;}
            resolve(a,(a+b)*.5,depth+1);resolve((a+b)*.5,b,depth+1);
        };
        if(active.size()==1)emitSlice(active.front(),first,last);else resolve(first,last,0);
    }
    result.wires=std::move(selected);
}
}
