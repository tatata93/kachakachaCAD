#include "kachakacha/app/EntityEdit.h"

#include "kachakacha/geometry/Units.h"

#include <cmath>
#include <string>

namespace kachakacha::v2::app {

using base::MakeError;
using base::Result;
using domain::CreateWireDefinition;
using domain::CreateWorkPlaneDefinition;
using geometry::CurveKind;
using geometry::CurveSegment;
using geometry::kPi;
using geometry::Vector3;
using modeling::WorkPlaneFrame;

namespace {

constexpr const char* kNothingSelected = "UI-E001";
constexpr const char* kOriginPlane = "UI-E002";
constexpr const char* kMixedKinds = "UI-E003";
constexpr const char* kNoDirection = "UI-E004";

[[nodiscard]] bool AllLines(const std::vector<CurveSegment>& segments)
{
    for (const auto& segment : segments) {
        if (segment.Kind() != CurveKind::Line) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Result<WireEditFields> FieldsOfSingle(const CurveSegment& segment)
{
    WireEditFields fields;
    switch (segment.Kind()) {
    case CurveKind::Line:
        fields.shape = WireEditShape::Line;
        fields.points = {segment.StartPoint(), segment.EndPoint()};
        return Result<WireEditFields>::Success(std::move(fields));
    case CurveKind::CubicBezier:
        fields.shape = WireEditShape::CubicBezier;
        fields.points = segment.ControlPoints();
        return Result<WireEditFields>::Success(std::move(fields));
    case CurveKind::CubicBSpline:
        fields.shape = WireEditShape::CubicBSpline;
        fields.points = segment.ControlPoints();
        return Result<WireEditFields>::Success(std::move(fields));
    case CurveKind::Circle:
    case CurveKind::CircularArc:
        fields.shape = segment.Kind() == CurveKind::Circle ? WireEditShape::Circle
                                                             : WireEditShape::CircularArc;
        fields.center = segment.Center();
        fields.uAxis = segment.ReferenceDirection();
        fields.vAxis = geometry::Cross(segment.Normal(), segment.ReferenceDirection());
        fields.radiusMm = segment.Radius();
        fields.startAngleDeg = segment.StartAngleRad() * 180.0 / kPi;
        fields.sweepAngleDeg = segment.SweepAngleRad() * 180.0 / kPi;
        return Result<WireEditFields>::Success(std::move(fields));
    }
    return Result<WireEditFields>::Failure(MakeError(kMixedKinds,
        "この線は数値の欄に出せません。", std::string(geometry::CurveKindName(segment.Kind()))));
}

[[nodiscard]] Result<std::vector<CurveSegment>> LinesThrough(
    const std::vector<Vector3>& points)
{
    using Out = Result<std::vector<CurveSegment>>;
    if (points.size() < 2) {
        return Out::Failure(MakeError(kNoDirection, "点が足りません。",
            "直線には 2 点、折れ線には 2 点以上が要ります。"));
    }
    std::vector<CurveSegment> segments;
    for (std::size_t index = 0; index + 1 < points.size(); ++index) {
        auto line = CurveSegment::MakeLine(points[index], points[index + 1]);
        if (!line.HasValue()) {
            return Out::Failure(line.Diagnostics());
        }
        segments.push_back(line.Value());
    }
    return Out::Success(std::move(segments));
}

//! 直線の終点を「長さ」「平面内角度」で置き直す。どちらも無ければそのまま。
[[nodiscard]] Result<Vector3> RelocatedLineEnd(const WireEditFields& fields,
    const WorkPlaneFrame& frame)
{
    const Vector3 start = fields.points[0];
    const Vector3 end = fields.points[1];
    Vector3 direction = end - start;
    double length = direction.Length();
    if (fields.angleDeg.has_value()) {
        const double radians = *fields.angleDeg * kPi / 180.0;
        direction = frame.uAxis * std::cos(radians) + frame.vAxis * std::sin(radians);
    }
    if (fields.lengthMm.has_value()) {
        length = *fields.lengthMm;
    }
    if (!(length > 0.0) || !std::isfinite(length)) {
        return Result<Vector3>::Failure(MakeError(kNoDirection,
            "直線の長さは 0 より大きい数にしてください。", std::to_string(length) + " mm"));
    }
    const Vector3 unit = geometry::Normalized(direction);
    if (unit == Vector3{}) {
        return Result<Vector3>::Failure(MakeError(kNoDirection,
            "直線の向きが決まりません。", "始点と終点が同じ位置です。"));
    }
    return Result<Vector3>::Success(start + unit * length);
}

} // namespace

std::string_view WireEditShapeNameJa(WireEditShape shape) noexcept
{
    switch (shape) {
    case WireEditShape::Line:         return "直線";
    case WireEditShape::Polyline:     return "折れ線";
    case WireEditShape::CubicBezier:  return "ベジエ";
    case WireEditShape::CubicBSpline: return "スプライン";
    case WireEditShape::Circle:       return "円";
    case WireEditShape::CircularArc:  return "円弧";
    }
    return "線";
}

Result<WireEditFields> WireEditFieldsOf(const CreateWireDefinition& definition)
{
    using Out = Result<WireEditFields>;
    if (definition.segments.empty()) {
        return Out::Failure(MakeError(kMixedKinds, "線が1本も入っていません。", {}));
    }
    WireEditFields fields;
    if (definition.segments.size() == 1) {
        auto single = FieldsOfSingle(definition.segments.front());
        if (!single.HasValue()) {
            return single;
        }
        fields = single.Value();
    } else {
        if (!AllLines(definition.segments)) {
            return Out::Failure(MakeError(kMixedKinds,
                "種類が混ざったワイヤーは数値で編集できません。",
                "直線だけの並び(折れ線)なら点の表で直せます。"
                "円弧や曲線が混ざったものは、制御点を掴んで動かしてください。"));
        }
        fields.shape = WireEditShape::Polyline;
        fields.points.push_back(definition.segments.front().StartPoint());
        for (const auto& segment : definition.segments) {
            fields.points.push_back(segment.EndPoint());
        }
    }
    fields.construction = definition.construction;
    fields.sourcePlaneId = definition.sourcePlaneId;
    return Out::Success(std::move(fields));
}

Result<std::vector<CurveSegment>> BuildWireFromEditFields(const WireEditFields& fields,
    const WorkPlaneFrame& angleFrame)
{
    using Out = Result<std::vector<CurveSegment>>;
    switch (fields.shape) {
    case WireEditShape::Line: {
        if (fields.points.size() != 2) {
            return Out::Failure(MakeError(kNoDirection, "点が足りません。",
                "直線には始点と終点の 2 点が要ります。"));
        }
        const auto end = RelocatedLineEnd(fields, angleFrame);
        if (!end.HasValue()) {
            return Out::Failure(end.Diagnostics());
        }
        return LinesThrough({fields.points[0], end.Value()});
    }
    case WireEditShape::Polyline:
        return LinesThrough(fields.points);
    case WireEditShape::CubicBezier: {
        auto made = CurveSegment::MakeCubicBezier(fields.points);
        if (!made.HasValue()) {
            return Out::Failure(made.Diagnostics());
        }
        return Out::Success({made.Value()});
    }
    case WireEditShape::CubicBSpline: {
        auto made = CurveSegment::MakeCubicBSpline(fields.points);
        if (!made.HasValue()) {
            return Out::Failure(made.Diagnostics());
        }
        return Out::Success({made.Value()});
    }
    case WireEditShape::Circle:
    case WireEditShape::CircularArc: {
        // 円の X 軸と Y 軸から法線を出す。V1 の欄と同じ持ち方。
        const Vector3 normal = geometry::Cross(fields.uAxis, fields.vAxis);
        auto made = fields.shape == WireEditShape::Circle
            ? CurveSegment::MakeCircle(fields.center, normal, fields.uAxis, fields.radiusMm)
            : CurveSegment::MakeCircularArc(fields.center, normal, fields.uAxis,
                  fields.radiusMm, fields.startAngleDeg * kPi / 180.0,
                  fields.sweepAngleDeg * kPi / 180.0);
        if (!made.HasValue()) {
            return Out::Failure(made.Diagnostics());
        }
        return Out::Success({made.Value()});
    }
    }
    return Out::Failure(MakeError(kMixedKinds, "知らない形です。", {}));
}

Result<CreateWireDefinition> EditedWireDefinition(const CreateWireDefinition& current,
    const WireEditFields& fields, const WorkPlaneFrame& angleFrame, base::IdGenerator& ids)
{
    using Out = Result<CreateWireDefinition>;
    auto segments = BuildWireFromEditFields(fields, angleFrame);
    if (!segments.HasValue()) {
        return Out::Failure(segments.Diagnostics());
    }
    CreateWireDefinition edited = current;
    edited.segments = std::move(segments.Value());
    edited.construction = fields.construction;
    edited.sourcePlaneId = fields.sourcePlaneId;
    // 本数が同じなら線の ID を保つ。寸法や役割表がその線を指し続けられる。
    if (edited.segmentIds.size() != edited.segments.size()) {
        edited.segmentIds.clear();
        for (std::size_t index = 0; index < edited.segments.size(); ++index) {
            edited.segmentIds.push_back(ids.NextTyped<base::IdKind::Segment>());
        }
    }
    return Out::Success(std::move(edited));
}

PlaneEditFields PlaneEditFieldsOf(const CreateWorkPlaneDefinition& definition)
{
    PlaneEditFields fields;
    fields.origin = definition.origin;
    fields.normal = definition.normal;
    fields.uDirection = definition.uDirection;
    return fields;
}

Result<CreateWorkPlaneDefinition> EditedPlaneDefinition(
    const CreateWorkPlaneDefinition& current, const PlaneEditFields& fields,
    const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<CreateWorkPlaneDefinition>;
    if (current.isOriginPlane) {
        return Out::Failure(MakeError(kOriginPlane,
            "原点の基準平面は数値で変えられません。",
            "「平面から離す」などで新しい作業平面を作ってください。"));
    }
    modeling::WorkPlaneRequest request;
    request.method = modeling::WorkPlaneMethod::PointNormal;
    request.origin = fields.origin;
    request.normal = fields.normal;
    request.uHint = fields.uDirection;
    const auto frame = modeling::BuildWorkPlane(request, tolerance);
    if (!frame.HasValue()) {
        return Out::Failure(frame.Diagnostics());
    }
    CreateWorkPlaneDefinition edited = current;
    edited.method = static_cast<int>(modeling::WorkPlaneMethod::PointNormal);
    edited.inputs.clear();
    edited.origin = frame.Value().origin;
    edited.normal = frame.Value().normal;
    edited.uDirection = frame.Value().uAxis;
    edited.offset = {};
    return Out::Success(std::move(edited));
}

LineMeasure MeasureLine(const Vector3& start, const Vector3& end,
    const WorkPlaneFrame& angleFrame)
{
    LineMeasure measure;
    const Vector3 delta = end - start;
    measure.lengthMm = delta.Length();
    const double u = geometry::Dot(delta, angleFrame.uAxis);
    const double v = geometry::Dot(delta, angleFrame.vAxis);
    measure.angleDeg = (u == 0.0 && v == 0.0) ? 0.0 : std::atan2(v, u) * 180.0 / kPi;
    return measure;
}

base::Diagnostic NothingToEditDiagnostic(std::size_t selectedCount)
{
    return MakeError(kNothingSelected, "数値で編集できるものを1つ選んでください。",
        selectedCount == 0 ? std::string("何も選んでいません。")
                           : std::to_string(selectedCount) + " 個選んでいます。"
                                 "作業平面か線を 1 つだけにしてください。");
}

} // namespace kachakacha::v2::app
