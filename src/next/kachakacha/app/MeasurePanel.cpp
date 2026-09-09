#include "kachakacha/app/MeasurePanel.h"

#include "kachakacha/geometry/Measurement.h"

#include <cmath>
#include <cstdio>

namespace kachakacha::v2::app {
namespace {

//! 桁を揃えて出す。揃えないと、変わった桁に気づけない。
[[nodiscard]] std::string Fixed(double value, int digits)
{
    char buffer[64];
    // -0.000 と出ると読む人が戸惑うので、0 に寄せる。
    if (value > -5e-7 && value < 5e-7) {
        value = 0.0;
    }
    std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    return std::string(buffer);
}

[[nodiscard]] std::string KindNameJa(geometry::CurveKind kind)
{
    switch (kind) {
    case geometry::CurveKind::Line:         return "直線";
    case geometry::CurveKind::CircularArc:  return "円弧";
    case geometry::CurveKind::Circle:       return "円";
    case geometry::CurveKind::CubicBezier:  return "ベジエ";
    case geometry::CurveKind::CubicBSpline: return "スプライン";
    }
    return "線";
}

} // namespace

std::string FormatMillimetersJa(double value)
{
    return Fixed(value, 3) + " mm";
}

std::string FormatDegreesJa(double radians)
{
    return Fixed(radians * 180.0 / geometry::kPi, 3) + " 度";
}

std::string FormatPointJa(const geometry::Vector3& point)
{
    return "(" + Fixed(point.x, 3) + ", " + Fixed(point.y, 3) + ", " + Fixed(point.z, 3)
        + ")";
}

std::string MeasureSummaryJa(const MeasureRequest& request)
{
    if (request.curves.empty()) {
        return "測るものが選ばれていません。";
    }
    if (request.curves.size() == 1) {
        return "線を1本測っています。";
    }
    if (request.curves.size() == 2) {
        return "線を2本測っています。間の距離と角度も出します。";
    }
    return "線を" + std::to_string(request.curves.size()) + "本測っています。";
}

std::vector<MeasureRow> BuildMeasureRows(const MeasureRequest& request)
{
    std::vector<MeasureRow> rows;
    if (request.curves.empty()) {
        // 空の表は、壊れているのか選び忘れなのかが分からない。何をすればよいかを言う。
        rows.push_back(MeasureRow{"測るもの", "道具箱の「選択」で線を選んでください。"});
        return rows;
    }

    double totalLength = 0.0;
    for (std::size_t index = 0; index < request.curves.size(); ++index) {
        const auto& curve = request.curves[index];
        const std::string prefix = request.curves.size() == 1
            ? std::string()
            : std::to_string(index + 1) + ". ";
        const double length = geometry::MeasureCurveLength(curve, request.toleranceMm);
        totalLength += length;
        rows.push_back(MeasureRow{prefix + "種類", KindNameJa(curve.Kind())});
        rows.push_back(MeasureRow{prefix + "長さ", FormatMillimetersJa(length)});
        const auto radius = geometry::MeasureCurveRadius(curve);
        if (radius.has_value()) {
            rows.push_back(MeasureRow{prefix + "半径", FormatMillimetersJa(*radius)});
            rows.push_back(MeasureRow{prefix + "直径", FormatMillimetersJa(*radius * 2.0)});
        }
        rows.push_back(MeasureRow{prefix + "始点", FormatPointJa(curve.StartPoint())});
        rows.push_back(MeasureRow{prefix + "終点", FormatPointJa(curve.EndPoint())});
        // 両端の距離。長さと違うのは、曲がっているぶんである。
        const auto span = geometry::MeasureTwoPoints(curve.StartPoint(), curve.EndPoint());
        if (span.HasValue()) {
            rows.push_back(MeasureRow{prefix + "両端の距離",
                FormatMillimetersJa(span.Value().distanceMm)});
            rows.push_back(MeasureRow{prefix + "dX", FormatMillimetersJa(span.Value().deltaXMm)});
            rows.push_back(MeasureRow{prefix + "dY", FormatMillimetersJa(span.Value().deltaYMm)});
            rows.push_back(MeasureRow{prefix + "dZ", FormatMillimetersJa(span.Value().deltaZMm)});
        }
    }

    if (request.curves.size() > 1) {
        rows.push_back(MeasureRow{"長さの合計", FormatMillimetersJa(totalLength)});
    }
    if (request.curves.size() == 2) {
        const auto closest = geometry::MeasureCurveToCurve(request.curves[0],
            request.curves[1]);
        rows.push_back(MeasureRow{"いちばん近いところ",
            FormatMillimetersJa(closest.distanceMm)});
        // 触れているなら、そう言う。0.000 mm とだけ出しても分かりにくい。
        if (closest.distanceMm <= request.toleranceMm) {
            rows.push_back(MeasureRow{"触れているか", "触れています(許容差の内)"});
        }
        const auto angle = geometry::MeasureTangentAngle(request.curves[0], 0.5,
            request.curves[1], 0.5);
        if (angle.HasValue()) {
            rows.push_back(MeasureRow{"中ほどの接線の角度",
                FormatDegreesJa(angle.Value().acute.Value())});
        }
    }
    return rows;
}

} // namespace kachakacha::v2::app
