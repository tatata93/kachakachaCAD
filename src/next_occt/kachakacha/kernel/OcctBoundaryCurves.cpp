#include "kachakacha/kernel/OcctBoundaryCurves.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/geometry/WireEdit.h"
#include <BRep_Tool.hxx>
#include <GeomConvert.hxx>
#include <GeomConvert_BSplineCurveToBezierCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom_Curve.hxx>
#include <NCollection_Array1.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_XYZ.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace kachakacha::v2::kernel::detail {
namespace {
using Polygon=std::array<gp_Pnt,4>;
using Bezier=occ::handle<Geom_BezierCurve>;
struct Piece { double first,last;Bezier curve; };
double Choose(int n,int k) { double v=1;for(int i=1;i<=k;++i)v=v*(n-i+1)/i;return v; }
std::pair<Polygon,Polygon> SplitPolygon(Polygon p,double t) {
    Polygon left,right;left[0]=p[0];right[3]=p[3];
    for(int level=1;level<=3;++level) {
        for(int i=0;i<4-level;++i)p[i]=gp_Pnt(p[i].XYZ()*(1-t)+p[i+1].XYZ()*t);
        left[level]=p[0];right[3-level]=p[3-level];
    }
    return {left,right};
}
Polygon Restrict(const Polygon& p,double a,double b) {
    const auto left=SplitPolygon(p,b).first;
    return a==0?left:SplitPolygon(left,a/b).second;
}
Polygon Proposal(const std::vector<Piece>& pieces) {
    gp_Pnt a,b;gp_Vec da,db;
    pieces.front().curve->D1(0,a,da);pieces.back().curve->D1(1,b,db);
    const double length=pieces.back().last-pieces.front().first;
    return {a,a.Translated(da*(length/(pieces.front().last-pieces.front().first)/3)),
        b.Translated(db*(-length/(pieces.back().last-pieces.back().first)/3)),b};
}
double DeviationBound(const Bezier& native,const Polygon& cubic) {
    const int degree=native->Degree();double minimum=std::numeric_limits<double>::infinity(),bound=0,scale=1;
    for(const auto& point:cubic)scale=std::max(scale,point.XYZ().Modulus());
    for(int i=1;i<=native->NbPoles();++i) {
        minimum=std::min(minimum,native->Weight(i));scale=std::max(scale,native->Pole(i).XYZ().Modulus());
    }
    if(!(minimum>0))return std::numeric_limits<double>::infinity();
    // N(t)-W(t)C(t) in degree (n+3) Bernstein coefficients. Positive weights
    // bound W below by min(w), so max norm(coeff)/min(w) bounds the whole arc.
    for(int k=0;k<=degree+3;++k) {
        gp_XYZ difference(0,0,0);
        for(int j=0;j<=3;++j) {
            const int i=k-j;if(i<0||i>degree)continue;
            const double factor=Choose(degree,i)*Choose(3,j)/Choose(degree+3,k);
            difference+=(native->Pole(i+1).XYZ()-cubic[j].XYZ())*(native->Weight(i+1)*factor);
        }
        bound=std::max(bound,difference.Modulus()/minimum);
    }
    return bound+64*std::numeric_limits<double>::epsilon()*scale/minimum;
}
gp_Pnt Evaluate(Polygon p,double t) {return SplitPolygon(p,t).first.back();}
bool Fit(const std::vector<Piece>& pieces,double tolerance,int depth,BoundedBoundaryCurves& out) {
    if(depth>24||out.curves.size()>=4096)return false;
    // Projection onto a planar B-spline face may return a B-spline even for a
    // straight source. Positive rational weights keep its entire curve inside
    // the control hull; certify the line geometrically, not by sparse samples.
    const auto a=pieces.front().curve->Value(0),b=pieces.back().curve->Value(1);
    const gp_XYZ axis=b.XYZ()-a.XYZ();const double lengthSquared=axis.SquareModulus();
    if(lengthSquared>1e-20) {
        double bound=0;bool positive=true;
        for(const auto& piece:pieces)for(int i=1;i<=piece.curve->NbPoles();++i) {
            positive=positive&&piece.curve->Weight(i)>0;
            const auto p=piece.curve->Pole(i).XYZ();const double t=std::clamp((p-a.XYZ()).Dot(axis)/lengthSquared,0.0,1.0);
            bound=std::max(bound,(p-a.XYZ()-axis*t).Modulus());
        }
        if(positive&&bound<=std::min(tolerance,1e-10*std::max(1.0,std::sqrt(lengthSquared)))) {
            const auto line=geometry::CurveSegment::MakeLine(FromPoint(a),FromPoint(b));
            if(line.HasValue()){out.curves.push_back(line.Value());out.parameters.push_back({pieces.front().first,pieces.back().last});
                out.maxDeviationBoundMm=std::max(out.maxDeviationBoundMm,bound);return true;}
        }
    }
    const auto proposal=Proposal(pieces);
    const double start=pieces.front().first,length=pieces.back().last-start;
    double bound=0;
    for(const auto& piece:pieces)bound=std::max(bound,DeviationBound(piece.curve,
        Restrict(proposal,(piece.first-start)/length,(piece.last-start)/length)));
    if(bound<=tolerance) {
        std::vector<geometry::Vector3> control;for(const auto& point:proposal)control.push_back(FromPoint(point));
        const auto curve=geometry::CurveSegment::MakeCubicBezier(std::move(control));if(!curve.HasValue())return false;
        out.curves.push_back(curve.Value());out.parameters.push_back({start,start+length});
        out.maxDeviationBoundMm=std::max(out.maxDeviationBoundMm,bound);
        for(const auto& piece:pieces)for(int i=0;i<8;++i) {
            const double t=(i+.5)/8;
            const auto p=Evaluate(proposal,(piece.first+(piece.last-piece.first)*t-start)/length);
            out.squaredDeviationMm+=p.SquareDistance(piece.curve->Value(t));++out.samples;
        }
        return true;
    }
    if(pieces.size()>1) {
        const auto mid=pieces.begin()+pieces.size()/2;
        return Fit({pieces.begin(),mid},tolerance,depth+1,out)&&Fit({mid,pieces.end()},tolerance,depth+1,out);
    }
    const auto& piece=pieces.front();const double mid=(piece.first+piece.last)*.5;
    auto left=Bezier::DownCast(piece.curve->Copy()),right=Bezier::DownCast(piece.curve->Copy());left->Segment(0,.5);right->Segment(.5,1);
    return Fit({{piece.first,mid,left}},tolerance,depth+1,out)&&Fit({{mid,piece.last,right}},tolerance,depth+1,out);
}
}
base::Result<BoundedBoundaryCurves> BoundaryCurves(const TopoDS_Edge& edge,double tolerance) {
    using Out=base::Result<BoundedBoundaryCurves>;
    try {
        double first,last;auto curve=BRep_Tool::Curve(edge,first,last);if(curve.IsNull())throw Standard_Failure("Missing boundary curve");
        auto basis=curve;while(basis->IsKind(STANDARD_TYPE(Geom_TrimmedCurve)))basis=occ::handle<Geom_TrimmedCurve>::DownCast(basis)->BasisCurve();
        if(!basis->IsKind(STANDARD_TYPE(Geom_BSplineCurve))&&!basis->IsKind(STANDARD_TYPE(Geom_BezierCurve))) {
            const auto single=FromEdge(edge,tolerance);if(!single.HasValue())return Out::Failure(single.Diagnostics());
            BoundedBoundaryCurves out;out.curves={single.Value()};return Out::Success(std::move(out));
        }
        if(!std::isfinite(tolerance)||tolerance<=0)throw Standard_Failure("Invalid boundary tolerance");
        const auto spline=GeomConvert::CurveToBSplineCurve(basis);
        GeomConvert_BSplineCurveToBezierCurve split(spline,first,last,1e-12);
        NCollection_Array1<double> knots(1,split.NbArcs()+1);split.Knots(knots);
        std::vector<Piece> pieces;for(int i=1;i<=split.NbArcs();++i)pieces.push_back({knots(i),knots(i+1),split.Arc(i)});
        BoundedBoundaryCurves out;if(pieces.empty()||!Fit(pieces,tolerance,0,out))throw Standard_Failure("Boundary subdivision limit reached");
        if(edge.Orientation()==TopAbs_REVERSED) {
            std::reverse(out.curves.begin(),out.curves.end());std::reverse(out.parameters.begin(),out.parameters.end());
            for(std::size_t i=0;i<out.curves.size();++i) {
                const auto reversed=geometry::ReverseCurve(out.curves[i]);if(!reversed.HasValue())throw Standard_Failure("Boundary reversal failed");
                out.curves[i]=reversed.Value();
                std::swap(out.parameters[i].first,out.parameters[i].second);
            }
        }
        return Out::Success(std::move(out));
    }catch(const Standard_Failure& e){return Out::Failure(base::MakeError(kCurveUnsupported,"境界を許容偏差内の滑らかなワイヤーに変換できません。",e.what()));}
}
}
#endif
