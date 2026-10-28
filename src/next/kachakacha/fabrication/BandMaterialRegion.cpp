#include "kachakacha/fabrication/BandMaterialRegion.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <cstdint>

namespace kachakacha::v2::fabrication {
namespace {
using Loop=std::vector<Point2>;
constexpr double eps=1e-9;
double Dist(Point2 a,Point2 b){return std::hypot(a.u-b.u,a.v-b.v);}
Point2 Mix(Point2 a,Point2 b,double t){return {a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t};}
double Area(const Loop& p){double s=0;for(std::size_t i=0;i<p.size();++i){auto a=p[i],b=p[(i+1)%p.size()];s+=a.u*b.v-b.u*a.v;}return s*.5;}
bool Inside(const Loop& loop,Point2 p){bool in=false;for(std::size_t i=0,j=loop.size()-1;i<loop.size();j=i++) {
    const auto a=loop[j],b=loop[i];if((a.v>p.v)!=(b.v>p.v)&&p.u<(b.u-a.u)*(p.v-a.v)/(b.v-a.v)+a.u)in=!in;
}return in;}
bool Material(const std::vector<Loop>& loops,Point2 p){
    if(!Inside(loops.front(),p))return false;
    for(std::size_t i=1;i<loops.size();++i)if(Inside(loops[i],p))return false;
    return true;
}
Point2 Coordinates(Point2 p,BandSplitAxis axis){return axis==BandSplitAxis::U?p:Point2{p.v,p.u};}
bool BoundariesValid(const std::vector<Loop>& loops){
    struct Segment{Point2 a,b;std::size_t loop,index,count;double minX,maxX,minY,maxY;};
    std::vector<Segment> segments;
    for(std::size_t k=0;k<loops.size();++k)for(std::size_t i=0;i<loops[k].size();++i){auto a=loops[k][i],b=loops[k][(i+1)%loops[k].size()];
        segments.push_back({a,b,k,i,loops[k].size(),std::min(a.u,b.u),std::max(a.u,b.u),std::min(a.v,b.v),std::max(a.v,b.v)});}
    std::sort(segments.begin(),segments.end(),[](const auto& a,const auto& b){return a.minX<b.minX;});
    const auto cross=[](Point2 a,Point2 b,Point2 c){return (b.u-a.u)*(c.v-a.v)-(b.v-a.v)*(c.u-a.u);};
    for(std::size_t i=0;i<segments.size();++i)for(std::size_t j=i+1;j<segments.size()&&segments[j].minX<=segments[i].maxX+eps;++j){
        const auto& a=segments[i];const auto& b=segments[j];
        if(a.maxY<b.minY-eps||b.maxY<a.minY-eps)continue;
        if(a.loop==b.loop&&((a.index+1)%a.count==b.index||(b.index+1)%b.count==a.index))continue;
        const double c1=cross(a.a,a.b,b.a),c2=cross(a.a,a.b,b.b),c3=cross(b.a,b.b,a.a),c4=cross(b.a,b.b,a.b);
        const double e=eps*std::max(Dist(a.a,a.b),Dist(b.a,b.b));
        if(((c1>e&&c2>e)||(c1<-e&&c2<-e))||((c3>e&&c4>e)||(c3<-e&&c4<-e)))continue;
        return false;
    }
    return true;
}
}
base::Result<std::vector<BandMaterialRegion>> ClipBandMaterial(
    const std::vector<Loop>& input,BandSplitAxis axis,double low,double high)
{
    using Out=base::Result<std::vector<BandMaterialRegion>>;
    const auto fail=[](){return Out::Failure(base::MakeError("FAB-O001",
        "材料の外周と穴を帯の範囲へ写せません。","境界の閉鎖、自己交差、接点を確認してください。"));};
    if(input.empty()||!std::isfinite(low)||!std::isfinite(high)||high-low<eps)return fail();
    std::vector<Loop> loops;std::size_t total=0;
    for(std::size_t i=0;i<input.size();++i){
        total+=input[i].size();if(total>20000)return fail();
        Loop loop;for(auto p:input[i]){
            if(!std::isfinite(p.u)||!std::isfinite(p.v)||std::abs(p.u)>1e6||std::abs(p.v)>1e6)return fail();
            p=Coordinates(p,axis);if(loop.empty()||Dist(loop.back(),p)>eps)loop.push_back(p);
        }
        if(loop.size()>1&&Dist(loop.front(),loop.back())<eps)loop.pop_back();
        if(loop.size()<3||std::abs(Area(loop))<eps*eps)return fail();
        if((Area(loop)>0)!=(i==0))std::reverse(loop.begin(),loop.end());
        loops.push_back(std::move(loop));
    }
    if(!BoundariesValid(loops))return fail();
    for(std::size_t i=1;i<loops.size();++i){if(!Inside(loops.front(),loops[i].front()))return fail();
        for(std::size_t j=1;j<i;++j)if(Inside(loops[j],loops[i].front())||Inside(loops[i],loops[j].front()))return fail();}
    struct Edge{std::size_t a,b;bool used=false;};std::vector<Edge> edges;
    std::vector<Point2> nodes;std::vector<std::vector<std::size_t>> outgoing;
    using Key=std::pair<std::int64_t,std::int64_t>;
    std::map<Key,std::vector<std::size_t>> bins;std::set<std::pair<std::size_t,std::size_t>> edgeKeys;
    const auto node=[&](Point2 p){const Key key{std::llround(p.u/eps),std::llround(p.v/eps)};
        for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y){const auto bin=bins.find({key.first+x,key.second+y});if(bin!=bins.end())
            for(auto index:bin->second)if(Dist(nodes[index],p)<eps)return index;}
        const auto index=nodes.size();nodes.push_back(p);outgoing.emplace_back();bins[key].push_back(index);return index;
    };
    std::vector<double> cuts[2];
    const auto add=[&](Point2 a,Point2 b){
        if(Dist(a,b)<=eps)return;
        const auto from=node(a),to=node(b);if(from==to||!edgeKeys.emplace(from,to).second)return;
        outgoing[from].push_back(edges.size());edges.push_back({from,to});
    };
    for(const auto& loop:loops)for(std::size_t i=0;i<loop.size();++i){
        const auto a=loop[i],b=loop[(i+1)%loop.size()];double begin=0,end=1;
        const double dx=b.u-a.u;
        for(int k=0;k<2;++k){const double x=k?high:low;
            if(std::abs(dx)>eps){const double t=(x-a.u)/dx;if(t>=-eps&&t<=1+eps)cuts[k].push_back(Mix(a,b,std::clamp(t,0.,1.)).v);}
            else if(std::abs(a.u-x)<eps){cuts[k].push_back(a.v);cuts[k].push_back(b.v);}
        }
        if(std::abs(dx)<eps){if(a.u<low-eps||a.u>high+eps)continue;}
        else {const auto t0=(low-a.u)/dx,t1=(high-a.u)/dx;begin=std::max(0.,std::min(t0,t1));end=std::min(1.,std::max(t0,t1));}
        if(end-begin<=eps)continue;
        auto p=Mix(a,b,begin),q=Mix(a,b,end);
        if(std::abs(p.u-q.u)<eps&&(std::abs(p.u-low)<eps||std::abs(p.u-high)<eps))continue;
        add(p,q);
    }
    for(int k=0;k<2;++k){auto& c=cuts[k];std::sort(c.begin(),c.end());
        c.erase(std::unique(c.begin(),c.end(),[](double a,double b){return std::abs(a-b)<eps;}),c.end());
        const double x=k?high:low;
        for(std::size_t i=1;i<c.size();++i){
            if(c[i]-c[i-1]<eps)continue;
            // Probe strictly inside this band. Never bridge a hole across a seam.
            const double nudge=std::min((high-low)*1e-6,1e-8);
            if(!Material(loops,{x+(k?-nudge:nudge),(c[i]+c[i-1])*.5}))continue;
            Point2 a{x,c[i-1]},b{x,c[i]};if(!k)std::swap(a,b);add(a,b);
        }
    }
    std::vector<Loop> closed;
    for(std::size_t start=0;start<edges.size();++start){
        if(edges[start].used)continue;Loop loop;std::size_t at=start;
        for(std::size_t steps=0;steps<=edges.size();++steps){
            if(edges[at].used)return fail();edges[at].used=true;loop.push_back(nodes[edges[at].a]);
            const auto next=edges[at].b;if(next==edges[start].a)break;
            std::size_t found=edges.size();
            for(auto j:outgoing[next])if(!edges[j].used){
                if(found!=edges.size())return fail();found=j;
            }
            if(found==edges.size())return fail();at=found;
            if(steps==edges.size())return fail();
        }
        if(loop.size()>=3&&std::abs(Area(loop))>eps*eps)closed.push_back(std::move(loop));
    }
    if(!BoundariesValid(closed))return fail();
    std::vector<BandMaterialRegion> regions;
    for(const auto& loop:closed)if(Area(loop)>0)regions.push_back({{loop}});
    for(const auto& loop:closed)if(Area(loop)<0){
        auto found=regions.end();double smallest=std::numeric_limits<double>::infinity();
        for(auto i=regions.begin();i!=regions.end();++i)if(Inside(i->loops.front(),loop.front())&&Area(i->loops.front())<smallest){found=i;smallest=Area(i->loops.front());}
        if(found==regions.end())return fail();found->loops.push_back(loop);
    }
    for(auto& region:regions)for(auto& loop:region.loops)for(auto& p:loop)p=Coordinates(p,axis);
    return Out::Success(std::move(regions));
}
void CompactMaterialLoop(Loop& loop){
    Loop out;
    const auto redundant=[](Point2 a,Point2 b,Point2 c){const auto x=b.u-a.u,y=b.v-a.v,xx=c.u-b.u,yy=c.v-b.v;
        return x*xx+y*yy>=0&&std::abs(x*yy-y*xx)<=1e-12*std::min(std::hypot(x,y),std::hypot(xx,yy));};
    for(auto p:loop){while(out.size()>1&&redundant(out[out.size()-2],out.back(),p))out.pop_back();out.push_back(p);}
    while(out.size()>3&&redundant(out.back(),out[0],out[1]))out.erase(out.begin());
    while(out.size()>3&&redundant(out[out.size()-2],out.back(),out.front()))out.pop_back();loop=std::move(out);
}
std::vector<Vector3> MapBandMaterialLoop(const Loop& loop,BandSplitAxis axis,double low,double high,
    const std::vector<double>& longitudinal,const std::vector<std::vector<Vector3>>& rails)
{
    if(loop.size()<3||rails.size()!=2||rails[0].size()!=rails[1].size()||rails[0].size()<2||high<=low)return {};
    std::vector<double> parameters=longitudinal;
    if(parameters.size()!=rails[0].size()){parameters.clear();for(std::size_t i=0;i<rails[0].size();++i)parameters.push_back(double(i)/(rails[0].size()-1));}
    const auto map=[&](Point2 p){
        const double x=std::clamp((p.u-low)/(high-low),0.,1.);
        const double s=std::clamp(p.v,0.,1.);
        auto end=std::upper_bound(parameters.begin(),parameters.end(),s);
        const std::size_t column=std::min(parameters.size()-2,end==parameters.begin()?0:std::size_t(end-parameters.begin()-1));
        const double y=std::clamp((s-parameters[column])/(parameters[column+1]-parameters[column]),0.,1.);
        if(x+y<=1)return rails[0][column]*(1-x-y)+rails[1][column]*x+rails[0][column+1]*y;
        return rails[1][column]*(1-y)+rails[0][column+1]*(1-x)+rails[1][column+1]*(x+y-1);
    };
    std::vector<Vector3> result;
    for(std::size_t i=0;i<loop.size();++i){const auto a=Coordinates(loop[i],axis),b=Coordinates(loop[(i+1)%loop.size()],axis);
        std::vector<double> fractions{0,1};
        if(std::abs(b.v-a.v)>1e-14)for(double s:parameters){const double t=(s-a.v)/(b.v-a.v);if(t>eps&&t<1-eps)fractions.push_back(t);}
        for(std::size_t c=0;c+1<parameters.size();++c){
            const double span=parameters[c+1]-parameters[c];
            const double from=(a.u-low)/(high-low)+(a.v-parameters[c])/span-1;
            const double delta=(b.u-a.u)/(high-low)+(b.v-a.v)/span;
            if(std::abs(delta)<1e-14)continue;const double t=-from/delta;
            const double s=a.v+(b.v-a.v)*t;
            if(t>eps&&t<1-eps&&s>=parameters[c]-eps&&s<=parameters[c+1]+eps)fractions.push_back(t);
        }
        std::sort(fractions.begin(),fractions.end());fractions.erase(std::unique(fractions.begin(),fractions.end(),[](double x,double y){return std::abs(x-y)<eps;}),fractions.end());
        for(double t:fractions){if(t>=1-eps)continue;const auto p=map(Mix(a,b,t));if(result.empty()||geometry::Distance(result.back(),p)>1e-10)result.push_back(p);}
    }
    if(result.size()>1&&geometry::Distance(result.front(),result.back())<1e-10)result.pop_back();
    return result;
}
}
