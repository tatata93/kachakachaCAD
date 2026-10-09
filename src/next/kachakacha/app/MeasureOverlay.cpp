#include "kachakacha/app/MeasurePanel.h"
#include "kachakacha/geometry/Measurement.h"

namespace kachakacha::v2::app {
MeasureOverlay BuildMeasureOverlay(const MeasureRequest& request)
{
    MeasureOverlay result;
    result.rows = BuildMeasureRows(request);
    result.points = request.pickedPoints;
    if (request.mode == MeasureMode::TwoPoints || request.mode == MeasureMode::ThreePointAngle) {
        if (result.points.size() >= 2) result.lines.push_back(result.points);
        return result;
    }
    for (const auto& curve : request.curves) {
        std::vector<geometry::Vector3> line;
        const int steps = curve.Kind() == geometry::CurveKind::Line ? 1 : 64;
        for (int index = 0; index <= steps; ++index)
            line.push_back(curve.Evaluate(static_cast<double>(index) / steps));
        result.lines.push_back(std::move(line));
        if (geometry::MeasureCurveRadius(curve).has_value()) {
            result.lines.push_back({curve.Center(), curve.Evaluate(0.5)});
            result.points.push_back(curve.Center());
        }
    }
    if (request.curves.size() == 2) {
        const auto closest = geometry::MeasureCurveToCurve(request.curves[0], request.curves[1]);
        result.lines.push_back({closest.firstPoint, closest.secondPoint});
        result.points.push_back(closest.firstPoint);
        result.points.push_back(closest.secondPoint);
    } else if (request.mode == MeasureMode::Element && request.curves.size() == 1
        && !request.pickedPoints.empty()) {
        const auto closest = geometry::MeasurePointToCurve(request.pickedPoints.back(), request.curves[0]);
        result.lines.push_back({closest.firstPoint, closest.secondPoint});
        result.points.push_back(closest.secondPoint);
    }
    return result;
}
} // namespace kachakacha::v2::app
