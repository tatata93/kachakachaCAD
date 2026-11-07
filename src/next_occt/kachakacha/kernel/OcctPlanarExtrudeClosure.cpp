#include "kachakacha/kernel/OcctPlanarExtrudeClosure.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctExtrudeWires.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFill.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepLib_CheckCurveOnSurface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <GeomProjLib.hxx>
#include <Geom_Plane.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Solid.hxx>
#include <TopTools_MapOfShape.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <array>
#include <vector>

namespace kachakacha::v2::kernel::detail {
namespace {
using Out=base::Result<ExtrudeBuildResult>;
Out Failed(const char* message) {return Out::Failure(base::MakeError("EXT-008",message,"元の形は変更していません。"));}
struct StoredSolid {
    modeling::KernelShapeHandle handle;
    bool keep=false;
    ~StoredSolid(){if(!keep)ReleaseShape(handle);}
};

double VerifiedBoundaryError(const TopoDS_Face& face,double precision) {
    double required=precision;
    for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()) {
        const auto edge=TopoDS::Edge(e.Current());
        BRepLib_CheckCurveOnSurface check(edge,face);check.SetParallel(true);check.Perform();
        if(!BRep_Tool::SameParameter(edge)||!check.IsDone()||!std::isfinite(check.MaxDistance()))
            throw std::runtime_error("source boundary consistency not measurable");
        if(check.MaxDistance()>BRep_Tool::Tolerance(edge)+precision)
            throw std::runtime_error("source curve exceeds its declared boundary tolerance");
        required=std::max(required,check.MaxDistance()+precision);
        const BRepAdaptor_Curve curve(edge);
        for(const auto& endpoint:{std::pair{TopExp::FirstVertex(edge),curve.FirstParameter()},
            std::pair{TopExp::LastVertex(edge),curve.LastParameter()}}) {
            if(endpoint.first.IsNull())throw std::runtime_error("source boundary endpoint absent");
            const double gap=BRep_Tool::Pnt(endpoint.first).Distance(curve.Value(endpoint.second));
            if(!std::isfinite(gap)||gap>BRep_Tool::Tolerance(endpoint.first)+precision)
                throw std::runtime_error("source vertex exceeds its declared tolerance");
            required=std::max(required,gap+precision);
        }
    }
    return required;
}

std::array<double,2> PlaneDistances(const TopoDS_Shape& shape,const gp_Pln& plane) {
    gp_Trsf frame;frame.SetTransformation(plane.Position());
    const auto local=BRepBuilderAPI_Transform(shape,frame,true).Shape();
    Bnd_Box box;BRepBndLib::AddOptimal(local,box,false,false);
    if(box.IsVoid()||box.IsOpen())throw std::runtime_error("unbounded extrusion source");
    double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);return {z0,z1};
}

bool Covered(const TopoDS_Face& cap,const TopoDS_Face& target,double tolerance) {
    if(BRepTools::OuterWire(target).IsNull())return true; // unbounded support plane
    const auto capCopy=BRepBuilderAPI_Copy(cap,true,false).Shape();
    const auto targetCopy=BRepBuilderAPI_Copy(target,true,false).Shape();
    BRepAlgoAPI_Cut missing(capCopy,targetCopy);
    if(!missing.IsDone()||missing.HasErrors())return false;
    GProp_GProps area;BRepGProp::SurfaceProperties(missing.Shape(),area);
    return std::isfinite(area.Mass())&&std::abs(area.Mass())<=tolerance*tolerance;
}

