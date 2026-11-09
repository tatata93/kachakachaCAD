#include "V2GeneratedCurves.h"
#include "kachakacha/geometry/WireEdit.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <numeric>

namespace {
using namespace kachakacha::v2::geometry;
using Run=std::vector<CurveSegment>;
bool Curved(const CurveSegment& c) {return c.Kind()!=CurveKind::Line;}
bool Smooth(const CurveSegment& a,const CurveSegment& b,double tolerance) {
    const auto u=a.FirstDerivative(1),v=b.FirstDerivative(0);
    return Curved(a)==Curved(b)&&Distance(a.EndPoint(),b.StartPoint())<=tolerance
        &&u.LengthSquared()>1e-24&&v.LengthSquared()>1e-24
        &&Dot(Normalized(u),Normalized(v))>1-1e-6;
}
Run Reversed(const Run& run) {
    Run out;out.reserve(run.size());
    for(auto it=run.rbegin();it!=run.rend();++it)out.push_back(ReverseCurve(*it).Value());
    return out;
}
struct End {std::size_t run;bool last;Vector3 point,outward;};
}

bool V2IsSmoothCurvedWire(const std::vector<CurveSegment>& curves) {
    if(curves.empty()||!std::all_of(curves.begin(),curves.end(),Curved))return false;
    for(std::size_t i=1;i<curves.size();++i)if(!Smooth(curves[i-1],curves[i],1e-7))return false;
    if(Distance(curves.back().EndPoint(),curves.front().StartPoint())<=1e-7
        &&!Smooth(curves.back(),curves.front(),1e-7))return false;
    return true;
}

std::vector<Run> V2GeneratedBoundaryRuns(const std::vector<Run>& edges,double modelTolerance) {
    // Never bridge a visible gap or move an endpoint. This is grouping only.
    const double tolerance=std::min(1e-7,std::max(0.0,modelTolerance));
    std::vector<Run> runs;
    for(const auto& edge:edges) {
        Run run;
        for(const auto& curve:edge) {
            if(!run.empty()&&!Smooth(run.back(),curve,tolerance)){runs.push_back(std::move(run));run={};}
            run.push_back(curve);
        }
        if(!run.empty())runs.push_back(std::move(run));
    }
    std::vector<End> ends;
    for(std::size_t i=0;i<runs.size();++i) {
        const auto& r=runs[i];
        ends.push_back({i,false,r.front().StartPoint(),-r.front().FirstDerivative(0)});
        ends.push_back({i,true,r.back().EndPoint(),r.back().FirstDerivative(1)});
    }
    const auto none=ends.size();
    std::vector<std::size_t> order(none),link(none,none),candidate(none,none),count(none);
    std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return ends[a].point.x<ends[b].point.x;});
    for(std::size_t at=0;at<order.size();++at)for(std::size_t next=at+1;next<order.size();++next) {
        const auto a=order[at],b=order[next];const auto& p=ends[a];const auto& q=ends[b];
        if(q.point.x-p.point.x>tolerance)break;
        if(p.run==q.run||Curved(runs[p.run].front())!=Curved(runs[q.run].front())
            ||Distance(p.point,q.point)>tolerance||p.outward.LengthSquared()<1e-24||q.outward.LengthSquared()<1e-24
            ||Distance(runs[p.run].front().StartPoint(),runs[p.run].back().EndPoint())<=tolerance
            ||Distance(runs[q.run].front().StartPoint(),runs[q.run].back().EndPoint())<=tolerance
            ||Dot(Normalized(p.outward),Normalized(q.outward))>=-1+1e-6)continue;
        candidate[a]=b;candidate[b]=a;++count[a];++count[b];
    }
    for(std::size_t i=0;i<none;++i)if(count[i]==1&&count[candidate[i]]==1)link[i]=candidate[i];
    std::vector<bool> used(runs.size());std::vector<Run> output;
    for(std::size_t seed=0;seed<runs.size();++seed) {
        if(used[seed])continue;
        std::deque<CurveSegment> joined(runs[seed].begin(),runs[seed].end());used[seed]=true;
        for(bool front:{false,true}) {
            auto end=seed*2+(front?0:1);
            while(link[end]!=none&&!used[ends[link[end]].run]) {
                const auto target=link[end],index=ends[target].run;
                const auto piece=ends[target].last!=front?Reversed(runs[index]):runs[index];
                if(front)joined.insert(joined.begin(),piece.begin(),piece.end());
                else joined.insert(joined.end(),piece.begin(),piece.end());
                used[index]=true;end=target^1;
            }
        }
        output.push_back(V2CompactGeneratedCurves(Run(joined.begin(),joined.end())));
    }
    return output;
}
