#include "kachakacha/kernel/OcctThickenSource.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <BRepAdaptor_Curve2d.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace kachakacha::v2::kernel::detail {
namespace {
using Intervals = std::vector<std::pair<double,double>>;
struct Domain { double u0,u1,v0,v1; };

bool CoversOnce(Intervals spans, double first, double last)
{
    const double epsilon = std::max(1e-12, (last-first)*1e-12);
    std::sort(spans.begin(),spans.end());
    double end=first;
    for(const auto& [a,b]:spans) {
        if(std::abs(a-end)>epsilon || !(b>a))return false;
        end=b;
    }
    return !spans.empty() && std::abs(end-last)<=epsilon;
}

bool CompleteRectangle(const TopoDS_Face& face, const Domain& d)
{
    int wires=0;for(TopExp_Explorer e(face,TopAbs_WIRE);e.More();e.Next())++wires;
    if(wires!=1)return false;
    std::array<Intervals,4> sides;
    const double eu=std::max(1e-12,(d.u1-d.u0)*1e-12);
    const double ev=std::max(1e-12,(d.v1-d.v0)*1e-12);
    for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()) {
        const auto edge=TopoDS::Edge(e.Current());
        if(BRep_Tool::Degenerated(edge))return false;
        BRepAdaptor_Curve2d curve(edge,face);
        // A bounding box, equal area or sampled points cannot prove a trim.
        // An affine pcurve stays on its complete side, including between nodes.
        if(curve.GetType()!=GeomAbs_Line)return false;
        const auto a=curve.Value(curve.FirstParameter()),b=curve.Value(curve.LastParameter());
        const bool vertical=std::abs(a.X()-b.X())<=eu;
        const bool horizontal=std::abs(a.Y()-b.Y())<=ev;
        int side=-1;
        if(vertical && std::abs(a.X()-d.u0)<=eu)side=0;
        else if(vertical && std::abs(a.X()-d.u1)<=eu)side=1;
        else if(horizontal && std::abs(a.Y()-d.v0)<=ev)side=2;
        else if(horizontal && std::abs(a.Y()-d.v1)<=ev)side=3;
        if(side<0)return false;
        const double x=side<2?a.Y():a.X(),y=side<2?b.Y():b.X();
        sides[side].emplace_back(std::min(x,y),std::max(x,y));
    }
    return CoversOnce(sides[0],d.v0,d.v1) && CoversOnce(sides[1],d.v0,d.v1)
        && CoversOnce(sides[2],d.u0,d.u1) && CoversOnce(sides[3],d.u0,d.u1);
}

}

std::optional<TopoDS_Shape> CanonicalRectangularThickenFace(const TopoDS_Shape& source,
    double tolerance)
{
    try {
        TopExp_Explorer faces(source,TopAbs_FACE);if(!faces.More())return std::nullopt;
        const auto face=TopoDS::Face(faces.Current());faces.Next();if(faces.More())return std::nullopt;
        const auto surface=occ::handle<Geom_BSplineSurface>::DownCast(BRep_Tool::Surface(face));
        if(surface.IsNull() || surface->IsUPeriodic() || surface->IsVPeriodic()
            || !BRepCheck_Analyzer(face).IsValid())return std::nullopt;
        Domain d;BRepTools::UVBounds(face,d.u0,d.u1,d.v0,d.v1);
        if(!std::isfinite(d.u0+d.u1+d.v0+d.v1) || !(d.u1>d.u0) || !(d.v1>d.v0)
            || !CompleteRectangle(face,d))return std::nullopt;
        auto copy=occ::handle<Geom_BSplineSurface>::DownCast(surface->Copy());
        copy->Segment(d.u0,d.u1,d.v0,d.v1);
        BRepBuilderAPI_MakeFace maker(copy,Precision::Confusion());if(!maker.IsDone())return std::nullopt;
        auto result=maker.Face();result.Orientation(face.Orientation());
        if(!BRepCheck_Analyzer(result).IsValid())return std::nullopt;
        for(TopExp_Explorer e(result,TopAbs_EDGE);e.More();e.Next())
            if(BRep_Tool::Tolerance(TopoDS::Edge(e.Current()))>tolerance)return std::nullopt;
        for(TopExp_Explorer e(result,TopAbs_VERTEX);e.More();e.Next())
            if(BRep_Tool::Tolerance(TopoDS::Vertex(e.Current()))>tolerance)return std::nullopt;
        return result;
    } catch(const Standard_Failure&) { return std::nullopt; }
}

bool ThickeningContainsSourceBounds(const TopoDS_Shape& source, const TopoDS_Shape& wall,
    double tolerance)
{
    try {
        Bnd_Box a,b;
        BRepBndLib::AddOptimal(source,a,false,false);
        BRepBndLib::AddOptimal(wall,b,false,false);
        if(a.IsVoid() || b.IsVoid() || a.IsOpen() || b.IsOpen())return false;
        double ax,ay,az,aX,aY,aZ,bx,by,bz,bX,bY,bZ;
        a.Get(ax,ay,az,aX,aY,aZ);b.Get(bx,by,bz,bX,bY,bZ);
        return ax>=bx-tolerance && ay>=by-tolerance && az>=bz-tolerance
            && aX<=bX+tolerance && aY<=bY+tolerance && aZ<=bZ+tolerance;
    } catch(const Standard_Failure&) { return false; }
}
}
#endif