struct Caps {
    TopoDS_Face top,bottom;
    std::vector<TopoDS_Face> sides;
};
Caps ProjectCaps(TopoDS_Face source,const gp_Pln& stop,const gp_Dir& direction,double tolerance) {
    source.Orientation(TopAbs_FORWARD);
    Caps result;result.top=source;
    const occ::handle<Geom_Plane> plane=new Geom_Plane(stop);
    const auto outer=BRepTools::OuterWire(source);
    TopoDS_Wire projectedOuter;std::vector<TopoDS_Wire> projectedHoles;
    for(TopExp_Explorer w(source,TopAbs_WIRE);w.More();w.Next()) {
        BRepBuilderAPI_MakeWire projected;
        for(BRepTools_WireExplorer e(TopoDS::Wire(w.Current()),source);e.More();e.Next()) {
            const auto edge=e.Current();
            if(BRep_Tool::Degenerated(edge))throw std::runtime_error("degenerate projected boundary");
            TopLoc_Location location;double first,last;
            auto curve=BRep_Tool::Curve(edge,location,first,last);
            if(curve.IsNull())throw std::runtime_error("source boundary has no 3D curve");
            curve=occ::handle<Geom_Curve>::DownCast(curve->Transformed(location.Transformation()));
            const occ::handle<Geom_Curve> trimmed=new Geom_TrimmedCurve(curve,first,last);
            const auto projection=GeomProjLib::ProjectOnPlane(trimmed,plane,direction,true);
            if(projection.IsNull())throw std::runtime_error("boundary projection failed");
            BRepBuilderAPI_MakeEdge make(projection,first,last);
            if(!make.IsDone())throw std::runtime_error("projected boundary collapsed");
            auto low=make.Edge();low.Orientation(edge.Orientation());projected.Add(low);
            if(!projected.IsDone())throw std::runtime_error("projected boundary disconnected");
            const auto range=PlaneDistances(edge,stop);
            // A complete edge already on the stop has no side area. Do not
            // construct a degenerate swept face or discard any nonzero side.
            if(std::max(std::abs(range[0]),std::abs(range[1]))<=tolerance)continue;
            const auto side=BRepFill::Face(edge,low);
            if(side.IsNull())throw std::runtime_error("directional side construction failed");
            result.sides.push_back(side);
        }
        if(w.Current().IsSame(outer))projectedOuter=projected.Wire();
        else projectedHoles.push_back(projected.Wire());
    }
    if(projectedOuter.IsNull())throw std::runtime_error("projected outer boundary absent");
    BRepBuilderAPI_MakeFace bottom(plane,projectedOuter,true);
    for(const auto& wire:projectedHoles) {
        // Each loop is oriented in the target plane's parameterization, even
        // when the source orientation or the projection direction is reversed.
        BRepBuilderAPI_MakeFace hole(plane,wire,true);
        if(!hole.IsDone())throw std::runtime_error("projected hole invalid");
        auto inner=BRepTools::OuterWire(hole.Face());inner.Reverse();bottom.Add(inner);
    }
    if(!bottom.IsDone()||!BRepCheck_Analyzer(bottom.Face()).IsValid())throw std::runtime_error("projected planar cap invalid");
    result.bottom=bottom.Face();return result;
}

TopTools_MapOfShape FollowCap(const BRepBuilderAPI_Sewing& sewing,const TopoDS_Face& face) {
    TopTools_MapOfShape result;
    const auto& mapped=sewing.IsModified(face)?sewing.Modified(face):face;
    for(TopExp_Explorer e(mapped,TopAbs_FACE);e.More();e.Next())result.Add(e.Current());
    return result;
}

