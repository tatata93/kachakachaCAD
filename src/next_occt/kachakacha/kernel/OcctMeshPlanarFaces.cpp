#include "kachakacha/kernel/OcctMeshPlanarFaces.h"
#include "kachakacha/kernel/OcctMassProperties.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <Geom_Plane.hxx>
#include <Geom_Surface.hxx>
#include <GProp_GProps.hxx>
#include <ShapeBuild_ReShape.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <Standard_Failure.hxx>
#include <Precision.hxx>
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

namespace kachakacha::v2::kernel::detail {
namespace {
bool NeedsPlane(const TopoDS_Face& face) {
    const auto surface=BRep_Tool::Surface(face);
    if(!occ::handle<Geom_Plane>::DownCast(surface).IsNull()
        ||!GeomLib_IsPlanarSurface(surface,Precision::Confusion()).IsPlanar())return false;
    // Only singular parameterisations require replacement. Ordinary trimmed
    // planar splines (such as emboss caps) already mesh correctly, and their
    // arbitrary trimming topology must not be rebuilt as a pole workaround.
    for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()) {
        const auto edge=TopoDS::Edge(e.Current());
        if(BRep_Tool::Degenerated(edge)||BRep_Tool::IsClosed(edge,face))return true;
    }
    return false;
}
double Area(const TopoDS_Shape& shape) {GProp_GProps p;BRepGProp::SurfaceProperties(shape,p,1e-10);return p.Mass();}
TopoDS_Face PlanarCopy(const TopoDS_Face& face) {
    const auto surface=BRep_Tool::Surface(face);
    const GeomLib_IsPlanarSurface planar(surface,Precision::Confusion());
    occ::handle<TopTools_HSequenceOfShape> edges=new TopTools_HSequenceOfShape;
    for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()) {
        const auto edge=TopoDS::Edge(e.Current());
        if(!BRep_Tool::Degenerated(edge)&&!BRep_Tool::IsClosed(edge,face))edges->Append(edge);
    }
    const auto wires=ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges,Precision::Confusion(),true);
    if(wires.IsNull()||wires->IsEmpty())throw Standard_Failure("Planar mesh boundary is empty");
    std::vector<std::pair<double,TopoDS_Wire>> loops;
    for(int i=1;i<=wires->Length();++i) {
        BRepBuilderAPI_MakeFace bounded(planar.Plan(),TopoDS::Wire(wires->Value(i)),true);
        if(!bounded.IsDone())throw Standard_Failure("Planar mesh loop could not be built");
        loops.push_back({Area(bounded.Face()),BRepTools::OuterWire(bounded.Face())});
    }
    std::sort(loops.begin(),loops.end(),[](const auto& a,const auto& b){return a.first>b.first;});
    BRepBuilderAPI_MakeFace maker(planar.Plan(),loops.front().second,true);
    for(std::size_t i=1;i<loops.size();++i)maker.Add(TopoDS::Wire(loops[i].second.Reversed()));
    if(!maker.IsDone()||!BRepCheck_Analyzer(maker.Face()).IsValid())throw Standard_Failure("Planar mesh face is invalid");
    auto result=maker.Face();double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
    gp_Pnt p;gp_Vec du,dv;surface->D1((u0+u1)*.5,(v0+v1)*.5,p,du,dv);
    // ReShape propagates the source face orientation. Match its underlying
    // FORWARD normal here, rather than applying that orientation twice.
    if(du.Crossed(dv).Dot(gp_Vec(planar.Plan().Axis().Direction()))<0)result.Reverse();
    GProp_GProps perimeter;BRepGProp::LinearProperties(face,perimeter);
    // The same native edges are retained. Reparameterization onto the proven
    // plane is accurate to Confusion, so the dimensional area bound is boundary
    // length times that distance (both boundaries), not a fixed area epsilon.
    const double areaTolerance=std::max(1e-8,2*perimeter.Mass()*Precision::Confusion());
    if(std::abs(Area(result)-Area(face))>areaTolerance) {
        GProp_GProps a,b;const double ae=BRepGProp::SurfaceProperties(face,a,1e-12);
        const double be=BRepGProp::SurfaceProperties(result,b,1e-12);
        std::ostringstream message;message<<std::setprecision(17)<<"Planar mesh area: "
            <<Area(face)<<"/"<<Area(result)<<" tighter="<<a.Mass()<<"/"<<b.Mass()
            <<" errors="<<ae<<"/"<<be<<" loops="<<loops.size();
        throw Standard_Failure(message.str().c_str());
    }
    return result;
}
}
TopoDS_Shape MeshReadyShape(const TopoDS_Shape& source) {
    bool needed=false;
    for(TopExp_Explorer e(source,TopAbs_FACE);e.More();e.Next())needed=needed||NeedsPlane(TopoDS::Face(e.Current()));
    if(!needed)return source;
    BRepBuilderAPI_Copy copy(source,true,false);
    occ::handle<ShapeBuild_ReShape> replace=new ShapeBuild_ReShape;
    for(TopExp_Explorer e(copy.Shape(),TopAbs_FACE);e.More();e.Next()) {
        auto face=TopoDS::Face(e.Current());
        if(NeedsPlane(face))replace->Replace(face.Oriented(TopAbs_FORWARD),PlanarCopy(face));
    }
    const auto result=replace->Apply(copy.Shape());
    const bool hasSolid=TopExp_Explorer(source,TopAbs_SOLID).More();
    GProp_GProps before,after;
    if(hasSolid){VolumeProperties(source,before);VolumeProperties(result,after);}
    const double volumeTolerance=std::max(1e-7,2*Area(source)*Precision::Confusion());
    if(!BRepCheck_Analyzer(result).IsValid()||(hasSolid&&std::abs(before.Mass()-after.Mass())>volumeTolerance))
        throw Standard_Failure(("Planar mesh conversion changed validity or signed volume: valid="+std::to_string(BRepCheck_Analyzer(result).IsValid())+" volume="+std::to_string(before.Mass())+"/"+std::to_string(after.Mass())).c_str());
    return result;
}
}
#endif
