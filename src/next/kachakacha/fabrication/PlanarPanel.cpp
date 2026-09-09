#include "kachakacha/fabrication/PlanarPanel.h"

#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/WireEdit.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::fabrication {
namespace {

using base::MakeError;
using base::Result;
using geometry::CurveSegment;
using geometry::Point2;
using geometry::Vector3;

//! 線をぜんぶ点にする。平らかどうかも、2Dへ落とすのもこの点で決める。
[[nodiscard]] std::vector<Vector3> SampleAll(const std::vector<CurveSegment>& curves,
    double toleranceMm)
{
    std::vector<Vector3> points;
    for (const CurveSegment& curve : curves) {
        for (const auto& sample : geometry::SampleCurve(curve, toleranceMm)) {
            points.push_back(sample.position);
        }
    }
    return points;
}

} // namespace

PlanarityCheck CheckPlanar(const std::vector<CurveSegment>& curves, double toleranceMm)
{
    PlanarityCheck check;
    const std::vector<Vector3> points = SampleAll(curves, toleranceMm);
    if (points.size() < 3) {
        return check;
    }
    const geometry::PlaneFit fit = geometry::FitPlane(points);
    check.normal = fit.normal;
    check.origin = fit.origin;
    for (const Vector3& point : points) {
        const Vector3 delta{point.x - fit.origin.x, point.y - fit.origin.y,
            point.z - fit.origin.z};
        const double distance = std::abs(delta.x * fit.normal.x + delta.y * fit.normal.y
            + delta.z * fit.normal.z);
        check.maxDeviationMm = std::max(check.maxDeviationMm, distance);
    }
    check.planar = check.maxDeviationMm <= toleranceMm;
    return check;
}

Result<PatternPanel> BuildPlanarPanel(const PlanarPanelRequest& request,
    double toleranceMm)
{
    using Out = Result<PatternPanel>;
    if (request.boundary.empty()) {
        return Out::Failure(MakeError("FAB-P003", "型紙にする外周がありません。",
            "部材 " + request.panelId + " の境界の線が1本もありません。"));
    }
    std::vector<CurveSegment> everything = request.boundary;
    for (const auto& opening : request.openings) {
        everything.insert(everything.end(), opening.begin(), opening.end());
    }
    const PlanarityCheck check = CheckPlanar(everything, toleranceMm);
    if (!check.planar) {
        // 曲がった面は展開が要る。ここで近似すると、切ってから合わないことに気づく。
        return Out::Failure(MakeError("FAB-P004", "この部材は平らではありません。",
            "部材 " + request.panelId + " は平面から最大 "
                + std::to_string(check.maxDeviationMm)
                + " mm 外れています。曲がった面は展開してから型紙にしてください。"));
    }
    const geometry::PlaneFit fit{check.origin, check.normal};
    const geometry::PlanarFrame frame = geometry::MakeFrame(fit);

    PatternPanel panel;
    panel.panelId = request.panelId;
    panel.outline = geometry::ProjectToFrame(SampleAll(request.boundary, toleranceMm),
        frame);
    for (const auto& opening : request.openings) {
        panel.openings.push_back(
            geometry::ProjectToFrame(SampleAll(opening, toleranceMm), frame));
    }
    return Out::Success(std::move(panel));
}

Result<std::vector<PatternPanel>> BuildPlanarPanels(
    const std::vector<PlanarPanelRequest>& requests, double toleranceMm)
{
    using Out = Result<std::vector<PatternPanel>>;
    if (requests.empty()) {
        return Out::Failure(MakeError("FAB-P003", "型紙にする外周がありません。",
            "部材が1つも渡されていません。"));
    }
    std::vector<PatternPanel> panels;
    panels.reserve(requests.size());
    for (const PlanarPanelRequest& request : requests) {
        const auto built = BuildPlanarPanel(request, toleranceMm);
        if (!built.HasValue()) {
            // 1枚でも作れないなら、そこで止める。
            // 途中まで作って渡すと、足りないことに気づかないまま切ることになる。
            return Out::Failure(built.Diagnostics());
        }
        panels.push_back(built.Value());
    }
    return Out::Success(std::move(panels));
}

Result<std::vector<exporters::PatternCurve>> PlacePanelCurves(const PatternPanel& panel,
    const PatternPlacement& placement)
{
    using Out = Result<std::vector<exporters::PatternCurve>>;
    if (panel.outline.size() < 2) {
        return Out::Failure(MakeError("FAB-P003", "型紙にする外周がありません。",
            "部材 " + panel.panelId + " の外周に点が足りません。"));
    }
    // 置き場所は回転と平行移動だけ。倍率も鏡像も無いので、寸法は変わらない。
    const double cosine = std::cos(placement.rotationRad);
    const double sine = std::sin(placement.rotationRad);
    const auto place = [&](const Point2& point) {
        return Vector3{point.u * cosine - point.v * sine + placement.translationMm.u,
            point.u * sine + point.v * cosine + placement.translationMm.v, 0.0};
    };
    std::vector<exporters::PatternCurve> curves;
    const auto addLoop = [&](const std::vector<Point2>& loop, exporters::PatternLine layer) {
        for (std::size_t index = 0; index + 1 < loop.size(); ++index) {
            const auto line = CurveSegment::MakeLine(place(loop[index]),
                place(loop[index + 1]));
            if (!line.HasValue()) {
                continue; // 長さ0の辺は捨てる。切る線にならない。
            }
            curves.push_back(exporters::PatternCurve{layer, line.Value(), false,
                panel.panelId});
        }
        // 閉じる。閉じないと切り抜けない。
        if (loop.size() >= 3) {
            const auto line = CurveSegment::MakeLine(place(loop.back()), place(loop.front()));
            if (line.HasValue()) {
                curves.push_back(exporters::PatternCurve{layer, line.Value(), false,
                    panel.panelId});
            }
        }
    };
    addLoop(panel.outline, exporters::PatternLine::Outline);
    for (const auto& opening : panel.openings) {
        addLoop(opening, exporters::PatternLine::Opening);
    }
    if (curves.empty()) {
        return Out::Failure(MakeError("FAB-P003", "型紙にする外周がありません。",
            "部材 " + panel.panelId + " から切る線を作れませんでした。"));
    }
    return Out::Success(std::move(curves));
}

} // namespace kachakacha::v2::fabrication
