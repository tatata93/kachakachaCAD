#include "kachakacha/kernel/OcctPanelSolid.h"

#include <cmath>
#include <string>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Vec.hxx>

#include <exception>

#endif

namespace kachakacha::v2::kernel {
namespace {

using base::MakeError;
using base::Result;

} // namespace

#ifdef KACHACAD_V2_WITH_OCCT

namespace {

//! 輪郭の載っている面の法線。最初の3点から出す。
[[nodiscard]] Result<geometry::Vector3> OutlineNormal(
    const std::vector<geometry::Vector3>& outline)
{
    using Out = Result<geometry::Vector3>;
    for (std::size_t at = 2; at < outline.size(); ++at) {
        const geometry::Vector3 first = outline[1] - outline[0];
        const geometry::Vector3 second = outline[at] - outline[0];
        const geometry::Vector3 cross{first.y * second.z - first.z * second.y,
            first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x};
        const double length = cross.Length();
        if (length > 1.0e-9) {
            return Out::Success(cross * (1.0 / length));
        }
    }
    return Out::Failure(MakeError(kPanelSolidFailed,
        "折った立体が正しく作れませんでした。",
        "輪郭が一直線に並んでいて、面の向きを決められません。"));
}

} // namespace

Result<BuiltPanelSolid> BuildPanelSolid(const fabrication::PanelSolidRequest& request,
    const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<BuiltPanelSolid>;
    if (request.outline.size() < 3) {
        return Out::Failure(MakeError(kPanelSolidFailed,
            "折った立体が正しく作れませんでした。",
            "輪郭の点が足りません: " + request.panelId));
    }
    if (!(request.thicknessMm > 0.0)) {
        return Out::Failure(MakeError(kPanelSolidFailed,
            "折った立体が正しく作れませんでした。",
            "厚みが正の数ではありません: " + request.panelId));
    }
    for (const geometry::Vector3& point : request.outline) {
        if (!point.IsFinite()) {
            return Out::Failure(MakeError(kPanelSolidFailed,
                "折った立体が正しく作れませんでした。",
                "輪郭に数値でない点があります: " + request.panelId));
        }
    }
    const auto normal = OutlineNormal(request.outline);
    if (!normal.HasValue()) {
        return Out::Failure(normal.Diagnostics());
    }

    // 厚みの付け方で、面をどれだけずらしてから押し出すかが変わる。
    double offset = 0.0;
    switch (request.placement) {
    case fabrication::ThicknessPlacement::Outside:
        offset = 0.0;
        break;
    case fabrication::ThicknessPlacement::Centered:
        offset = -request.thicknessMm * 0.5;
        break;
    case fabrication::ThicknessPlacement::Inside:
        offset = -request.thicknessMm;
        break;
    }

    try {
        std::vector<geometry::CurveSegment> loop;
        const std::size_t count = request.outline.size();
        for (std::size_t at = 0; at < count; ++at) {
            const geometry::Vector3 from =
                request.outline[at] + normal.Value() * offset;
            const geometry::Vector3 to =
                request.outline[(at + 1) % count] + normal.Value() * offset;
            if ((to - from).Length() <= tolerance.modelLinearMm) {
                continue;   // 同じ点が続いている。飛ばす。
            }
            const auto line = geometry::CurveSegment::MakeLine(from, to);
            if (!line.HasValue()) {
                return Out::Failure(line.Diagnostics());
            }
            loop.push_back(line.Value());
        }
        if (loop.size() < 3) {
            return Out::Failure(MakeError(kPanelSolidFailed,
                "折った立体が正しく作れませんでした。",
                "つぶれた輪郭です: " + request.panelId));
        }
        const auto wire = ToWire(loop, tolerance.interactiveJoinMm);
        if (!wire.HasValue()) {
            return Out::Failure(wire.Diagnostics());
        }
        BRepBuilderAPI_MakeFace face(wire.Value(), Standard_True);
        if (!face.IsDone()) {
            return Out::Failure(MakeError(kPanelSolidFailed,
                "折った立体が正しく作れませんでした。",
                "輪郭を面にできませんでした: " + request.panelId));
        }
        const gp_Vec along(normal.Value().x * request.thicknessMm,
            normal.Value().y * request.thicknessMm,
            normal.Value().z * request.thicknessMm);
        BRepPrimAPI_MakePrism prism(face.Face(), along);
        if (!prism.IsDone()) {
            return Out::Failure(MakeError(kPanelSolidFailed,
                "折った立体が正しく作れませんでした。",
                "厚みを付けられませんでした: " + request.panelId));
        }
        const TopoDS_Shape solid = prism.Shape();
        GProp_GProps properties;
        BRepGProp::VolumeProperties(solid, properties);
        const double volume = std::abs(properties.Mass());
        if (volume <= 0.0) {
            return Out::Failure(MakeError(kPanelSolidFailed,
                "折った立体が正しく作れませんでした。",
                "体積がありません: " + request.panelId));
        }
        BuiltPanelSolid built;
        built.handle = StoreShape(solid);
        built.panelId = request.panelId;
        built.volumeMm3 = volume;
        built.thicknessMm = request.thicknessMm;
        return Out::Success(std::move(built));
    } catch (const std::exception& error) {
        return Out::Failure(MakeError(kPanelSolidFailed,
            "折った立体が正しく作れませんでした。",
            request.panelId + ": " + error.what()));
    } catch (...) {
        return Out::Failure(MakeError(kPanelSolidFailed,
            "折った立体が正しく作れませんでした。", request.panelId));
    }
}

#else // KACHACAD_V2_WITH_OCCT

Result<BuiltPanelSolid> BuildPanelSolid(const fabrication::PanelSolidRequest& request,
    const geometry::GeometryTolerance& tolerance)
{
    (void)request;
    (void)tolerance;
    return Result<BuiltPanelSolid>::Failure(MakeError(kPanelSolidFailed,
        "折った立体が正しく作れませんでした。",
        "この実行ファイルには幾何カーネルが入っていません。"));
}

#endif // KACHACAD_V2_WITH_OCCT

Result<std::vector<BuiltPanelSolid>> BuildPanelSolids(
    const fabrication::FreezeBundle& bundle, const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<std::vector<BuiltPanelSolid>>;
    if (bundle.parts.empty()) {
        return Out::Failure(MakeError("FAB-E003",
            "固定するもとの部品が見つかりません。", "束に部材がありません。"));
    }
    std::vector<BuiltPanelSolid> built;
    built.reserve(bundle.parts.size());
    for (const fabrication::PanelSolidRequest& part : bundle.parts) {
        // 1枚でも作れなければ全体を断る。半分だけ出来た立体を渡さない。
        const auto solid = BuildPanelSolid(part, tolerance);
        if (!solid.HasValue()) {
            return Out::Failure(solid.Diagnostics());
        }
        built.push_back(solid.Value());
    }
    return Out::Success(std::move(built));
}

} // namespace kachakacha::v2::kernel
