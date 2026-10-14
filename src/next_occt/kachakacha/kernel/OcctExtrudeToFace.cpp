#include "kachakacha/kernel/OcctSurfaceExtrude.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <algorithm>
#include <cmath>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctCurveConversion.h"
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRep_Tool.hxx>
#include <BRepBndLib.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <Bnd_Box.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Trsf.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include "kachakacha/kernel/OcctMassProperties.h"
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#endif

namespace kachakacha::v2::kernel {
using base::Result;
using base::MakeError;
using modeling::KernelShapeHandle;
using modeling::ExtrudeRequest;
using geometry::Vector3;
using geometry::GeometryTolerance;
using Out = Result<ExtrudeBuildResult>;
#ifdef KACHACAD_V2_WITH_OCCT
namespace {
struct TemporaryShapes {
    std::vector<KernelShapeHandle> handles;
    ~TemporaryShapes() { for (const auto handle : handles) ReleaseShape(handle); }
};
Result<KernelShapeHandle> ProfileSource(const ExtrudeRequest& original, const GeometryTolerance& tolerance)
{
    auto request = original;
    request.extent = modeling::ExtrudeExtentMode::Distance;
    request.distanceMm = 1.0;
    request.booleanMode = modeling::ExtrudeBooleanMode::NewPart;
    const auto analysis = modeling::AnalyzeExtrudeRequest(request, tolerance);
    if (!analysis.HasValue()) return Result<KernelShapeHandle>::Failure(analysis.Diagnostics());
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    const auto& plane = analysis.Value().profilePlane;
    for (const auto& loop : analysis.Value().loops) {
        if (loop.isHole) continue;
        const auto wire = ToWire(request.profiles[loop.profileIndex].segments, tolerance.modelLinearMm);
        if (!wire.HasValue()) return Result<KernelShapeHandle>::Failure(wire.Diagnostics());
        BRepBuilderAPI_MakeFace face(gp_Pln(ToPoint(plane.origin), gp_Dir(ToVector(plane.normal))), wire.Value(), true);
        for (const auto hole : loop.holes) {
            const auto inner = ToWire(request.profiles[hole].segments, tolerance.modelLinearMm);
            if (!inner.HasValue()) return Result<KernelShapeHandle>::Failure(inner.Diagnostics());
            auto reversed = inner.Value(); reversed.Reverse(); face.Add(reversed);
        }
        if (!face.IsDone()) return Result<KernelShapeHandle>::Failure(MakeError("EXT-008", "輪郭の面を作れません。", {}));
        builder.Add(compound, face.Face());
    }
    return Result<KernelShapeHandle>::Success(StoreShape(compound));
}

Result<TopoDS_Face> StopFace(const ExtrudeRequest& request, KernelShapeHandle target)
{
    using FaceResult = Result<TopoDS_Face>;
    TopoDS_Shape shape;
    if (LookupShape(target, shape)) {
        TopExp_Explorer faces(shape, TopAbs_FACE);
        if (!faces.More()) return FaceResult::Failure(MakeError("EXT-003", "終端の面がありません。", {}));
        const auto face = TopoDS::Face(faces.Current());
        faces.Next();
        if (faces.More()) return FaceResult::Failure(MakeError("EXT-003", "終端は1枚の面を指定してください。", {}));
        return FaceResult::Success(face);
    }
    if (request.targetKind != modeling::ExtrudeTargetKind::Plane) {
        return FaceResult::Failure(MakeError("EXT-003", "終端の面・線・点を選択してください。", {}));
    }
    const auto& plane = request.targetPlane;
    BRepBuilderAPI_MakeFace maker(gp_Pln(ToPoint(plane.origin), gp_Dir(ToVector(plane.normal))));
    return maker.IsDone() ? FaceResult::Success(maker.Face())
        : FaceResult::Failure(MakeError("EXT-003", "終端面を作れません。", {}));
}

Result<double> Reach(const modeling::ShapeMesh& mesh, const TopoDS_Face& target,
    const Vector3& direction, double tolerance)
{
    IntCurvesFace_ShapeIntersector intersector;
    intersector.Load(target, tolerance);
    double maximum = 0.0;
    std::vector<Vector3> points;
    for (const auto& edge : mesh.edges) points.insert(points.end(), edge.begin(), edge.end());
    for (const auto& triangle : mesh.triangles) points.push_back(triangle.Center());
    for (const auto& point : points) {
        intersector.Perform(gp_Lin(ToPoint(point), gp_Dir(ToVector(direction))), tolerance, 1.0e8);
        if (!intersector.IsDone() || intersector.NbPnt() == 0) {
            return Result<double>::Failure(MakeError("EXT-003", "終端面に届かない場所があります。",
                "押し出す向き、終端の大きさ、元の面との交差を確認してください。"));
        }
        double nearest = 1.0e8;
        for (int i = 1; i <= intersector.NbPnt(); ++i) nearest = std::min(nearest, intersector.WParameter(i));
        maximum = std::max(maximum, nearest);
    }
    if (maximum <= tolerance) return Result<double>::Failure(MakeError("EXT-003", "押し出し範囲がありません。", {}));
    return Result<double>::Success(maximum);
}

// Exact BRep bounds include extrema between tessellation samples. The temporary
// prism must extend beyond every point of a finite curved stop, not just rays.
double ConservativeReach(KernelShapeHandle source,const TopoDS_Face& target,
    const Vector3& direction,double sampled)
{
    TopoDS_Shape shape;if(!LookupShape(source,shape))return sampled;
    Bnd_Box from,to;BRepBndLib::AddOptimal(shape,from,false,true);
    if(from.IsVoid()||from.IsOpen())return sampled;
    BRepAdaptor_Surface surface(target);
    if(surface.GetType()==GeomAbs_Plane) {
        const auto plane=surface.Plane();const auto n=plane.Axis().Direction();const auto p=plane.Location();
        const Vector3 normal{n.X(),n.Y(),n.Z()}, origin{p.X(),p.Y(),p.Z()};
        const double cosine=geometry::Dot(direction,normal);if(std::abs(cosine)<1e-12)return sampled;
        double x0,y0,z0,x1,y1,z1;from.Get(x0,y0,z0,x1,y1,z1);
        for(double x:{x0,x1})for(double y:{y0,y1})for(double z:{z0,z1})
            sampled=std::max(sampled,geometry::Dot(origin-Vector3{x,y,z},normal)/cosine);
        return sampled;
    }
    BRepBndLib::AddOptimal(target,to,false,true);
    if(to.IsVoid()||to.IsOpen())return sampled;
    const auto extreme=[&](const Bnd_Box& box,bool maximum){
        double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
        double result=maximum?-1e100:1e100;
        for(double x:{x0,x1})for(double y:{y0,y1})for(double z:{z0,z1}) {
            const double value=geometry::Dot(Vector3{x,y,z},direction);
            result=maximum?std::max(result,value):std::min(result,value);
        }
        return result;
    };
    return std::max(sampled,extreme(to,true)-extreme(from,false));
}

// Compare the entire cap, rather than sampled rays, with the finite stop face.
// In particular, a small hole between samples must not be silently filled.
bool CapCovered(const TopoDS_Shape& solid, const TopoDS_Face& target, double tolerance)
{
    BRepBuilderAPI_MakeFace support(BRep_Tool::Surface(target), tolerance);
    if (!support.IsDone()) return false;
    BRepAlgoAPI_Common section(solid, support.Face());
    if (!section.IsDone()) return false;
    BRepAlgoAPI_Cut outside(section.Shape(), target);
    if (!outside.IsDone()) return false;
    GProp_GProps total, missing;
    BRepGProp::SurfaceProperties(section.Shape(), total);
    BRepGProp::SurfaceProperties(outside.Shape(), missing);
    return total.Mass() > tolerance * tolerance
        && std::abs(missing.Mass()) <= tolerance * tolerance;
}

Out Trim(const ExtrudeBuildResult& extended, const TopoDS_Face& target, const Vector3& inside,
    double tolerance)
{
    BRepPrimAPI_MakeHalfSpace half(target, ToPoint(inside));
    if (!half.IsDone()) return Out::Failure(MakeError("EXT-003", "終端面の手前側を決定できません。", {}));
    ExtrudeBuildResult result;
    TemporaryShapes created;
    for (const auto& part : extended.parts) {
        TopoDS_Shape solid;
        if (!LookupShape(part.handle, solid)) return Out::Failure(MakeError("EXT-008", "押し出しの形がありません。", {}));
        if (!CapCovered(solid, target, tolerance)) {
            return Out::Failure(MakeError("EXT-003", "終端面が押し出しの全域を覆っていません。",
                "終端の外周や穴を確認し、全域を覆う面を選んでください。"));
        }
        BRepAlgoAPI_Common cut(solid, half.Solid());
        if (!cut.IsDone() || !BRepCheck_Analyzer(cut.Shape()).IsValid()) {
            return Out::Failure(MakeError("EXT-008", "終端面で閉じた立体を作れません。", {}));
        }
        for (TopExp_Explorer solids(cut.Shape(), TopAbs_SOLID); solids.More(); solids.Next()) {
            GProp_GProps volume;
            detail::VolumeProperties(solids.Current(), volume);
            ExtrudedPart made;
            made.handle = StoreShape(solids.Current());
            created.handles.push_back(made.handle);
            made.volumeMm3 = std::abs(volume.Mass());
            for (TopExp_Explorer faces(solids.Current(), TopAbs_FACE); faces.More(); faces.Next()) ++made.faceCount;
            result.totalVolumeMm3 += made.volumeMm3;
            result.totalFaceCount += made.faceCount;
            result.parts.push_back(made);
        }
    }
    if (result.parts.empty() || result.totalVolumeMm3 <= 0.0) return Out::Failure(MakeError("EXT-008", "終端面までの立体を作れません。", {}));
    created.handles.clear();
    return Out::Success(std::move(result));
}
} // namespace
#endif

Out BuildExtrudeToFace(const ExtrudeRequest& request, KernelShapeHandle source,
    KernelShapeHandle target, const GeometryTolerance& tolerance, double targetOffsetMm)
{
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        TemporaryShapes temporary;
        if (!request.customDirection.IsFinite() || request.customDirection.Length() < 1.0e-9) {
            return Out::Failure(MakeError("EXT-007", "押し出す方向を指定してください。", {}));
        }
        TopoDS_Shape shape;
        if (!LookupShape(source, shape)) {
            const auto made = ProfileSource(request, tolerance);
            if (!made.HasValue()) return Out::Failure(made.Diagnostics());
            source = made.Value();
            temporary.handles.push_back(source);
        }
        if (!std::isfinite(targetOffsetMm)) return Out::Failure(MakeError("EXT-003", "終端オフセットが不正です。", {}));
        auto targetFace = StopFace(request, target);
        if (!targetFace.HasValue()) return Out::Failure(targetFace.Diagnostics());
        const auto mesh = BuildShapeMesh(source);
        if (!mesh.HasValue() || mesh.Value().triangles.empty()) return Out::Failure(MakeError("EXT-008", "押し出す面がありません。", {}));
        const Vector3 direction = geometry::Normalized(request.customDirection, 1.0e-12)
            * (request.reversed ? -1.0 : 1.0);
        if (targetOffsetMm != 0.0) {
            gp_Trsf shift; shift.SetTranslation(ToVector(direction * targetOffsetMm));
            BRepBuilderAPI_Transform moved(targetFace.Value(), shift, true);
            if (!moved.IsDone()) return Out::Failure(MakeError("EXT-003", "終端面を移動できません。", {}));
            targetFace = Result<TopoDS_Face>::Success(TopoDS::Face(moved.Shape()));
        }
        const auto reach = Reach(mesh.Value(), targetFace.Value(), direction, tolerance.modelLinearMm);
        if (!reach.HasValue()) return Out::Failure(reach.Diagnostics());
        const double length=ConservativeReach(source,targetFace.Value(),direction,reach.Value());
        const auto extended = BuildSurfaceExtrude(source, direction, 0.0,
            length + std::max(1.0, length * 0.1), tolerance);
        if (!extended.HasValue()) return extended;
        for (const auto& part : extended.Value().parts) temporary.handles.push_back(part.handle);
        return Trim(extended.Value(), targetFace.Value(), mesh.Value().triangles.front().Center(), tolerance.modelLinearMm);
    } catch (const Standard_Failure& error) {
        return Out::Failure(MakeError("EXT-008", "終端面まで押し出せませんでした。", error.GetMessageString()));
    } catch (...) {
        return Out::Failure(MakeError("EXT-008", "終端面まで押し出せませんでした。", {}));
    }
#else
    (void)request; (void)source; (void)target; (void)tolerance; (void)targetOffsetMm;
    return Out::Failure(MakeError("KER-E005", "幾何カーネルが必要です。", {}));
#endif
}
} // namespace kachakacha::v2::kernel
