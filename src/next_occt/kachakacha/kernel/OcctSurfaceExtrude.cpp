#include "kachakacha/kernel/OcctSurfaceExtrude.h"
#include <cmath>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctCurveConversion.h"
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>
#endif

namespace kachakacha::v2::kernel {
using base::Result;
using base::MakeError;
using modeling::KernelShapeHandle;
using geometry::Vector3;
using geometry::GeometryTolerance;

Result<ExtrudeBuildResult> BuildSurfaceExtrude(KernelShapeHandle source,
    const Vector3& direction, double startMm, double endMm, const GeometryTolerance& tolerance)
{
    using Out = Result<ExtrudeBuildResult>;
#ifdef KACHACAD_V2_WITH_OCCT
    if (!direction.IsFinite() || direction.Length() < tolerance.modelLinearMm
        || !std::isfinite(startMm) || !std::isfinite(endMm)
        || std::abs(endMm - startMm) <= tolerance.modelLinearMm) {
        return Out::Failure(MakeError("EXT-007", "押し出し方向と距離を指定してください。", {}));
    }
    TopoDS_Shape shape;
    if (!LookupShape(source, shape) || shape.IsNull()) {
        return Out::Failure(MakeError("EXT-008", "押し出す面の実形状がありません。", {}));
    }
    try {
        const Vector3 unit = geometry::Normalized(direction, 1.0e-12);
        gp_Trsf shift;
        shift.SetTranslation(ToVector(unit * startMm));
        TopoDS_Shape combined;
        for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next()) {
            const TopoDS_Shape first = BRepBuilderAPI_Transform(faces.Current(), shift, true).Shape();
            BRepPrimAPI_MakePrism prism(first, ToVector(unit * (endMm - startMm)), true, true);
            if (!prism.IsDone()) return Out::Failure(MakeError("EXT-008", "面を押し出せませんでした。", {}));
            if (combined.IsNull()) combined = prism.Shape();
            else {
                BRepAlgoAPI_Fuse fuse(combined, prism.Shape());
                if (!fuse.IsDone()) return Out::Failure(MakeError("EXT-008", "面の継ぎ目を接合できません。", {}));
                combined = fuse.Shape();
            }
        }
        if (combined.IsNull() || !BRepCheck_Analyzer(combined).IsValid()) {
            return Out::Failure(MakeError("EXT-008", "この方向では閉じた立体を作れません。",
                "面と押し出した面が交差する場合は、向きまたは距離を変更してください。"));
        }
        ExtrudeBuildResult result;
        for (TopExp_Explorer solids(combined, TopAbs_SOLID); solids.More(); solids.Next()) {
            auto solid = TopoDS::Solid(solids.Current());
            if (!BRepLib::OrientClosedSolid(solid)) {
                return Out::Failure(MakeError("EXT-008", "押し出し結果が閉じていません。", {}));
            }
            GProp_GProps properties;
            BRepGProp::VolumeProperties(solid, properties);
            const double volume = std::abs(properties.Mass());
            if (volume <= std::pow(tolerance.modelLinearMm, 3)) {
                return Out::Failure(MakeError("EXT-007", "この方向では面に体積が付きません。", {}));
            }
            ExtrudedPart part;
            part.handle = StoreShape(solid);
            part.volumeMm3 = volume;
            for (TopExp_Explorer face(solid, TopAbs_FACE); face.More(); face.Next()) ++part.faceCount;
            result.totalVolumeMm3 += volume;
            result.totalFaceCount += part.faceCount;
            result.parts.push_back(part);
        }
        if (result.parts.empty()) return Out::Failure(MakeError("EXT-008", "立体を作れませんでした。", {}));
        return Out::Success(std::move(result));
    } catch (const Standard_Failure& error) {
        return Out::Failure(MakeError("EXT-008", "面の押し出しに失敗しました。", error.GetMessageString()));
    } catch (...) {
        return Out::Failure(MakeError("EXT-008", "面の押し出しに失敗しました。", {}));
    }
#else
    (void)source; (void)direction; (void)startMm; (void)endMm; (void)tolerance;
    return Out::Failure(MakeError("KER-E005", "幾何カーネルが必要です。", {}));
#endif
}
} // namespace kachakacha::v2::kernel
