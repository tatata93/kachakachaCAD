#include "kachakacha/kernel/OcctPlanarSplineThicken.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepLib.hxx>
#include <BRepLib_CheckCurveOnSurface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <Geom_BSplineSurface.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <gp_Trsf.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <utility>
#include <cstdlib>
#include <fstream>
#include <string>

namespace kachakacha::v2::kernel::detail {
namespace {
void Trace(const std::string& stage){if(const auto* path=std::getenv("KACHACAD_SPAN_TRACE")){std::ofstream out(path,std::ios::app);out<<"planar spline: "<<stage<<'\n';}}
bool ConsistentLinearSpans(const occ::handle<Geom_BSplineSurface>& spline,const gp_Dir& normal,
    double u0,double u1,double v0,double v1) {
    // For a non-rational bilinear cell its signed planar Jacobian is affine.
    // The four exact corner derivatives therefore bound its orientation.
    for(int i=1;i<spline->NbUPoles();++i)for(int j=1;j<spline->NbVPoles();++j) {
        if(spline->UKnot(i+1)<=u0||spline->UKnot(i)>=u1
            ||spline->VKnot(j+1)<=v0||spline->VKnot(j)>=v1)continue;
        const auto a=spline->Pole(i,j),b=spline->Pole(i+1,j),c=spline->Pole(i,j+1),d=spline->Pole(i+1,j+1);
        for(const auto& du:{gp_Vec(a,b),gp_Vec(c,d)})for(const auto& dv:{gp_Vec(a,c),gp_Vec(b,d)})
            if(du.Crossed(dv).Dot(gp_Vec(normal))<=1e-20)return false;
    }
    return true;
}
std::optional<gp_Pln> ProvenPlane(const TopoDS_Face& face,double tolerance) {
    BRepAdaptor_Surface adaptor(face,false);
    if(adaptor.GetType()!=GeomAbs_BSplineSurface)return std::nullopt;
    const auto spline=adaptor.BSpline();
    if(spline->UDegree()!=1||spline->VDegree()!=1||spline->IsURational()||spline->IsVRational())return std::nullopt;
    const double limit=std::min(1e-10,tolerance*1e-3);
    const auto origin=spline->Pole(1,1);gp_Vec axis,normal;double longest=0,widest=0;
    for(int i=1;i<=spline->NbUPoles();++i)for(int j=1;j<=spline->NbVPoles();++j) {
        const gp_Vec delta(origin,spline->Pole(i,j));
        if(delta.SquareMagnitude()>longest){axis=delta;longest=delta.SquareMagnitude();}
    }
    if(longest<1e-20)return std::nullopt;
    for(int i=1;i<=spline->NbUPoles();++i)for(int j=1;j<=spline->NbVPoles();++j) {
        const auto cross=axis.Crossed(gp_Vec(origin,spline->Pole(i,j)));
        if(cross.SquareMagnitude()>widest){normal=cross;widest=cross.SquareMagnitude();}
    }
    if(widest<1e-20)return std::nullopt;
    gp_Pln plane(origin,gp_Dir(normal));
    // Positive rational weights make the complete control hull a distance
    // bound. A sampled planar-looking patch alone is not sufficient proof.
    for(int u=1;u<=spline->NbUPoles();++u)for(int v=1;v<=spline->NbVPoles();++v)
        if(!std::isfinite(spline->Weight(u,v))||spline->Weight(u,v)<=0
            ||plane.Distance(spline->Pole(u,v))>limit){Trace("control hull is not planar");return std::nullopt;}
    double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
    for(double fu:{.37,.63,.17,.83})for(double fv:{.37,.63,.17,.83}) {
        const double u=u0+fu*(u1-u0),v=v0+fv*(v1-v0);
        if(BRepClass_FaceClassifier(face,gp_Pnt2d(u,v),1e-12).State()!=TopAbs_IN)continue;
        gp_Pnt p;gp_Vec du,dv;spline->D1(u,v,p,du,dv);const auto parametricNormal=du.Crossed(dv);
        if(parametricNormal.Magnitude()<1e-20)continue;
        if(parametricNormal.Dot(gp_Vec(plane.Axis().Direction()))<0)plane=gp_Pln(origin,plane.Axis().Direction().Reversed());
        if(!ConsistentLinearSpans(spline,plane.Axis().Direction(),u0,u1,v0,v1))return std::nullopt;
        return plane;
    }
    return std::nullopt;
}
std::optional<TopoDS_Face> PlanarCopy(const TopoDS_Face& source,const gp_Pln& plane,double tolerance) {
    BRepBuilderAPI_Copy copy(source,true,false);if(!copy.IsDone())return std::nullopt;
    auto face=TopoDS::Face(copy.Shape());const auto orientation=face.Orientation();face.Orientation(TopAbs_FORWARD);
    const auto outer=BRepTools::OuterWire(face);if(outer.IsNull())return std::nullopt;
    BRepBuilderAPI_MakeFace maker(plane,outer,true);
    for(TopExp_Explorer w(face,TopAbs_WIRE);w.More();w.Next())
        if(!w.Current().IsSame(outer))maker.Add(TopoDS::Wire(w.Current()));
    if(!maker.IsDone())return std::nullopt;auto result=maker.Face();
    // The former UV representation belongs to another surface. Build planar
    // pcurves from the unchanged 3D boundaries, on copied edges only.
    for(TopExp_Explorer e(result,TopAbs_EDGE);e.More();e.Next()) {
        const auto edge=TopoDS::Edge(e.Current());BRepLib::BuildPCurveForEdgeOnPlane(edge,result);
        BRepLib_CheckCurveOnSurface check(edge,result);check.Perform();
        if(!check.IsDone()||!std::isfinite(check.MaxDistance())
            ||check.MaxDistance()>std::min(1e-9,tolerance*.1))return std::nullopt;
    }
    result.Orientation(orientation);
    if(!BRepCheck_Analyzer(result).IsValid())return std::nullopt;
    GProp_GProps before,after;
    const double a=BRepGProp::SurfaceProperties(source,before,1e-10,false);
    const double b=BRepGProp::SurfaceProperties(result,after,1e-10,false);
    if(!std::isfinite(a)||!std::isfinite(b)||a>1e-8||b>1e-8||before.Mass()<=0
        ||!std::isfinite(before.Mass())||!std::isfinite(after.Mass())
        ||std::abs(before.Mass()-after.Mass())>std::max(1e-9,before.Mass()*1e-9))return std::nullopt;
    return result;
}
}
std::optional<TopoDS_Shape> ThickenPlanarSpline(const TopoDS_Shape& source,double thickness,
    fabrication::ThicknessPlacement placement,const geometry::GeometryTolerance& tolerance) {
    try {
        TopExp_Explorer faces(source,TopAbs_FACE);if(!faces.More())return std::nullopt;
        const auto face=TopoDS::Face(faces.Current());faces.Next();if(faces.More())return std::nullopt;
        const auto plane=ProvenPlane(face,tolerance.modelLinearMm);if(!plane)return std::nullopt;
        Trace("complete planar control hull proved");
        auto flat=PlanarCopy(face,*plane,tolerance.modelLinearMm);if(!flat){Trace("planar boundary or area check failed");return std::nullopt;}
        const auto normal=gp_Vec(plane->Axis().Direction())*(face.Orientation()==TopAbs_REVERSED?-1.:1.);
        const double low=placement==fabrication::ThicknessPlacement::Inside?-thickness:
            placement==fabrication::ThicknessPlacement::Centered?-thickness*.5:0.;
        TopoDS_Shape base=*flat;
        if(low!=0){gp_Trsf move;move.SetTranslation(normal*low);base=BRepBuilderAPI_Transform(base,move,false).Shape();}
        BRepPrimAPI_MakePrism maker(base,normal*thickness,true,true);if(!maker.IsDone())return std::nullopt;
        TopExp_Explorer solids(maker.Shape(),TopAbs_SOLID);if(!solids.More())return std::nullopt;
        auto solid=TopoDS::Solid(solids.Current());solids.Next();if(solids.More()||!BRepLib::OrientClosedSolid(solid))return std::nullopt;
        const auto handle=StoreShape(solid);
        struct Guard{modeling::KernelShapeHandle handle;~Guard(){ReleaseShape(handle);}}guard{handle};
        const auto checked=CheckSolidForExport(handle,tolerance.modelLinearMm);
        if(!checked.HasValue()||!checked.Value().Ok()){Trace("prism native check failed");return std::nullopt;}
        GProp_GProps area;BRepGProp::SurfaceProperties(*flat,area,1e-10,false);
        const double expected=area.Mass()*thickness;
        if(std::abs(checked.Value().volumeMm3-expected)>std::max(1e-9,expected*1e-8))return std::nullopt;
        if(!BuildStepText(handle,tolerance.modelLinearMm).HasValue()){Trace("prism STEP check failed");return std::nullopt;}
        Trace("native and STEP planar prism accepted");
        return solid;
    }catch(const Standard_Failure&){return std::nullopt;}
}
}
#endif
