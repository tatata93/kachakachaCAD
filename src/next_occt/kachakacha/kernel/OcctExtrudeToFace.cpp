#include "kachakacha/kernel/OcctSurfaceExtrude.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include <algorithm>
#include <cmath>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctCurveConversion.h"
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
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

Out Trim(const ExtrudeBuildResult& extended, const TopoDS_Face& target, const Vector3& inside)
{
    BRepPrimAPI_MakeHalfSpace half(target, ToPoint(inside));
    if (!half.IsDone()) return Out::Failure(MakeError("EXT-003", "終端面の手前側を決定できません。", {}));
    ExtrudeBuildResult result;
    for (const auto& part : extended.parts) {
        TopoDS_Shape solid;
        if (!LookupShape(part.handle, solid)) return Out::Failure(MakeError("EXT-008", "押し出しの形がありません。", {}));
        BRepAlgoAPI_Common cut(solid, half.Solid());
        if (!cut.IsDone() || !BRepCheck_Analyzer(cut.Shape()).IsValid()) {
            return Out::Failure(MakeError("EXT-008", "終端面で閉じた立体を作れません。", {}));
        }
        for (TopExp_Explorer solids(cut.Shape(), TopAbs_SOLID); solids.More(); solids.Next()) {
            GProp_GProps volume;
            BRepGProp::VolumeProperties(solids.Current(), volume);
            ExtrudedPart made;
            made.handle = StoreShape(solids.Current());
            made.volumeMm3 = std::abs(volume.Mass());
            for (TopExp_Explorer faces(solids.Current(), TopAbs_FACE); faces.More(); faces.Next()) ++made.faceCount;
            result.totalVolumeMm3 += made.volumeMm3;
            result.totalFaceCount += made.faceCount;
            result.parts.push_back(made);
        }
    }
    if (result.parts.empty() || result.totalVolumeMm3 <= 0.0) return Out::Failure(MakeError("EXT-008", "終端面までの立体を作れません。", {}));
    return Out::Success(std::move(result));
}
} // namespace
#endif

Out BuildExtrudeToFace(const ExtrudeRequest& request, KernelShapeHandle source,
    KernelShapeHandle target, const GeometryTolerance& tolerance)
{
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        if (!request.customDirection.IsFinite() || request.customDirection.Length() < 1.0e-9) {
            return Out::Failure(MakeError("EXT-007", "押し出す方向を指定してください。", {}));
        }
        TopoDS_Shape shape;
        if (!LookupShape(source, shape)) {
            const auto made = ProfileSource(request, tolerance);
            if (!made.HasValue()) return Out::Failure(made.Diagnostics());
            source = made.Value();
        }
        const auto targetFace = StopFace(request, target);
        if (!targetFace.HasValue()) return Out::Failure(targetFace.Diagnostics());
        const auto mesh = BuildShapeMesh(source);
        if (!mesh.HasValue() || mesh.Value().triangles.empty()) return Out::Failure(MakeError("EXT-008", "押し出す面がありません。", {}));
        const Vector3 direction = geometry::Normalized(request.customDirection, 1.0e-12)
            * (request.reversed ? -1.0 : 1.0);
        const auto reach = Reach(mesh.Value(), targetFace.Value(), direction, tolerance.modelLinearMm);
        if (!reach.HasValue()) return Out::Failure(reach.Diagnostics());
        const auto extended = BuildSurfaceExtrude(source, direction, 0.0,
            reach.Value() + std::max(1.0, reach.Value() * 0.1), tolerance);
        if (!extended.HasValue()) return extended;
        return Trim(extended.Value(), targetFace.Value(), mesh.Value().triangles.front().Center());
    } catch (const Standard_Failure& error) {
        return Out::Failure(MakeError("EXT-008", "終端面まで押し出せませんでした。", error.GetMessageString()));
    } catch (...) {
        return Out::Failure(MakeError("EXT-008", "終端面まで押し出せませんでした。", {}));
    }
#else
    (void)request; (void)source; (void)target; (void)tolerance;
    return Out::Failure(MakeError("KER-E005", "幾何カーネルが必要です。", {}));
#endif
}
} // namespace kachakacha::v2::kernel
