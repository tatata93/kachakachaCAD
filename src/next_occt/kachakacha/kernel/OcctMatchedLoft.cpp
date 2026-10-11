#include "kachakacha/kernel/OcctMatchedLoft.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <Geom_BSplineSurface.hxx>
#include <NCollection_Array2.hxx>
#include <NCollection_Array1.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopoDS_Shape.hxx>
#include <algorithm>
#include <array>
#include <cmath>
namespace kachakacha::v2::kernel::detail {
namespace {
using geometry::Vector3;
// Shape-preserving C1 derivatives on nonuniform station spacing. Work in a
// section-derived frame, so moving/rotating the inputs moves/rotates the loft.
std::vector<double> Slopes(const std::vector<double>& x,const std::vector<double>& y)
{
    const auto n=x.size();std::vector<double> h(n-1),d(n-1),m(n);
    for(std::size_t i=0;i+1<n;++i){h[i]=x[i+1]-x[i];d[i]=(y[i+1]-y[i])/h[i];}
    if(n==2)return {d[0],d[0]};
    for(std::size_t i=1;i+1<n;++i)if(d[i-1]*d[i]>0) {
        const double w1=2*h[i]+h[i-1],w2=h[i]+2*h[i-1];
        m[i]=(w1+w2)/(w1/d[i-1]+w2/d[i]);
    }
    const auto endpoint=[](double a,double b,double da,double db){
        double value=((2*a+b)*da-a*db)/(a+b);
        if(value*da<=0)return 0.0;
        return std::copysign(std::min(std::abs(value),3*std::abs(da)),da);
    };
    m[0]=endpoint(h[0],h[1],d[0],d[1]);
    m[n-1]=endpoint(h[n-2],h[n-3],d[n-2],d[n-3]);return m;
}
struct Frame {
    Vector3 origin,x,y,z;
    Vector3 Local(const Vector3& p)const {const auto v=p-origin;return {geometry::Dot(v,x),geometry::Dot(v,y),geometry::Dot(v,z)};}
    Vector3 World(const Vector3& p)const {return origin+x*p.x+y*p.y+z*p.z;}
};
}
std::optional<base::Result<TopoDS_Shape>> BuildMatchedLoft(
    const modeling::GuideSurfaceRequest& request,const std::vector<std::size_t>& order,
    const geometry::GeometryTolerance& tolerance)
{
    using Out=base::Result<TopoDS_Shape>;
    if(order.size()<2)return std::nullopt;
    std::vector<std::vector<Vector3>> rows;
    for(const auto index:order) {
        const auto& chain=request.chains[index];
        if(chain.closed||chain.segments.size()!=1
            ||chain.segments[0].Kind()!=geometry::CurveKind::CubicBezier)return std::nullopt;
        rows.push_back(chain.segments[0].ControlPoints());
    }
    const auto& first=rows.front();Frame frame;frame.origin=first[0];
    // A valid Bezier may have its first three control points on a tangent line,
    // especially after reversing the section. Use all control points to find
    // its plane; traversal direction must not select a different loft solver.
    for(std::size_t i=1;i<first.size();++i)for(std::size_t j=i+1;j<first.size();++j) {
        const auto normal=geometry::Cross(first[i]-first[0],first[j]-first[0]);
        if(normal.LengthSquared()>frame.x.LengthSquared())frame.x=normal;
    }
    if(frame.x.Length()<1e-9)return std::nullopt;
    frame.x=geometry::Normalized(frame.x);
    if(geometry::Dot(rows.back()[0]-first[0],frame.x)<0)frame.x=-frame.x;
    frame.y=geometry::Normalized(first.back()-first.front());
    const auto startTangent=first[1]-first[0],endTangent=first.back()-first[first.size()-2];
    if(startTangent.Length()>1e-9&&endTangent.Length()>1e-9
        &&std::abs(geometry::Dot(geometry::Normalized(startTangent),geometry::Normalized(endTangent)))<1e-10) {
        // Orthogonal endpoint tangents give the natural section axes. A chord
        // frame mixes width and height, allowing an eccentric tank's constant
        // width to bulge while its centre height changes. Reversal swaps these
        // axes, preserving the same component-wise monotone interpolation.
        frame.y=geometry::Normalized(startTangent);
    }
    if(frame.y.Length()<1e-9)return std::nullopt;
    frame.z=geometry::Cross(frame.x,frame.y);
    std::vector<double> stations;
    for(auto& row:rows) {
        for(auto& p:row)p=frame.Local(p);
        const double station=row[0].x;
        for(const auto& p:row)if(std::abs(p.x-station)>tolerance.modelLinearMm)return std::nullopt;
        if(!stations.empty()&&station-stations.back()<=tolerance.modelLinearMm)return std::nullopt;
        stations.push_back(station);
    }
    const int count=static_cast<int>(rows.size()),polesCount=3*(count-1)+1;
    NCollection_Array2<gp_Pnt> poles(1,4,1,polesCount);
    for(int u=0;u<4;++u) {
        std::array<std::vector<double>,3> values,slopes;
        for(const auto& row:rows){values[0].push_back(row[u].x);values[1].push_back(row[u].y);values[2].push_back(row[u].z);}
        for(int c=0;c<3;++c)slopes[c]=Slopes(stations,values[c]);
        const auto put=[&](int at,const Vector3& local){const auto p=frame.World(local);poles.SetValue(u+1,at,gp_Pnt(p.x,p.y,p.z));};
        for(int v=0;v+1<count;++v) {
            const double h=(stations[v+1]-stations[v])/3;
            const Vector3 a{slopes[0][v],slopes[1][v],slopes[2][v]};
            const Vector3 b{slopes[0][v+1],slopes[1][v+1],slopes[2][v+1]};
            put(3*v+1,rows[v][u]);put(3*v+2,rows[v][u]+a*h);
            put(3*v+3,rows[v+1][u]-b*h);put(3*v+4,rows[v+1][u]);
        }
    }
    NCollection_Array1<double> uKnots(1,2),vKnots(1,count);
    NCollection_Array1<int> uMult(1,2),vMult(1,count);
    uKnots.SetValue(1,0);uKnots.SetValue(2,1);uMult.Init(4);
    for(int v=1;v<=count;++v){vKnots.SetValue(v,stations[v-1]);vMult.SetValue(v,v==1||v==count?4:3);}
    const occ::handle<Geom_BSplineSurface> surface=new Geom_BSplineSurface(poles,uKnots,vKnots,uMult,vMult,3,3);
    for(int v=count-1;v>=2;--v) {
        if(!surface->RemoveVKnot(v,2,tolerance.modelLinearMm))return Out::Failure(
            base::MakeError("KER-S001","ロフトの断面間で接線が連続しません。","断面の間隔と曲線の対応を確認してください。"));
    }
    BRepBuilderAPI_MakeFace made(surface,tolerance.modelLinearMm);
    if(!made.IsDone()||!BRepCheck_Analyzer(made.Shape()).IsValid())return Out::Failure(
        base::MakeError("KER-S001","断面間の膨らみを抑えたロフトを作れません。","断面の対応と間隔を確認してください。"));
    return Out::Success(made.Shape());
}
}
#endif
