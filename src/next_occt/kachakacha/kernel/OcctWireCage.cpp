#include "kachakacha/kernel/OcctWireCage.h"

#include <string>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>

#include <cmath>
#include <exception>

#endif

namespace kachakacha::v2::kernel {
namespace {

using base::MakeError;
using base::Result;

} // namespace

#ifdef KACHACAD_V2_WITH_OCCT

namespace {

//! OCCT の例外を診断へ変える。UIイベントループの外へ漏らさない。
template<class Body>
[[nodiscard]] auto GuardedCage(Body body, const char* what) -> decltype(body())
{
    try {
        return body();
    } catch (const std::exception& error) {
        return decltype(body())::Failure(MakeError(kCageKernelFailure,
            "立体を作るところで幾何カーネルが失敗しました。",
            std::string(what) + ": " + error.what()));
    } catch (...) {
        return decltype(body())::Failure(MakeError(kCageKernelFailure,
            "立体を作るところで幾何カーネルが失敗しました。", what));
    }
}

//! パッチ1枚を面にする。
[[nodiscard]] Result<TopoDS_Face> MakePatchFace(
    const std::vector<modeling::CageEdgeInput>& edges, const modeling::CagePatch& patch,
    double toleranceMm)
{
    std::vector<geometry::CurveSegment> loop;
    loop.reserve(patch.edgeIndices.size());
    for (std::size_t at = 0; at < patch.edgeIndices.size(); ++at) {
        const std::size_t index = patch.edgeIndices[at];
        if (index >= edges.size()) {
            return Result<TopoDS_Face>::Failure(MakeError(kCageKernelFailure,
                "立体を作るところで幾何カーネルが失敗しました。",
                "面が指している線がありません。"));
        }
        loop.push_back(edges[index].segment);
    }
    const auto wire = ToWire(loop, toleranceMm);
    if (!wire.HasValue()) {
        return Result<TopoDS_Face>::Failure(wire.Diagnostics());
    }
    BRepBuilderAPI_MakeFace builder(wire.Value(), Standard_True);
    if (!builder.IsDone()) {
        return Result<TopoDS_Face>::Failure(MakeError(kCageKernelFailure,
            "立体を作るところで幾何カーネルが失敗しました。", "面にできませんでした。"));
    }
    return Result<TopoDS_Face>::Success(builder.Face());
}

//! 面を縫って立体にし、出来たものを検査する。
[[nodiscard]] Result<TopoDS_Solid> SewIntoSolid(const std::vector<TopoDS_Face>& faces,
    double toleranceMm)
{
    BRepBuilderAPI_Sewing sewing(toleranceMm * 10.0);
    for (const TopoDS_Face& face : faces) {
        sewing.Add(face);
    }
    sewing.Perform();
    const TopoDS_Shape sewn = sewing.SewedShape();
    if (sewn.IsNull()) {
        return Result<TopoDS_Solid>::Failure(MakeError(kCageKernelFailure,
            "立体を作るところで幾何カーネルが失敗しました。", "面を縫えませんでした。"));
    }
    TopExp_Explorer explorer(sewn, TopAbs_SHELL);
    if (!explorer.More()) {
        return Result<TopoDS_Solid>::Failure(MakeError(kCageInvalidSolid,
            "作った立体が閉じていません。", "縫った結果に殻がありません。"));
    }
    const TopoDS_Shell shell = TopoDS::Shell(explorer.Current());
    explorer.Next();
    if (explorer.More()) {
        // 殻が2つ以上出るのは、選んだ面が1つの立体を囲んでいない証拠である。
        return Result<TopoDS_Solid>::Failure(MakeError(kCageInvalidSolid,
            "作った立体が閉じていません。", "1つの立体になりませんでした。"));
    }
    BRepBuilderAPI_MakeSolid solidBuilder(shell);
    if (!solidBuilder.IsDone()) {
        return Result<TopoDS_Solid>::Failure(MakeError(kCageInvalidSolid,
            "作った立体が閉じていません。", "殻から立体にできませんでした。"));
    }
    const TopoDS_Solid solid = solidBuilder.Solid();
    if (!BRepCheck_Analyzer(solid).IsValid()) {
        return Result<TopoDS_Solid>::Failure(MakeError(kCageInvalidSolid,
            "作った立体が閉じていません。", "検査に通りませんでした。"));
    }
    return Result<TopoDS_Solid>::Success(solid);
}

} // namespace

Result<std::vector<BuiltCagePart>> BuildWireCageParts(
    const std::vector<modeling::CageEdgeInput>& edges,
    const modeling::WireCageAnalysis& analysis,
    const std::vector<modeling::WireCagePart>& plan,
    const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<std::vector<BuiltCagePart>>;
    if (plan.empty()) {
        return Out::Failure(MakeError(kCageKernelFailure,
            "立体を作るところで幾何カーネルが失敗しました。",
            "作る立体が1つも選ばれていません。"));
    }
    return GuardedCage([&]() -> Out {
        std::vector<BuiltCagePart> built;
        built.reserve(plan.size());
        // 1つの計画 = 1つの立体。まとめて1つにしない(AT-GEO-013)。
        for (const modeling::WireCagePart& wanted : plan) {
            if (wanted.shellIndex >= analysis.shells.size()) {
                return Out::Failure(MakeError(kCageKernelFailure,
                    "立体を作るところで幾何カーネルが失敗しました。",
                    "選ばれた立体が候補にありません。"));
            }
            const modeling::CageShell& shell = analysis.shells[wanted.shellIndex];
            std::vector<TopoDS_Face> faces;
            faces.reserve(shell.patches.size());
            for (const modeling::CagePatch& patch : shell.patches) {
                const auto face = MakePatchFace(edges, patch, tolerance.modelLinearMm);
                if (!face.HasValue()) {
                    return Out::Failure(face.Diagnostics());
                }
                faces.push_back(face.Value());
            }
            const auto solid = SewIntoSolid(faces, tolerance.modelLinearMm);
            if (!solid.HasValue()) {
                return Out::Failure(solid.Diagnostics());
            }
            GProp_GProps properties;
            BRepGProp::VolumeProperties(solid.Value(), properties);
            const double volume = std::abs(properties.Mass());
            if (volume <= tolerance.modelLinearMm) {
                return Out::Failure(MakeError(kCageInvalidSolid,
                    "作った立体が閉じていません。", "体積がありません。"));
            }
            BuiltCagePart part;
            part.handle = StoreShape(solid.Value());
            part.shellIndex = wanted.shellIndex;
            part.volumeMm3 = volume;
            std::size_t faceCount = 0;
            for (TopExp_Explorer explorer(solid.Value(), TopAbs_FACE); explorer.More();
                explorer.Next()) {
                ++faceCount;
            }
            part.faceCount = faceCount;
            for (const modeling::SubshapeKey& key : wanted.faceKeys) {
                part.faceKeys.push_back(key.ToString());
            }
            built.push_back(std::move(part));
        }
        return Out::Success(std::move(built));
    }, "ワイヤーかごから立体を作る");
}

#else // KACHACAD_V2_WITH_OCCT

Result<std::vector<BuiltCagePart>> BuildWireCageParts(
    const std::vector<modeling::CageEdgeInput>& edges,
    const modeling::WireCageAnalysis& analysis,
    const std::vector<modeling::WireCagePart>& plan,
    const geometry::GeometryTolerance& tolerance)
{
    (void)edges;
    (void)analysis;
    (void)plan;
    (void)tolerance;
    return Result<std::vector<BuiltCagePart>>::Failure(MakeError(kCageKernelFailure,
        "立体を作るところで幾何カーネルが失敗しました。",
        "この実行ファイルには幾何カーネルが入っていません。"));
}

#endif // KACHACAD_V2_WITH_OCCT

} // namespace kachakacha::v2::kernel
