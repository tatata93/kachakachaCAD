#include "kachakacha/kernel/OcctCorrectedThicken.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include "kachakacha/kernel/OcctMassProperties.h"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepAdaptor_Curve2d.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GeomConvert.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <limits>
#include <exception>
#include <utility>

namespace kachakacha::v2::kernel::detail {
namespace {
using Surface = occ::handle<Geom_BSplineSurface>;
using Out = base::Result<ThickenedSolid>;
struct Domain { double u0,u1,v0,v1; };
Out Refused(const char* reason) {
    return Out::Failure(base::MakeError(kThickenFailed,"反対面を補正しても、安全な厚みを作れませんでした。",reason));
}
bool Rectangle(const TopoDS_Face& face,const Domain& d) {
    int wires=0;for(TopExp_Explorer e(face,TopAbs_WIRE);e.More();e.Next())++wires;
    if(wires!=1)return false;
    for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()) {
        const auto edge=TopoDS::Edge(e.Current());if(BRep_Tool::Degenerated(edge))return false;
        BRepAdaptor_Curve2d c(edge,face);if(c.GetType()!=GeomAbs_Line)return false;
        const auto a=c.Value(c.FirstParameter()),b=c.Value(c.LastParameter());
        const double tu=std::max(1e-12,(d.u1-d.u0)*1e-10),tv=std::max(1e-12,(d.v1-d.v0)*1e-10);
        const bool u=std::abs(a.X()-b.X())<tu&&(std::abs(a.X()-d.u0)<tu||std::abs(a.X()-d.u1)<tu);
        const bool v=std::abs(a.Y()-b.Y())<tv&&(std::abs(a.Y()-d.v0)<tv||std::abs(a.Y()-d.v1)<tv);
        if(!u&&!v)return false;
    }
    return true;
}
std::vector<double> Greville(const NCollection_Array1<double>& knots,int degree,int poles) {
    std::vector<double> values;
    for(int i=0;i<poles;++i) {
        double sum=0;for(int k=1;k<=degree;++k)sum+=knots(knots.Lower()+i+k);
        values.push_back(sum/degree);
    }
    return values;
}
gp_Vec Normal(const Surface& surface,const Domain& d,double u,double v,double sign,gp_Pnt& p) {
    gp_Vec du,dv;surface->D1(u,v,p,du,dv);auto n=du.Crossed(dv);
    if(n.SquareMagnitude()<1e-24) {
        gp_Pnt unused;surface->D1(std::clamp(u,d.u0+(d.u1-d.u0)*1e-7,d.u1-(d.u1-d.u0)*1e-7),
            std::clamp(v,d.v0+(d.v1-d.v0)*1e-7,d.v1-(d.v1-d.v0)*1e-7),unused,du,dv);
        n=du.Crossed(dv);
    }
    if(n.SquareMagnitude()<1e-24)throw Standard_Failure("Undefined source normal");
    n.Normalize();return n*sign;
}
using Cell = std::tuple<long long,long long,long long>;
Cell CellOf(const gp_Pnt& p,double size) {
    return {static_cast<long long>(std::floor(p.X()/size)),static_cast<long long>(std::floor(p.Y()/size)),
        static_cast<long long>(std::floor(p.Z()/size))};
}
std::vector<gp_Vec> SmoothedNormals(const Surface& base,const Domain& d,double sign,double radius) {
    struct Node { gp_Pnt point;gp_Vec normal; };
    std::vector<Node> nodes;std::map<Cell,std::vector<std::size_t>> cells;
    const auto us=Greville(base->UKnotSequence(),base->UDegree(),base->NbUPoles());
    const auto vs=Greville(base->VKnotSequence(),base->VDegree(),base->NbVPoles());
    for(double u:us)for(double v:vs) {
        gp_Pnt p;const auto n=Normal(base,d,u,v,sign,p);cells[CellOf(p,radius*3)].push_back(nodes.size());nodes.push_back({p,n});
    }
    std::vector<gp_Vec> normals;
    for(const auto& node:nodes) {
        const auto [x,y,z]=CellOf(node.point,radius*3);gp_Vec sum(0,0,0);double weight=0;
        for(int i=-1;i<=1;++i)for(int j=-1;j<=1;++j)for(int k=-1;k<=1;++k) {
            const auto found=cells.find({x+i,y+j,z+k});if(found==cells.end())continue;
            for(const auto index:found->second) {
                const auto& other=nodes[index];const double distance=other.point.SquareDistance(node.point);
                if(distance>radius*radius*9)continue;
                const double w=std::exp(-distance/(radius*radius));sum+=other.normal*w;weight+=w;
            }
        }
        normals.push_back(sum/weight);
    }
    return normals;
}
Surface Opposite(const Surface& base,const std::vector<gp_Vec>& normals,double distance,double precision) {
    auto surface=Surface::DownCast(base->Copy());
    for(int i=1;i<=base->NbUPoles();++i)for(int j=1;j<=base->NbVPoles();++j)
        surface->SetPole(i,j,base->Pole(i,j).Translated(normals[(i-1)*base->NbVPoles()+j-1]*distance));
    // Only the new skin is simplified. Repeated removals are checked against
    // the ideal offset below; their individual tolerance is not a global bound.
    for(int i=surface->NbUKnots()-1;i>=2;--i)
        for(int m=surface->UMultiplicity(i)-1;m>=0;--m)if(!surface->RemoveUKnot(i,m,precision))break;
    for(int i=surface->NbVKnots()-1;i>=2;--i)
        for(int m=surface->VMultiplicity(i)-1;m>=0;--m)if(!surface->RemoveVKnot(i,m,precision))break;
    return surface;
}
TopoDS_Face Cap(const Surface& first,const Surface& last,const Domain& d,int side,double tolerance) {
    const bool u=side==1||side==3;const double parameter=u?(side==1?d.u1:d.u0):(side==0?d.v0:d.v1);
    auto a=occ::handle<Geom_BSplineCurve>::DownCast(u?first->UIso(parameter):first->VIso(parameter));
    auto b=occ::handle<Geom_BSplineCurve>::DownCast(u?last->UIso(parameter):last->VIso(parameter));
    const int degree=std::max(a->Degree(),b->Degree());a->IncreaseDegree(degree);b->IncreaseDegree(degree);
    const auto ak=a->Knots();const auto am=a->Multiplicities();const auto bk=b->Knots();const auto bm=b->Multiplicities();
    a->InsertKnots(bk,bm,1e-12,false);b->InsertKnots(ak,am,1e-12,false);
    if(a->NbPoles()!=b->NbPoles())throw Standard_Failure("Incompatible boundary knots");
    NCollection_Array2<gp_Pnt> poles(1,a->NbPoles(),1,2);NCollection_Array2<double> weights(1,a->NbPoles(),1,2);
    for(int i=1;i<=a->NbPoles();++i) {poles(i,1)=a->Pole(i);poles(i,2)=b->Pole(i);weights(i,1)=a->Weight(i);weights(i,2)=b->Weight(i);}
    NCollection_Array1<double> knots(1,2);knots(1)=0;knots(2)=1;NCollection_Array1<int> mults(1,2);mults.Init(2);
    const Surface surface=new Geom_BSplineSurface(poles,weights,a->Knots(),knots,a->Multiplicities(),mults,degree,1);
    return BRepBuilderAPI_MakeFace(surface,tolerance).Face();
}
bool Measure(const Surface& base,const Surface& first,const Surface& last,const Domain& d,double sign,
    double firstOffset,double lastOffset,double thickness,double tolerance,ThickenedSolid& result) {
    std::vector<double> us,vs;
    const auto locations=[](const auto& knots,double lo,double hi) {
        std::vector<double> values{lo,hi};for(int i=1;i<knots.Length();++i)
            for(double t:{1.0/6,.5,5.0/6})values.push_back(knots(i)+(knots(i+1)-knots(i))*t);
        for(int i=1;i<32;++i)values.push_back(lo+(hi-lo)*i/32);return values;
    };
    us=locations(base->UKnots(),d.u0,d.u1);vs=locations(base->VKnots(),d.v0,d.v1);
    double sum=0;std::size_t count=0;result.normalThicknessMinMm=std::numeric_limits<double>::infinity();
    for(double u:us)for(double v:vs) {
        gp_Pnt p;const auto n=Normal(base,d,u,v,sign,p);
        const auto a=first->Value(u,v),b=last->Value(u,v);
        const double normal=gp_Vec(a,b).Dot(n)*(lastOffset>firstOffset?1:-1);
        if(!std::isfinite(normal)||normal<=tolerance)return false;
        result.normalThicknessMinMm=std::min(result.normalThicknessMinMm,normal);
        result.normalThicknessMaxMm=std::max(result.normalThicknessMaxMm,normal);
        for(const auto& [skin,offset]:{std::pair{first,firstOffset},std::pair{last,lastOffset}}) {
            if(offset==0)continue;
            const double error=p.Translated(n*offset).Distance(skin->Value(u,v));
            if(!std::isfinite(error)||error>thickness*.75)return false;
            result.maxOffsetDeviationMm=std::max(result.maxOffsetDeviationMm,error);sum+=error*error;++count;
            gp_Pnt q;gp_Vec du,dv;skin->D1(u,v,q,du,dv);
            // Zero derivatives at a spline corner can be a valid endpoint
            // parametrization. Check orientation in the interior; native
            // topology and self-intersection still validate the entire solid.
            const bool interior=u>d.u0+(d.u1-d.u0)*1e-9&&u<d.u1-(d.u1-d.u0)*1e-9
                &&v>d.v0+(d.v1-d.v0)*1e-9&&v<d.v1-(d.v1-d.v0)*1e-9;
            if(interior&&du.Crossed(dv).Dot(n)*sign<=1e-20)return false;
        }
    }
    result.rmsOffsetDeviationMm=std::sqrt(sum/count);return true;
}
}
Out CorrectedThicken(modeling::KernelShapeHandle source,double thickness,fabrication::ThicknessPlacement placement,
    const geometry::GeometryTolerance& tolerance) {
    try {
        TopoDS_Shape original;if(!LookupShape(source,original))return Refused("元の面がありません。");
        const auto copy=BRepBuilderAPI_Copy(original,true,false).Shape();TopExp_Explorer faces(copy,TopAbs_FACE);
        if(!faces.More())return Refused("元の面がありません。");
        const auto face=TopoDS::Face(faces.Current());faces.Next();
        if(faces.More())return Refused("補正は1枚の連続した曲面に対応しています。面を分けて指定してください。");
        Domain d;BRepTools::UVBounds(face,d.u0,d.u1,d.v0,d.v1);
        if(!std::isfinite(d.u0+d.u1+d.v0+d.v1)||d.u1<=d.u0||d.v1<=d.v0||!Rectangle(face,d))
            return Refused("穴や任意トリムのある面は補正で埋めません。一定厚みを使うか、面を分けてください。");
        auto base=GeomConvert::SurfaceToBSplineSurface(BRep_Tool::Surface(face));
        if(base->IsUPeriodic()||base->IsVPeriodic())return Refused("周期面の補正には対応していません。一定厚みを使ってください。");
        base->Segment(d.u0,d.u1,d.v0,d.v1);
        if(base->NbUPoles()>32768/base->NbVPoles())return Refused("補正面が複雑すぎます。面を分けてください。");
        const double sign=face.Orientation()==TopAbs_REVERSED?-1:1;
        const auto normals=SmoothedNormals(base,d,sign,thickness);
        const double a=placement==fabrication::ThicknessPlacement::Centered?-thickness*.5:0;
        const double b=placement==fabrication::ThicknessPlacement::Inside?-thickness:
            placement==fabrication::ThicknessPlacement::Centered?thickness*.5:thickness;
        const double precision=std::max(1e-9,thickness*.0002);
        const auto first=a==0?base:Opposite(base,normals,a,precision);
        const auto last=Opposite(base,normals,b,precision);
        ThickenedSolid result;result.thicknessMm=thickness;result.corrected=true;
        if(!Measure(base,first,last,d,sign,a,b,thickness,tolerance.modelLinearMm,result))
            return Refused("反対面の向き、厚み、位置ずれの検査に失敗しました。厚みを小さくするか面を分けてください。");
        const double sewing=std::max(1e-7,tolerance.modelLinearMm);
        BRepBuilderAPI_Sewing sew(sewing);
        // In one-sided mode this is the exact original face, not a refit.
        sew.Add(a==0?face:BRepBuilderAPI_MakeFace(first,sewing).Face());
        sew.Add(BRepBuilderAPI_MakeFace(last,sewing).Face());
        for(int side=0;side<4;++side)sew.Add(Cap(first,last,d,side,sewing));
        sew.Perform();if(sew.SewedShape().ShapeType()!=TopAbs_SHELL)return Refused("厚みの側面を閉じられませんでした。");
        auto solid=BRepBuilderAPI_MakeSolid(TopoDS::Shell(sew.SewedShape())).Solid();
        GProp_GProps mass;VolumeProperties(solid,mass);if(mass.Mass()<0)solid.Reverse();
        result.handle=StoreShape(solid);const auto check=CheckSolidForExport(result.handle,tolerance.modelLinearMm);
        if(!check.HasValue()||!check.Value().Ok()) {
            ReleaseShape(result.handle);return Refused("補正した立体が閉鎖性・体積・自己交差の安全検査に通りませんでした。");
        }
        result.volumeMm3=check.Value().volumeMm3;const auto edges=OutputEdges(result.handle,tolerance.modelLinearMm);
        if(edges.HasValue())result.edges=edges.Value();return Out::Success(std::move(result));
    } catch(const Standard_Failure& e) {return Out::Failure(base::MakeError(kThickenFailed,"厚みの反対面を補正できませんでした。",e.what()));}
      catch(const std::exception& e) {return Out::Failure(base::MakeError(kThickenFailed,"厚みの反対面を補正できませんでした。",e.what()));}
}
}
#else
namespace kachakacha::v2::kernel::detail {
base::Result<ThickenedSolid> CorrectedThicken(modeling::KernelShapeHandle,double,fabrication::ThicknessPlacement,const geometry::GeometryTolerance&) {
    return base::Result<ThickenedSolid>::Failure(base::MakeError(kThickenFailed,"幾何カーネルがありません。"));
}
}
#endif
