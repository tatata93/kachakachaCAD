#include "kachakacha/kernel/OcctGptSurface.h"

#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctLoftSurface.h"
#include "kachakacha/geometry/CurveSampling.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepOffsetAPI_MakeFilling.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Shape.hxx>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#endif

namespace kachakacha::v2::kernel {
using base::MakeError;
using base::Result;
using modeling::GuideSurfaceResult;
#ifdef KACHACAD_V2_WITH_OCCT
namespace {
Result<TopoDS_Shape> GenerateBoundary(const app::GptSurfaceRequest& request,
    const geometry::GeometryTolerance& tolerance)
{
    const auto& outer = request.curves.front();
    auto wire = ToWire(outer.segments, tolerance.modelLinearMm);
    if (!wire.HasValue()) { return Result<TopoDS_Shape>::Failure(wire.Diagnostics()); }
    // 平面も内部拘束も、最後に同じ実形状偏差検査を通す。
    std::vector<geometry::Vector3> points;
    for (const auto& curve : request.curves) {
        auto sampled = geometry::SampleChain(curve.segments, tolerance.modelLinearMm);
        points.insert(points.end(), sampled.begin(), sampled.end());
    }
    const auto plane = geometry::FitPlane(points);
    if (plane.valid && plane.maximumDeviationMm <= tolerance.modelLinearMm) {
        BRepBuilderAPI_MakeFace face(wire.Value(), true);
        if (face.IsDone()) { return Result<TopoDS_Shape>::Success(face.Shape()); }
    }
    BRepOffsetAPI_MakeFilling filling;
    filling.SetConstrParam(tolerance.modelLinearMm, tolerance.modelLinearMm,
        tolerance.modelAngularRad, tolerance.modelLinearMm);
    for (const auto& curve : request.curves) {
        for (const auto& segment : curve.segments) {
            const auto edge = ToEdge(segment);
            if (!edge.HasValue()) { return Result<TopoDS_Shape>::Failure(edge.Diagnostics()); }
            filling.Add(edge.Value(), GeomAbs_C0, curve.role == app::kGptBoundaryRole);
        }
    }
    filling.Build();
    if (!filling.IsDone()) {
        return Result<TopoDS_Shape>::Failure(MakeError("GPT-S003",
            "指定した外周と通る線から面を張れませんでした。", "線同士の交差や矛盾する高さを確認してください。"));
    }
    return Result<TopoDS_Shape>::Success(filling.Shape());
}

Result<TopoDS_Shape> GenerateSections(const app::GptSurfaceRequest& request,
    const geometry::GeometryTolerance& tolerance)
{
    BRepOffsetAPI_ThruSections loft(false, false, tolerance.modelLinearMm);
    // 入力の向き・継ぎ目を利用者が指定するため、自動反転はしない。
    loft.CheckCompatibility(false);
    for (const auto& curve : request.curves) {
        const auto wire = ToWire(curve.segments, tolerance.modelLinearMm);
        if (!wire.HasValue()) { return Result<TopoDS_Shape>::Failure(wire.Diagnostics()); }
        loft.AddWire(wire.Value());
    }
    loft.Build();
    if (!loft.IsDone()) {
        return Result<TopoDS_Shape>::Failure(MakeError("GPT-S003",
            "指定した断面をつなげませんでした。", "重なった断面や断面の順序を確認してください。"));
    }
    return Result<TopoDS_Shape>::Success(loft.Shape());
}

struct Deviation { double maximum = 0.0; double squared = 0.0; std::size_t count = 0; };

Result<Deviation> Measure(const TopoDS_Shape& shape, const app::GptSurfaceRequest& request,
    const geometry::GeometryTolerance& tolerance)
{
    Deviation measured;
    for (const auto& curve : request.curves) {
        for (const auto& segment : curve.segments) {
            // 適応標本に均等標本を加え、直線の両端だけの検査にしない。
            auto samples = geometry::SampleCurve(segment,
                std::max(tolerance.modelLinearMm, request.maximumDeviationMm / 10.0));
            for (int step = 0; step <= 32; ++step) {
                const double parameter = static_cast<double>(step) / 32.0;
                samples.push_back({parameter, segment.Evaluate(parameter)});
            }
            for (const auto& sample : samples) {
                const auto vertex = BRepBuilderAPI_MakeVertex(ToPoint(sample.position)).Vertex();
                BRepExtrema_DistShapeShape distance(vertex, shape);
                distance.Perform();
                if (!distance.IsDone() || distance.NbSolution() == 0 || !std::isfinite(distance.Value())) {
                    return Result<Deviation>::Failure(MakeError("GPT-S004",
                        curve.label + "から面へのずれを測れませんでした。", "入力線の形を確認して作り直してください。"));
                }
                const double value = distance.Value();
                measured.maximum = std::max(measured.maximum, value);
                measured.squared += value * value;
                ++measured.count;
            }
        }
    }
    if (measured.count == 0) {
        return Result<Deviation>::Failure(MakeError("GPT-S004", "測定する線がありません。", "入力線を指定してください。"));
    }
    return Result<Deviation>::Success(measured);
}
} // namespace
#endif

Result<GuideSurfaceResult> BuildGptSurface(const app::GptSurfaceRequest& raw,
    const geometry::GeometryTolerance& tolerance)
{
    const auto checked = app::ValidateGptSurface(raw, tolerance);
    if (!checked.HasValue()) { return Result<GuideSurfaceResult>::Failure(checked.Diagnostics()); }
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        const auto& request = checked.Value();
        const auto shape = request.loft ? GenerateSections(request, tolerance) : GenerateBoundary(request, tolerance);
        if (!shape.HasValue()) { return Result<GuideSurfaceResult>::Failure(shape.Diagnostics()); }
        if (!BRepCheck_Analyzer(shape.Value()).IsValid()) {
            return Result<GuideSurfaceResult>::Failure(MakeError("GPT-S003",
                "生成した面の形状検査に失敗しました。", "外周の交差または断面の順序を確認してください。"));
        }
        const auto deviation = Measure(shape.Value(), request, tolerance);
        if (!deviation.HasValue()) { return Result<GuideSurfaceResult>::Failure(deviation.Diagnostics()); }
        if (deviation.Value().maximum > request.maximumDeviationMm) {
            std::ostringstream detail;
            detail << std::setprecision(8) << "最大 " << deviation.Value().maximum
                << " mm / 許容 " << request.maximumDeviationMm << " mm。入力を見直してください。";
            return Result<GuideSurfaceResult>::Failure(MakeError("GPT-S005",
                "近似面が入力線から許容以上に外れています。", detail.str()));
        }
        auto result = detail::FinishSurfaceResult(shape.Value(), tolerance);
        if (!result.HasValue()) { return result; }
        auto value = result.Value();
        value.maximumDeviationMm = deviation.Value().maximum;
        value.rmsDeviationMm = std::sqrt(deviation.Value().squared / static_cast<double>(deviation.Value().count));
        return Result<GuideSurfaceResult>::Success(std::move(value), result.Diagnostics());
    } catch (const Standard_Failure&) {
        return Result<GuideSurfaceResult>::Failure(MakeError("GPT-S003",
            "GPT版の面生成を完了できませんでした。", "入力線の交差・接続・断面順を確認してください。"));
    }
#else
    (void)tolerance;
    return Result<GuideSurfaceResult>::Failure(MakeError("GPT-S006",
        "この構成には面生成カーネルがありません。", "Windowsの通常版で実行してください。"));
#endif
}
} // namespace kachakacha::v2::kernel
