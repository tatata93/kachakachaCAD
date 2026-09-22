#include "kachakacha/app/MeasurePanel.h"

#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/Measurement.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/modeling/ExtrudeInput.h"

#include <algorithm>
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

std::string_view MeasureModeNameJa(MeasureMode mode) noexcept
{
    switch (mode) {
    case MeasureMode::Selection:       return "選んだものから";
    case MeasureMode::TwoPoints:       return "2点間";
    case MeasureMode::ThreePointAngle: return "3点角度";
    case MeasureMode::Element:         return "要素(接線・法線)";
    case MeasureMode::Area:            return "面積(閉じた線)";
    }
    return "";
}

const std::vector<MeasureMode>& MeasureModes()
{
    static const std::vector<MeasureMode> modes{MeasureMode::Selection, MeasureMode::TwoPoints,
        MeasureMode::ThreePointAngle, MeasureMode::Element, MeasureMode::Area};
    return modes;
}

int MeasurePointCount(MeasureMode mode) noexcept
{
    switch (mode) {
    case MeasureMode::TwoPoints:       return 2;
    case MeasureMode::ThreePointAngle: return 3;
    case MeasureMode::Element:         return 1;
    case MeasureMode::Selection:       return 0;
    case MeasureMode::Area:            return 0;
    }
    return 0;
}

