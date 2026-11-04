#include "kachakacha/kernel/OcctBilinearSupport.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <BRepTools.hxx>
#include <BRepTools_CopyModification.hxx>
#include <BRepTools_Modifier.hxx>
#include <BRepTools_Modification.hxx>
#include <BRep_Tool.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepLib_CheckCurveOnSurface.hxx>
#include <BRepGProp.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_Surface.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <utility>
namespace kachakacha::v2::kernel::detail {
namespace {
using Spline=occ::handle<Geom_BSplineSurface>;
bool LinearGrid(const Spline& b) {
    if(b.IsNull()||b->UDegree()!=1||b->VDegree()!=1||b->IsURational()||b->IsVRational()
        ||b->IsUPeriodic()||b->IsVPeriodic()||b->NbUPoles()!=b->NbUKnots()||b->NbVPoles()!=b->NbVKnots())return false;
    for(int i=1;i<=b->NbUKnots();++i)if(b->UMultiplicity(i)!=(i==1||i==b->NbUKnots()?2:1))return false;
    for(int i=1;i<=b->NbVKnots();++i)if(b->VMultiplicity(i)!=(i==1||i==b->NbVKnots()?2:1))return false;
    return b->NbUKnots()>2||b->NbVKnots()>2;
}
bool BoundedDifference(const Spline& old,const Spline& b,double thickness,double tolerance) {
    const int nu=old->NbUPoles(),nv=old->NbVPoles();double error=0;
    for(int i=1;i<=nu;++i)for(int j=1;j<=nv;++j)
        error=std::max(error,old->Pole(i,j).Distance(b->Value(old->UKnot(i),old->VKnot(j))));
    if(const auto* path=std::getenv("KACHACAD_SPAN_TRACE")){std::ofstream out(path,std::ios::app);out<<"bilinear hull deviation "<<error<<'\n';}
    if(!std::isfinite(error)||error>std::min(1e-10,tolerance*1e-3))return false;
    // On every common bilinear cell, both the position difference and the
    // unnormalized normal difference are bounded by their four corners.
    for(int i=1;i<nu;++i)for(int j=1;j<nv;++j) {
        gp_Vec candidate[4],original[4];int index=0;
        const double u0=old->UKnot(i),u1=old->UKnot(i+1),v0=old->VKnot(j),v1=old->VKnot(j+1);
        const auto a=old->Pole(i,j),c=old->Pole(i,j+1),d=old->Pole(i+1,j+1),e=old->Pole(i+1,j);
        for(int x:{0,1})for(int y:{0,1}) {
            gp_Pnt p;gp_Vec du,dv;b->D1(x?u1:u0,y?v1:v0,p,du,dv);
            candidate[index]=du.Crossed(dv)*(u1-u0)*(v1-v0);
            original[index++]=(y?gp_Vec(c,d):gp_Vec(a,e)).Crossed(x?gp_Vec(e,d):gp_Vec(a,c));
        }
        const auto middle=(candidate[0]+candidate[1]+candidate[2]+candidate[3])*.25;
        if(middle.Magnitude()<1e-20)return false;const auto direction=middle.Normalized();
        double lower=middle.Magnitude(),difference=0;
        for(int k=0;k<4;++k){lower=std::min(lower,candidate[k].Dot(direction));difference=std::max(difference,(candidate[k]-original[k]).Magnitude());}
        if(lower<=difference||error+std::abs(thickness)*2*difference/lower>std::min(1e-9,tolerance*.1))return false;
    }
    return true;
}
class ReplaceSupport final:public BRepTools_CopyModification {
    Spline support_;
public:
    explicit ReplaceSupport(Spline support):BRepTools_CopyModification(true,false),support_(std::move(support)){}
    bool NewSurface(const TopoDS_Face& face,occ::handle<Geom_Surface>& surface,TopLoc_Location& location,
        double& tolerance,bool& wires,bool& reversed) override {
        BRep_Tool::Surface(face,location);surface=support_;tolerance=BRep_Tool::Tolerance(face);wires=false;reversed=false;return true;
    }
};
}
std::optional<TopoDS_Shape> BilinearSupportCopy(const TopoDS_Shape& source,double thickness,double tolerance) {
    try {
        TopExp_Explorer faces(source,TopAbs_FACE);if(!faces.More())return std::nullopt;
        const auto face=TopoDS::Face(faces.Current());faces.Next();if(faces.More())return std::nullopt;
        TopLoc_Location location;const auto old=Spline::DownCast(BRep_Tool::Surface(face,location));
        if(!LinearGrid(old))return std::nullopt;
        NCollection_Array2<gp_Pnt> poles(1,2,1,2);NCollection_Array1<double> u(1,2),v(1,2);NCollection_Array1<int> mult(1,2);mult.Init(2);
        for(int i=1;i<=2;++i)for(int j=1;j<=2;++j)poles(i,j)=old->Pole(i==1?1:old->NbUPoles(),j==1?1:old->NbVPoles());
        u(1)=old->UKnot(1);u(2)=old->UKnot(old->NbUKnots());v(1)=old->VKnot(1);v(2)=old->VKnot(old->NbVKnots());
        const Spline support=new Geom_BSplineSurface(poles,u,v,mult,mult,1,1);
        if(!BoundedDifference(old,support,thickness,tolerance))return std::nullopt;
        const occ::handle<BRepTools_Modification> replacement=new ReplaceSupport(support);
        BRepTools_Modifier modifier(face,replacement);if(!modifier.IsDone())return std::nullopt;
        const auto result=modifier.ModifiedShape(face);if(!BRepCheck_Analyzer(result).IsValid())return std::nullopt;
        for(TopExp_Explorer f(result,TopAbs_FACE);f.More();f.Next())for(TopExp_Explorer e(f.Current(),TopAbs_EDGE);e.More();e.Next()) {
            BRepLib_CheckCurveOnSurface check(TopoDS::Edge(e.Current()),TopoDS::Face(f.Current()));check.Perform();
            if(!check.IsDone()||!std::isfinite(check.MaxDistance())||check.MaxDistance()>std::min(1e-9,tolerance*.1))return std::nullopt;
        }
        GProp_GProps before,after;const auto a=BRepGProp::SurfaceProperties(face,before,1e-10,false),b=BRepGProp::SurfaceProperties(result,after,1e-10,false);
        if(!std::isfinite(a)||!std::isfinite(b)||a>1e-8||b>1e-8||!std::isfinite(before.Mass())||!std::isfinite(after.Mass())||before.Mass()<=0
            ||std::abs(before.Mass()-after.Mass())>std::max(1e-9,before.Mass()*1e-9))return std::nullopt;
        return result;
    }catch(const Standard_Failure&){return std::nullopt;}
}
}
#endif