Out Close(Caps& caps,const TopoDS_Face& target,const geometry::GeometryTolerance& tolerance) {
    const double eps=tolerance.modelLinearMm;
    if(!Covered(caps.bottom,target,eps))return Out::Failure(base::MakeError("EXT-003",
        "終端面が押し出しの全域を覆っていません。","終端の範囲と穴を確認してください。"));
    // A previously fitted face can already contain measured curve/support
    // uncertainty above the document precision. Do not inherit arbitrary native
    // metadata (some imported vertices declare millimetres), or increase the
    // document's modelling tolerance. Bound only the join by measured input
    // consistency, retaining the original support and 3D curves.
    const double sourceError=VerifiedBoundaryError(caps.top,eps);
    BRepBuilderAPI_Sewing sewing(eps);sewing.SetMaxTolerance(sourceError);sewing.SetLocalTolerancesMode(false);
    sewing.SetSameParameterMode(false);
    sewing.Add(caps.top);sewing.Add(caps.bottom);for(const auto& side:caps.sides)sewing.Add(side);
    sewing.Perform();const auto shape=sewing.SewedShape();
    if(sewing.NbFreeEdges()!=0||sewing.NbMultipleEdges()!=0)return Failed("平面までの境界を閉じられませんでした。");
    TopExp_Explorer shells(shape,TopAbs_SHELL);
    if(!shells.More())return Failed("平面までの閉じた形がありません。");
    auto solid=BRepBuilderAPI_MakeSolid(TopoDS::Shell(shells.Current())).Solid();shells.Next();
    if(shells.More()||!BRepLib::OrientClosedSolid(solid)||!BRepCheck_Analyzer(solid).IsValid())
        return Failed("平面までを一つの閉じた立体にできませんでした。");
    StoredSolid stored{StoreShape(solid)};
    const auto quality=CheckSolidForExport(stored.handle,eps);
    if(!quality.HasValue()||!quality.Value().Ok())return Failed("平面までの立体が安全検査に通りませんでした。");
    ExtrudeBuildResult result;ExtrudedPart part;
    const auto wires=ExtrudeWires(solid,FollowCap(sewing,caps.top),FollowCap(sewing,caps.bottom),eps,part,result);
    if(!wires.HasValue())return Out::Failure(wires.Diagnostics());
    part.handle=stored.handle;part.volumeMm3=quality.Value().volumeMm3;
    for(TopExp_Explorer e(solid,TopAbs_FACE);e.More();e.Next())++part.faceCount;
    result.totalVolumeMm3=part.volumeMm3;result.totalFaceCount=part.faceCount;result.parts.push_back(std::move(part));
    stored.keep=true;
    return Out::Success(std::move(result));
}
}

std::optional<base::Result<ExtrudeBuildResult>> TryPlanarExtrudeClosure(
    const TopoDS_Shape& source,const TopoDS_Face& target,const geometry::Vector3& direction,
    const geometry::GeometryTolerance& tolerance) {
    const BRepAdaptor_Surface surface(target);
    if(surface.GetType()!=GeomAbs_Plane)return std::nullopt;
    TopExp_Explorer faces(source,TopAbs_FACE);if(!faces.More())return std::nullopt;
    const auto original=TopoDS::Face(faces.Current());faces.Next();
    if(faces.More()||BRepAdaptor_Surface(original).GetType()==GeomAbs_Plane)return std::nullopt;
    for(TopExp_Explorer e(original,TopAbs_EDGE);e.More();e.Next())
        if(BRep_Tool::Degenerated(TopoDS::Edge(e.Current())))return std::nullopt;
    const auto plane=surface.Plane();const gp_Dir axis(ToVector(direction));
    const double cosine=axis.Dot(plane.Axis().Direction());
    if(std::abs(cosine)<=tolerance.modelAngularRad)return Failed("押し出す向きが終端面に平行です。");
    const auto copied=BRepBuilderAPI_Copy(original,true,false).Shape();
    const auto range=PlaneDistances(copied,plane);
    const double first=-range[0]/cosine,last=-range[1]/cosine;
    if(std::min(first,last)<-tolerance.modelLinearMm)
        return Out::Failure(base::MakeError("EXT-003","元の面の一部が終端を越えています。","押し出す向きか終端面を変更してください。"));
    if(std::max(first,last)<=tolerance.modelLinearMm)
        return Out::Failure(base::MakeError("EXT-003","押し出し範囲がありません。",{}));
    if(std::min(first,last)>tolerance.modelLinearMm)return std::nullopt;
    auto caps=ProjectCaps(TopoDS::Face(copied),plane,axis,tolerance.modelLinearMm);
    return Close(caps,target,tolerance);
}
}
#endif