namespace {

[[nodiscard]] std::string PickHint(const MeasureRequest& request, int needed)
{
    return "点をあと " + std::to_string(needed - static_cast<int>(request.pickedPoints.size()))
        + " つ押してください(いま " + std::to_string(request.pickedPoints.size()) + " つ)。"
          "右クリックで消します。";
}

//! 2点間。距離と成分、座標面への投影、軸との角度。
[[nodiscard]] std::vector<MeasureRow> TwoPointRows(const MeasureRequest& request)
{
    std::vector<MeasureRow> rows;
    if (request.pickedPoints.size() < 2) {
        rows.push_back(MeasureRow{"2点間", PickHint(request, 2)});
        return rows;
    }
    const auto measured = geometry::MeasureTwoPoints(request.pickedPoints[0],
        request.pickedPoints[1]);
    if (!measured.HasValue()) {
        rows.push_back(MeasureRow{"2点間", measured.FirstSummaryJa()});
        return rows;
    }
    const auto& m = measured.Value();
    rows.push_back(MeasureRow{"1点目", FormatPointJa(m.firstPoint)});
    rows.push_back(MeasureRow{"2点目", FormatPointJa(m.secondPoint)});
    rows.push_back(MeasureRow{"距離", FormatMillimetersJa(m.distanceMm)});
    rows.push_back(MeasureRow{"dX", FormatMillimetersJa(m.deltaXMm)});
    rows.push_back(MeasureRow{"dY", FormatMillimetersJa(m.deltaYMm)});
    rows.push_back(MeasureRow{"dZ", FormatMillimetersJa(m.deltaZMm)});
    rows.push_back(MeasureRow{"XY面への投影", FormatMillimetersJa(m.projectedOnXYMm)});
    rows.push_back(MeasureRow{"YZ面への投影", FormatMillimetersJa(m.projectedOnYZMm)});
    rows.push_back(MeasureRow{"ZX面への投影", FormatMillimetersJa(m.projectedOnZXMm)});
    const geometry::Vector3 delta = m.secondPoint - m.firstPoint;
    const char* axes[3] = {"X軸との角度", "Y軸との角度", "Z軸との角度"};
    const geometry::Vector3 units[3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    for (int axis = 0; axis < 3; ++axis) {
        const auto angle = geometry::MeasureDirections(delta, units[axis]);
        if (angle.HasValue()) {
            rows.push_back(MeasureRow{axes[axis], FormatDegreesJa(angle.Value().acute.Value())});
        }
    }
    return rows;
}

//! 3点角度。2点目が頂点。
[[nodiscard]] std::vector<MeasureRow> ThreePointRows(const MeasureRequest& request)
{
    std::vector<MeasureRow> rows;
    if (request.pickedPoints.size() < 3) {
        rows.push_back(MeasureRow{"3点角度", PickHint(request, 3) + " 2点目が頂点です。"});
        return rows;
    }
    const auto measured = geometry::MeasureThreePointAngle(request.pickedPoints[1],
        request.pickedPoints[0], request.pickedPoints[2]);
    if (!measured.HasValue()) {
        rows.push_back(MeasureRow{"3点角度", measured.FirstSummaryJa()});
        return rows;
    }
    rows.push_back(MeasureRow{"頂点", FormatPointJa(request.pickedPoints[1])});
    rows.push_back(MeasureRow{"角度", FormatDegreesJa(measured.Value().directed.Value())});
    rows.push_back(MeasureRow{"鋭角にすると", FormatDegreesJa(measured.Value().acute.Value())});
    const auto a = geometry::MeasureTwoPoints(request.pickedPoints[1], request.pickedPoints[0]);
    const auto b = geometry::MeasureTwoPoints(request.pickedPoints[1], request.pickedPoints[2]);
    if (a.HasValue() && b.HasValue()) {
        rows.push_back(MeasureRow{"頂点から1点目", FormatMillimetersJa(a.Value().distanceMm)});
        rows.push_back(MeasureRow{"頂点から3点目", FormatMillimetersJa(b.Value().distanceMm)});
    }
    return rows;
}

//! 要素。線1本 + 点なら、その点にいちばん近い所の接線・法線・曲率半径。
//! 線2本なら、いちばん近い所での接線どうし・法線どうしの角度。
[[nodiscard]] std::vector<MeasureRow> ElementRows(const MeasureRequest& request)
{
    std::vector<MeasureRow> rows;
    if (request.curves.empty()) {
        rows.push_back(MeasureRow{"要素", "線を1本(点を押す)か2本選んでください。"});
        return rows;
    }
    if (request.curves.size() >= 2) {
        const auto& first = request.curves[0];
        const auto& second = request.curves[1];
        const auto closest = geometry::MeasureCurveToCurve(first, second);
        rows.push_back(MeasureRow{"測る位置", FormatPointJa(closest.firstPoint)});
        rows.push_back(MeasureRow{"いちばん近いところ", FormatMillimetersJa(closest.distanceMm)});
        const auto tangent = geometry::MeasureTangentAngle(first, closest.firstParameter, second,
            closest.secondParameter);
        rows.push_back(MeasureRow{"接線どうしの角度",
            tangent.HasValue() ? FormatDegreesJa(tangent.Value().acute.Value())
                               : tangent.FirstSummaryJa()});
        const auto normal = geometry::MeasureNormalAngle(first, closest.firstParameter, second,
            closest.secondParameter);
        rows.push_back(MeasureRow{"法線どうしの角度",
            normal.HasValue() ? FormatDegreesJa(normal.Value().acute.Value())
                              : normal.FirstSummaryJa()});
        return rows;
    }
    if (request.pickedPoints.empty()) {
        rows.push_back(MeasureRow{"要素", "線の上の測りたい所を押してください。"});
        return rows;
    }
    const auto& curve = request.curves.front();
    const auto foot = geometry::MeasurePointToCurve(request.pickedPoints.back(), curve);
    rows.push_back(MeasureRow{"線の上の位置", FormatPointJa(foot.secondPoint)});
    rows.push_back(MeasureRow{"押した点からの距離", FormatMillimetersJa(foot.distanceMm)});
    const geometry::Vector3 tangent = geometry::MeasureCurveTangent(curve, foot.secondParameter);
    rows.push_back(MeasureRow{"接線の向き", FormatPointJa(tangent)});
    const auto normal = geometry::MeasureCurveCurvatureNormal(curve, foot.secondParameter);
    rows.push_back(MeasureRow{"法線(曲率)の向き",
        normal.has_value() ? FormatPointJa(*normal) : std::string("直線なので決まりません")});
    const auto radius = geometry::MeasureCurveRadius(curve);
    if (radius.has_value()) {
        rows.push_back(MeasureRow{"半径", FormatMillimetersJa(*radius)});
    }
    return rows;
}

//! 面積の 1 行ぶん(mm²)。
[[nodiscard]] std::string FormatSquareMillimetersJa(double value)
{
    return Fixed(value, 3) + " mm²";
}

//! 選んだ線を、順と向きのそろった 1 つの閉じた輪にする。
[[nodiscard]] base::Result<std::vector<geometry::CurveSegment>> OrderedLoop(
    const std::vector<geometry::CurveSegment>& curves, const geometry::GeometryTolerance& tolerance)
{
    using Out = base::Result<std::vector<geometry::CurveSegment>>;
    base::DeterministicIdGenerator ids{1};
    std::vector<geometry::ChainInput> inputs;
    for (const auto& curve : curves) {
        inputs.push_back({ids.NextTyped<base::IdKind::Entity>(), ids.NextTyped<base::IdKind::Segment>(),
            curve});
    }
    const std::vector<geometry::ChainInput> original = inputs;
    const auto analyzed = geometry::AnalyzeChain(inputs, tolerance);
    if (!analyzed.HasValue()) {
        return Out::Failure(base::MakeError("UI-M002", "選んだ線が 1 つの輪になっていません。",
            analyzed.FirstSummaryJa()));
    }
    if (!analyzed.Value().order.closed) {
        return Out::Failure(base::MakeError("UI-M002", "選んだ線が閉じていません。",
            "端と端がつながった輪だけ、囲む面積を測れます。"));
    }
    std::vector<geometry::CurveSegment> ordered;
    for (const auto& item : analyzed.Value().order.segments) {
        const auto found = std::find_if(original.begin(), original.end(),
            [&](const auto& input) { return input.segmentId == item.segmentId; });
        if (found == original.end()) {
            return Out::Failure(base::MakeError("UI-M002", "選んだ線が 1 つの輪になっていません。", {}));
        }
        if (!item.reversed) {
            ordered.push_back(found->segment);
            continue;
        }
        const auto reversed = geometry::ReverseCurve(found->segment);
        if (!reversed.HasValue()) {
            return Out::Failure(reversed.Diagnostics());
        }
        ordered.push_back(reversed.Value());
    }
    return Out::Success(std::move(ordered));
}

//! 面積。選んだ線が 1 つの閉じた輪で平面に載っていれば、囲む面積と求め方。
[[nodiscard]] std::vector<MeasureRow> AreaRows(const MeasureRequest& request)
{
    std::vector<MeasureRow> rows;
    const auto measured = MeasureLoopArea(request.curves, request.toleranceMm);
    if (!measured.HasValue()) {
        const std::string details = measured.FirstDetailsJa();
        rows.push_back(MeasureRow{"面積", measured.FirstSummaryJa()
                + (details.empty() ? std::string() : "(" + details + ")")});
        return rows;
    }
    const auto& m = measured.Value();
    rows.push_back(MeasureRow{"面積", FormatSquareMillimetersJa(m.areaMm2)});
    rows.push_back(MeasureRow{"求め方", m.exact ? std::string("厳密(直線と円弧だけ)")
                                               : std::string("近似(曲線を 0.001 mm で刻んで足した)")});
    rows.push_back(MeasureRow{"周の長さ", FormatMillimetersJa(m.perimeterMm)});
    rows.push_back(MeasureRow{"平面の法線", FormatPointJa(m.normal)});
    return rows;
}

} // namespace

base::Result<LoopAreaMeasure> MeasureLoopArea(const std::vector<geometry::CurveSegment>& curves,
    double toleranceMm)
{
    using Out = base::Result<LoopAreaMeasure>;
    if (curves.empty()) {
        return Out::Failure(base::MakeError("UI-M002", "面積を測る閉じた線が選ばれていません。",
            "輪になっている線を選んでください(線は何本に分かれていてもかまいません)。"));
    }
    geometry::GeometryTolerance tolerance;
    tolerance.interactiveJoinMm = std::max(toleranceMm, 1.0e-6);
    const auto loop = OrderedLoop(curves, tolerance);
    if (!loop.HasValue()) {
        return Out::Failure(loop.Diagnostics());
    }
    constexpr double kSamplingMm = 0.001;
    const auto sampled = geometry::SampleChain(loop.Value(), kSamplingMm);
    const geometry::PlaneFit plane = geometry::FitPlane(sampled);
    if (!plane.valid || plane.maximumDeviationMm > tolerance.interactiveJoinMm) {
        return Out::Failure(base::MakeError("UI-M003", "選んだ線が平面に載っていません。",
            plane.valid ? "平面から最大 " + FormatMillimetersJa(plane.maximumDeviationMm) + " 離れています。"
                        : std::string("線が一直線に並んでいて、平面が決まりません。")));
    }
    const geometry::PlanarFrame frame = geometry::MakeFrame(plane);
    const auto area = modeling::SignedAreaOnPlane(loop.Value(), frame, kSamplingMm);
    LoopAreaMeasure result;
    result.areaMm2 = std::abs(area.signedAreaMm2);
    result.exact = area.exact;
    result.normal = frame.normal;
    for (const auto& segment : loop.Value()) {
        result.perimeterMm += geometry::MeasureCurveLength(segment, kSamplingMm);
    }
    return Out::Success(result);
}

std::optional<MeasurePrimaryValue> MeasurePrimary(const MeasureRequest& request)
{
    switch (request.mode) {
    case MeasureMode::TwoPoints: {
        if (request.pickedPoints.size() < 2) {
            return std::nullopt;
        }
        const auto m = geometry::MeasureTwoPoints(request.pickedPoints[0], request.pickedPoints[1]);
        if (!m.HasValue()) {
            return std::nullopt;
        }
        return MeasurePrimaryValue{m.Value().distanceMm, "mm", "two_points"};
    }
    case MeasureMode::ThreePointAngle: {
        if (request.pickedPoints.size() < 3) {
            return std::nullopt;
        }
        const auto m = geometry::MeasureThreePointAngle(request.pickedPoints[1],
            request.pickedPoints[0], request.pickedPoints[2]);
        if (!m.HasValue()) {
            return std::nullopt;
        }
        return MeasurePrimaryValue{m.Value().directed.Value(), "rad", "three_point_angle"};
    }
    case MeasureMode::Element: {
        if (request.curves.size() >= 2) {
            const auto closest = geometry::MeasureCurveToCurve(request.curves[0], request.curves[1]);
            const auto t = geometry::MeasureTangentAngle(request.curves[0], closest.firstParameter,
                request.curves[1], closest.secondParameter);
            if (!t.HasValue()) {
                return std::nullopt;
            }
            return MeasurePrimaryValue{t.Value().acute.Value(), "rad", "element"};
        }
        if (request.curves.size() == 1) {
            const auto radius = geometry::MeasureCurveRadius(request.curves.front());
            if (radius.has_value()) {
                return MeasurePrimaryValue{*radius, "mm", "element"};
            }
        }
        return std::nullopt;
    }
    case MeasureMode::Area:
        // 面積は寸法(長さ・角度)として残さない(MeasureDimensionOf が理由を言う)。
        return std::nullopt;
    case MeasureMode::Selection:
        break;
    }
    if (request.curves.empty()) {
        return std::nullopt;
    }
    double total = 0.0;
    for (const auto& curve : request.curves) {
        total += geometry::MeasureCurveLength(curve, request.toleranceMm);
    }
    return MeasurePrimaryValue{total, "mm", "selection"};
}

base::Result<document::ReferenceDimension> MeasureDimensionOf(const MeasureRequest& request,
    std::string_view label, base::DimensionId id)
{
    using Out = base::Result<document::ReferenceDimension>;
    if (request.mode == MeasureMode::Area) {
        return Out::Failure(base::MakeError("UI-M004", "面積は寸法として残せません。",
            "残せる寸法は長さと角度だけです。面積は表の値を控えてください。"));
    }
    const auto primary = MeasurePrimary(request);
    if (!primary.has_value()) {
        return Out::Failure(base::MakeError("UI-M001", "残せる寸法がまだありません。",
            "測り終えてから「寸法を残す」を押してください。"));
    }
    if (request.targetIds.empty()) {
        return Out::Failure(base::MakeError("UI-M001", "残せる寸法がまだありません。",
            "測った相手が文書の線ではありません。線の上の点を押すか、線を選んでください。"));
    }
    document::ReferenceDimension dimension;
    dimension.id = id;
    dimension.label = label.empty() ? std::string(MeasureModeNameJa(request.mode))
                                    : std::string(label);
    dimension.kind = primary->kind;
    dimension.targets = request.targetIds;
    dimension.recordedValue = primary->value;
    dimension.unit = primary->unit;
    dimension.noteJa = MeasureSummaryJa(request);
    // 3D に描く位置。押した点で測ったならその点、線を 1 本選んで測ったならその両端。
    if (request.pickedPoints.size() >= 2) {
        dimension.anchors = request.pickedPoints;
    } else if (request.curves.size() == 1) {
        dimension.anchors = {request.curves.front().StartPoint(), request.curves.front().EndPoint()};
    }
    return Out::Success(std::move(dimension));
}

std::string MeasureSummaryJa(const MeasureRequest& request)
{
    if (request.mode != MeasureMode::Selection) {
        return std::string(MeasureModeNameJa(request.mode)) + "で測っています。";
    }
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
    // V1 の3モード。押した点で測る。
    if (request.mode == MeasureMode::TwoPoints) {
        return TwoPointRows(request);
    }
    if (request.mode == MeasureMode::ThreePointAngle) {
        return ThreePointRows(request);
    }
    if (request.mode == MeasureMode::Element) {
        return ElementRows(request);
    }
    if (request.mode == MeasureMode::Area) {
        return AreaRows(request);
    }
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
