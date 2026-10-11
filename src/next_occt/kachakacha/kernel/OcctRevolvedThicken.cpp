#include "kachakacha/kernel/OcctRevolvedThicken.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Result.hxx>
#include <BRepLib.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <Geom_OffsetCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomAPI.hxx>
#include <GeomConvert_ApproxCurve.hxx>
#include <Geom_Plane.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Solid.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <Standard_Failure.hxx>
#include <cmath>

namespace kachakacha::v2::kernel::detail {
namespace {
using Out=base::Result<TopoDS_Shape>;
Out Failed(const std::string& detail="断面の曲率、軸との距離、端の接続を確認してください。"){
    return Out::Failure(base::MakeError(kThickenFailed,"回転面の断面から厚みを作れませんでした。",detail));}
std::string InvalidProfile(const TopoDS_Face& face) {
    BRepCheck_Analyzer check(face);std::string message="厚み断面の検査:";
    for(const auto kind:{TopAbs_FACE,TopAbs_WIRE,TopAbs_EDGE,TopAbs_VERTEX})
        for(TopExp_Explorer e(face,kind);e.More();e.Next())
            for(const auto status:check.Result(e.Current())->Status())
                if(status!=BRepCheck_NoError)message+=" type="+std::to_string(int(kind))+" status="+std::to_string(int(status));
    return message;
}
// Accept only an unperforated rectangular parameter domain. Arbitrary trims
// must stay on the general surface-offset path instead of losing their holes.
bool FullDomain(const TopoDS_Face& face,const occ::handle<Geom_SurfaceOfRevolution>& surface,
    double u0,double u1,double v0,double v1,double tolerance)
{
    int wires=0;for(TopExp_Explorer e(face,TopAbs_WIRE);e.More();e.Next())++wires;
    if(wires!=1)return false;
    BRepBuilderAPI_MakeFace full(surface,u0,u1,v0,v1,tolerance);if(!full.IsDone())return false;
    GProp_GProps a,b;BRepGProp::SurfaceProperties(face,a);BRepGProp::SurfaceProperties(full.Face(),b);
    return std::abs(a.Mass()-b.Mass())<=std::max(tolerance*tolerance,a.Mass()*1e-9);
}
Out Build(const TopoDS_Face& face,const occ::handle<Geom_SurfaceOfRevolution>& surface,
    double u0,double u1,double v0,double v1,double thickness,fabrication::ThicknessPlacement placement,double tolerance)
{
    const auto axis=surface->Axis();gp_Trsf rotation;rotation.SetRotation(axis,u0);
    auto curve=occ::handle<Geom_Curve>::DownCast(surface->BasisCurve()->Transformed(rotation));
    while(const auto trimmed=occ::handle<Geom_TrimmedCurve>::DownCast(curve))curve=trimmed->BasisCurve();
    gp_Pnt point;gp_Vec du,dv;surface->D1(u0,(v0+v1)*.5,point,du,dv);
    auto expected=du.Crossed(dv);if(face.Orientation()==TopAbs_REVERSED)expected.Reverse();
    if(expected.SquareMagnitude()<1e-20)return Failed();
    gp_Vec radial(axis.Location(),point);radial-=gp_Vec(axis.Direction())*radial.Dot(gp_Vec(axis.Direction()));
    const gp_Dir reference(radial.Crossed(gp_Vec(axis.Direction())));
    occ::handle<Geom_OffsetCurve> probe=new Geom_OffsetCurve(curve,1.0,reference);
    const double sign=gp_Vec(curve->Value((v0+v1)*.5),probe->Value((v0+v1)*.5)).Dot(expected)<0?-1:1;
    const double start=placement==fabrication::ThicknessPlacement::Centered?-.5*thickness:0;
    const double end=placement==fabrication::ThicknessPlacement::Inside?-thickness:
        placement==fabrication::ThicknessPlacement::Centered?.5*thickness:thickness;
    const auto offset=[&](double distance)->occ::handle<Geom_Curve>{
        if(distance==0)return curve;
        const occ::handle<Geom_Curve> exact=new Geom_OffsetCurve(curve,distance*sign,reference);
        const occ::handle<Geom_Curve> trimmed=new Geom_TrimmedCurve(exact,v0,v1);
        GeomConvert_ApproxCurve approximation(trimmed,tolerance,GeomAbs_C1,4096,8);
        if(!approximation.IsDone()||approximation.MaxError()>tolerance)return {};
        return approximation.Curve();};
    const auto a=offset(start),b=offset(end);
    if(a.IsNull()||b.IsNull())return Failed("The offset profile could not meet the model tolerance.");
    BRepBuilderAPI_MakeEdge first(a,v0,v1),last(b,v0,v1);
    BRepBuilderAPI_MakeEdge cap1(a->Value(v1),b->Value(v1)),cap0(b->Value(v0),a->Value(v0));
    if(!first.IsDone()||!last.IsDone()||!cap1.IsDone()||!cap0.IsDone())return Failed("断面の辺を作れません。");
    const gp_Pln plane(axis.Location(),reference);
    const occ::handle<Geom_Plane> planeSurface=new Geom_Plane(plane);
    // The outer skin stays exact. Offset B-splines meet a tenth of the model
    // tolerance and can be retained by STEP, unlike its unsupported offset curve.
    BRep_Builder builder;
    builder.UpdateEdge(first.Edge(),GeomAPI::To2d(a,plane),planeSurface,TopLoc_Location(),tolerance);
    builder.UpdateEdge(last.Edge(),GeomAPI::To2d(b,plane),planeSurface,TopLoc_Location(),tolerance);
    auto back=last.Edge();back.Reverse();
    BRepBuilderAPI_MakeWire wire(first.Edge(),cap1.Edge(),back,cap0.Edge());if(!wire.IsDone())return Failed("断面の端がつながりません。");
    BRepBuilderAPI_MakeFace profile(planeSurface,wire.Wire(),true);
    if(!profile.IsDone())return Failed("厚み断面の平面を作れません。");
    if(!BRepCheck_Analyzer(profile.Face()).IsValid())return Failed(InvalidProfile(profile.Face()));
    BRepPrimAPI_MakeRevol revolution(profile.Face(),axis,u1-u0,true);
    if(!revolution.IsDone())return Failed("厚み断面を回転できません。");
    TopExp_Explorer solid(revolution.Shape(),TopAbs_SOLID);if(!solid.More())return Failed();
    auto result=TopoDS::Solid(solid.Current());solid.Next();
    if(solid.More()||!BRepLib::OrientClosedSolid(result)||!BRepCheck_Analyzer(result).IsValid())return Failed("回転した断面が閉じた正しい立体になりません。");
    return Out::Success(result);
}
}
std::optional<Out> ThickenRevolvedProfile(const TopoDS_Shape& shape,double thickness,
    fabrication::ThicknessPlacement placement,const geometry::GeometryTolerance& tolerance)
{
    TopExp_Explorer faces(shape,TopAbs_FACE);if(!faces.More())return {};
    const auto face=TopoDS::Face(faces.Current());faces.Next();if(faces.More())return {};
    const auto surface=occ::handle<Geom_SurfaceOfRevolution>::DownCast(BRep_Tool::Surface(face));
    if(surface.IsNull())return {};
    auto basis=surface->BasisCurve();
    while(const auto trimmed=occ::handle<Geom_TrimmedCurve>::DownCast(basis))basis=trimmed->BasisCurve();
    if(occ::handle<Geom_BSplineCurve>::DownCast(basis).IsNull())return {};
    try {
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        if(!FullDomain(face,surface,u0,u1,v0,v1,tolerance.modelLinearMm))return {};
        return Build(face,surface,u0,u1,v0,v1,thickness,placement,tolerance.modelLinearMm*.1);
    }catch(const Standard_Failure& error){return Failed(error.GetMessageString());}
}
}
#endif
