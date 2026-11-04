#include "kachakacha/kernel/OcctBoundaryDistance.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <Extrema_LocateExtPC.hxx>
#include <Extrema_GenLocateExtPS.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_State.hxx>
#include <gp_Vec.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace kachakacha::v2::kernel::detail {
NativeBoundaryDistance::NativeBoundaryDistance(const TopoDS_Shape& shape) {
    TopTools_IndexedMapOfShape edges;TopExp::MapShapes(shape,TopAbs_EDGE,edges);
    for(int i=1;i<=edges.Extent();++i)try {
        Edge entry;entry.curve.Initialize(TopoDS::Edge(edges(i)));
        const double first=entry.curve.FirstParameter(),last=entry.curve.LastParameter();
        if(!std::isfinite(first)||!std::isfinite(last)||last<=first)continue;
        for(std::size_t j=0;j<entry.samples.size();++j) {
            entry.parameters[j]=first+(last-first)*j/(entry.samples.size()-1);
            entry.samples[j]=entry.curve.Value(entry.parameters[j]);
        }
        edges_.push_back(std::move(entry));
    }catch(const Standard_Failure&){} // The full finite-shape distance remains available.
    for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next())try {
        Face entry;entry.face=TopoDS::Face(e.Current());entry.surface.Initialize(entry.face,true);
        const double u0=entry.surface.FirstUParameter(),u1=entry.surface.LastUParameter();
        const double v0=entry.surface.FirstVParameter(),v1=entry.surface.LastVParameter();
        if(!std::isfinite(u0+u1+v0+v1)||u1<=u0||v1<=v0)continue;
        for(int i=0;i<9;++i)for(int j=0;j<9;++j) {
            const double u=u0+(u1-u0)*i/8,v=v0+(v1-v0)*j/8;
            entry.parameters[i*9+j]={u,v};entry.samples[i*9+j]=entry.surface.Value(u,v);
        }
        faces_.push_back(std::move(entry));
    }catch(const Standard_Failure&){}
}
std::optional<double> NativeBoundaryDistance::Coincident(const gp_Pnt& point,double precision) const {
    const Edge* closest=nullptr;std::size_t seed=0;double squared=std::numeric_limits<double>::infinity();
    for(const auto& edge:edges_)for(std::size_t j=0;j<edge.samples.size();++j) {
        const double d=point.SquareDistance(edge.samples[j]);
        if(d<squared){squared=d;closest=&edge;seed=j;}
    }
    if(closest&&squared<=precision*precision)return std::sqrt(squared);
    if(closest)try {
        const auto& curve=closest->curve;
        Extrema_LocateExtPC locate(point,curve,closest->parameters[seed],1e-12);
        if(locate.IsDone()) {
        // Reevaluate inside the actual trim interval. A converged extremum may
        // be local; only its proven upper bound is used, never global minimality.
        const double u=std::clamp(locate.Point().Parameter(),curve.FirstParameter(),curve.LastParameter());
        const double distance=point.Distance(curve.Value(u));
        if(std::isfinite(distance)&&distance<=precision)return distance;
        }
    }catch(const Standard_Failure&){}
    for(const auto& face:faces_)try {
        seed=0;squared=std::numeric_limits<double>::infinity();
        for(std::size_t i=0;i<face.samples.size();++i) {
            const double d=point.SquareDistance(face.samples[i]);if(d<squared){squared=d;seed=i;}
        }
        Extrema_GenLocateExtPS locate(face.surface,1e-12,1e-12);
        locate.Perform(point,face.parameters[seed].X(),face.parameters[seed].Y());
        if(!locate.IsDone())continue;
        double u,v;locate.Point().Parameter(u,v);
        u=std::clamp(u,face.surface.FirstUParameter(),face.surface.LastUParameter());
        v=std::clamp(v,face.surface.FirstVParameter(),face.surface.LastVParameter());
        const double distance=point.Distance(face.surface.Value(u,v));
        if(!std::isfinite(distance)||distance>precision)continue;
        const auto state=BRepClass_FaceClassifier(face.face,gp_Pnt2d(u,v),1e-12).State();
        if(state==TopAbs_IN)return distance;
        // ON is a UV tolerance classification, not an exact material point.
        // Obtain a nearby point strictly inside the trim and remeasure its
        // physical distance. Never accept a supporting-surface point in a hole.
        if(state==TopAbs_ON) {
            gp_Pnt p;gp_Vec du,dv;face.surface.D1(u,v,p,du,dv);
            const double su=du.Magnitude()>0?precision*.2/du.Magnitude():0;
            const double sv=dv.Magnitude()>0?precision*.2/dv.Magnitude():0;
            for(int i=-1;i<=1;++i)for(int j=-1;j<=1;++j) {
                const double a=std::clamp(u+i*su,face.surface.FirstUParameter(),face.surface.LastUParameter());
                const double b=std::clamp(v+j*sv,face.surface.FirstVParameter(),face.surface.LastVParameter());
                if(BRepClass_FaceClassifier(face.face,gp_Pnt2d(a,b),1e-12).State()!=TopAbs_IN)continue;
                const double upper=point.Distance(face.surface.Value(a,b));
                if(std::isfinite(upper)&&upper<=precision)return upper;
            }
        }
    }catch(const Standard_Failure&){}
    return std::nullopt;
}
}
#endif
