#include "kachakacha/modeling/ToolController.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::modeling {

using base::Diagnostic;
using base::MakeError;
using base::Result;
using geometry::Normalized;

namespace {

constexpr const char* kNeedMore = "UI-T001";
constexpr const char* kBadInput = "UI-T002";
constexpr const char* kNotSupported = "UI-T003";

//! 選択や測定のように、形を作らないツール。
[[nodiscard]] bool MakesGeometry(DrawingTool tool)
{
    switch (tool) {
    case DrawingTool::Select:
    case DrawingTool::SetGridOrigin:
    case DrawingTool::Measure:
    case DrawingTool::Split:
    case DrawingTool::Trim:
    case DrawingTool::Extend:
    case DrawingTool::JoinEndpoints:
    case DrawingTool::TangentJoin:
    case DrawingTool::CurvatureJoin:
    case DrawingTool::ChamferOrFilletPair:
        return false;
    default:
        return true;
    }
}

} // namespace

std::string_view DrawingToolNameJa(DrawingTool tool) noexcept
{
    switch (tool) {
    case DrawingTool::Select:              return "選択";
    case DrawingTool::SetGridOrigin:       return "画面で基準を合わせる";
    case DrawingTool::Point:               return "作図点";
    case DrawingTool::Line:                return "直線";
    case DrawingTool::Polyline:            return "ポリライン";
    case DrawingTool::Rectangle:           return "矩形";
    case DrawingTool::Circle:              return "円";
    case DrawingTool::Arc:                 return "円弧";
    case DrawingTool::Bezier:              return "ベジェ";
    case DrawingTool::Spline:              return "スプライン";
    case DrawingTool::Move:                return "移動";
    case DrawingTool::Copy:                return "コピー";
    case DrawingTool::Mirror:              return "ミラー複製";
    case DrawingTool::Rotate:              return "回転";
    case DrawingTool::Split:               return "分割";
    case DrawingTool::Trim:                return "トリム";
    case DrawingTool::Extend:              return "延長";
    case DrawingTool::JoinEndpoints:       return "端点一致";
    case DrawingTool::TangentJoin:         return "接線接続";
    case DrawingTool::CurvatureJoin:       return "曲率接続";
    case DrawingTool::Measure:             return "測定";
    case DrawingTool::ConnectTwoPoints:    return "2点を線で結ぶ";
    case DrawingTool::ChamferOrFilletPair: return "面取り/丸め";
    }
    return "";
}

ToolSession::ToolSession(DrawingTool tool, ToolSettings settings, GeometryTolerance tolerance)
    : tool_(tool), settings_(std::move(settings)), tolerance_(tolerance)
{
}

int ToolSession::RequiredPointCount() const
{
    switch (tool_) {
    case DrawingTool::Point:
    case DrawingTool::SetGridOrigin:
        return 1;
    case DrawingTool::Line:
    case DrawingTool::ConnectTwoPoints:
    case DrawingTool::Rectangle:
    case DrawingTool::Circle:
    case DrawingTool::Move:
    case DrawingTool::Copy:
        return 2;
    case DrawingTool::Arc:
        // 3点通す場合は3点、それ以外は2点。
        return settings_.arcMode == ArcMode::ThreePoints ? 3 : 2;
    case DrawingTool::Mirror:
        return 2;   // 鏡の線を2点で
    case DrawingTool::Rotate:
        return 3;   // 中心、始まりの向き、終わりの向き
    case DrawingTool::Bezier:
        return 4;
    case DrawingTool::Polyline:
    case DrawingTool::Spline:
        return -1;  // いくつでも
    default:
        return 0;   // 形を作らないツール
    }
}

ToolPrompt ToolSession::Prompt() const
{
    ToolPrompt prompt;
    const int required = RequiredPointCount();
    const int placed = static_cast<int>(points_.size());
    if (required < 0) {
        prompt.acceptsMorePoints = true;
        const int minimum = tool_ == DrawingTool::Spline ? 4 : 2;
        prompt.canFinish = placed >= minimum;
        prompt.remainingPoints = std::max(0, minimum - placed);
        prompt.messageJa = prompt.canFinish
            ? std::string(DrawingToolNameJa(tool_))
                + ": 次の点を置くか、右クリックで確定します。"
            : std::string(DrawingToolNameJa(tool_)) + ": あと "
                + std::to_string(prompt.remainingPoints) + " 点。";
        return prompt;
    }
    if (required == 0) {
        prompt.messageJa = std::string(DrawingToolNameJa(tool_))
            + ": 対象を選んでください。";
        return prompt;
    }
    prompt.remainingPoints = std::max(0, required - placed);
    prompt.canFinish = prompt.remainingPoints == 0;
    if (prompt.remainingPoints == 0) {
        prompt.messageJa = std::string(DrawingToolNameJa(tool_)) + ": 確定できます。";
    } else {
        prompt.messageJa = std::string(DrawingToolNameJa(tool_)) + ": あと "
            + std::to_string(prompt.remainingPoints) + " 点。";
    }
    return prompt;
}

Result<ToolOutput> ToolSession::Build(const std::vector<Vector3>& points) const
{
    ToolOutput output;
    output.construction = settings_.construction;
    const auto fail = [&](const char* code, const std::string& summary,
                          const std::string& detail) {
        return Result<ToolOutput>::Failure(MakeError(code, summary, detail));
    };

    switch (tool_) {
    case DrawingTool::Point:
    case DrawingTool::SetGridOrigin:
        if (points.size() != 1) {
            return fail(kNeedMore, "点が足りません。", {});
        }
        output.points.push_back(points.front());
        return Result<ToolOutput>::Success(std::move(output));

    case DrawingTool::Line:
    case DrawingTool::ConnectTwoPoints: {
        if (points.size() != 2) {
            return fail(kNeedMore, "点が足りません。", {});
        }
        auto made = CurveSegment::MakeLine(points[0], points[1]);
        if (!made.HasValue()) {
            return Result<ToolOutput>::Failure(made.Diagnostics());
        }
        output.segments.push_back(made.Value());
        return Result<ToolOutput>::Success(std::move(output));
    }

    case DrawingTool::Polyline: {
        if (points.size() < 2) {
            return fail(kNeedMore, "点が足りません。", "2点以上必要です。");
        }
        for (std::size_t index = 1; index < points.size(); ++index) {
            auto made = CurveSegment::MakeLine(points[index - 1], points[index]);
            if (!made.HasValue()) {
                return Result<ToolOutput>::Failure(made.Diagnostics());
            }
            output.segments.push_back(made.Value());
        }
        return Result<ToolOutput>::Success(std::move(output));
    }

    case DrawingTool::Rectangle: {
        if (points.size() != 2) {
            return fail(kNeedMore, "点が足りません。", {});
        }
        // 2点は対角。作業平面の向きは、いまは XY として扱う。
        const Vector3& a = points[0];
        const Vector3& b = points[1];
        if (std::abs(a.x - b.x) <= tolerance_.modelLinearMm
            || std::abs(a.y - b.y) <= tolerance_.modelLinearMm) {
            return fail(kBadInput, "つぶれた矩形は作れません。",
                "対角の2点が同じ行か列に乗っています。");
        }
        const Vector3 corners[4]{{a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, b.y, a.z},
            {a.x, b.y, a.z}};
        for (int index = 0; index < 4; ++index) {
            auto made = CurveSegment::MakeLine(corners[index], corners[(index + 1) % 4]);
            if (!made.HasValue()) {
                return Result<ToolOutput>::Failure(made.Diagnostics());
            }
            output.segments.push_back(made.Value());
        }
        return Result<ToolOutput>::Success(std::move(output));
    }

    case DrawingTool::Circle: {
        if (points.size() != 2) {
            return fail(kNeedMore, "点が足りません。", {});
        }
        const double radius = (points[1] - points[0]).Length();
        auto made = CurveSegment::MakeCircle(points[0], {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0},
            radius);
        if (!made.HasValue()) {
            return Result<ToolOutput>::Failure(made.Diagnostics());
        }
        output.segments.push_back(made.Value());
        return Result<ToolOutput>::Success(std::move(output));
    }

    case DrawingTool::Arc: {
        if (settings_.arcMode == ArcMode::ThreePoints) {
            if (points.size() != 3) {
                return fail(kNeedMore, "点が足りません。", {});
            }
            auto made = geometry::ArcThroughThreePoints(points[0], points[1], points[2]);
            if (!made.HasValue()) {
                return Result<ToolOutput>::Failure(made.Diagnostics());
            }
            output.segments.push_back(made.Value());
            return Result<ToolOutput>::Success(std::move(output));
        }
        if (points.size() != 2) {
            return fail(kNeedMore, "点が足りません。", {});
        }
        if (settings_.arcMode == ArcMode::EndpointsAndRadius) {
            auto made = geometry::ArcFromEndpointsAndRadius(points[0], points[1],
                settings_.radiusMm, {0.0, 0.0, 1.0}, false, false);
            if (!made.HasValue()) {
                return Result<ToolOutput>::Failure(made.Diagnostics());
            }
            output.segments.push_back(made.Value());
            return Result<ToolOutput>::Success(std::move(output));
        }
        // 始点と、接線の向きを決める2点目。
        const Vector3 tangent = Normalized(points[1] - points[0]);
        if (!(tangent.Length() > 0.0)) {
            return fail(kBadInput, "接線の向きが決まりません。", "2点が同じ位置です。");
        }
        auto made = geometry::ArcFromStartTangentRadiusSweep(points[0], tangent,
            {0.0, 0.0, 1.0}, settings_.radiusMm, settings_.sweepAngleRad);
        if (!made.HasValue()) {
            return Result<ToolOutput>::Failure(made.Diagnostics());
        }
        output.segments.push_back(made.Value());
        return Result<ToolOutput>::Success(std::move(output));
    }

    case DrawingTool::Bezier: {
        if (points.size() != 4) {
            return fail(kNeedMore, "点が足りません。", "制御点は4つです。");
        }
        auto made = CurveSegment::MakeCubicBezier(points);
        if (!made.HasValue()) {
            return Result<ToolOutput>::Failure(made.Diagnostics());
        }
        output.segments.push_back(made.Value());
        return Result<ToolOutput>::Success(std::move(output));
    }

    case DrawingTool::Spline: {
        if (points.size() < 4) {
            return fail(kNeedMore, "点が足りません。", "制御点は4つ以上です。");
        }
        auto made = CurveSegment::MakeCubicBSpline(points);
        if (!made.HasValue()) {
            return Result<ToolOutput>::Failure(made.Diagnostics());
        }
        output.segments.push_back(made.Value());
        return Result<ToolOutput>::Success(std::move(output));
    }

    case DrawingTool::Move:
    case DrawingTool::Copy:
    case DrawingTool::Mirror:
    case DrawingTool::Rotate:
        // 変換そのものは WireEdit が行う。ここでは基準の点を集めて渡す。
        // 何を意味するか(移動量・鏡の面・回す角)は TransformInput が決める。
        if (static_cast<int>(points.size()) != RequiredPointCount()) {
            return fail(kNeedMore, "点が足りません。", {});
        }
        output.transformPoints = points;
        return Result<ToolOutput>::Success(std::move(output));

    default:
        return fail(kNotSupported, "このツールは点から形を作りません。",
            std::string(DrawingToolNameJa(tool_)) + " は対象を選んで使います。");
    }
}

Result<std::optional<ToolOutput>> ToolSession::AddPoint(const Vector3& point)
{
    if (!point.IsFinite()) {
        return Result<std::optional<ToolOutput>>::Failure(MakeError(kBadInput,
            "座標が数になっていません。", {}));
    }
    if (!MakesGeometry(tool_)) {
        return Result<std::optional<ToolOutput>>::Failure(MakeError(kNotSupported,
            "このツールは点を置いて使うものではありません。",
            std::string(DrawingToolNameJa(tool_))));
    }
    points_.push_back(point);
    const int required = RequiredPointCount();
    if (required < 0) {
        // いくつでも受け付けるツール。確定は Finish で。
        return Result<std::optional<ToolOutput>>::Success(std::nullopt);
    }
    if (static_cast<int>(points_.size()) < required) {
        return Result<std::optional<ToolOutput>>::Success(std::nullopt);
    }
    auto built = Build(points_);
    if (!built.HasValue()) {
        // 作れなかった。最後の点を戻して、やり直せるようにする。
        points_.pop_back();
        return Result<std::optional<ToolOutput>>::Failure(built.Diagnostics());
    }
    points_.clear();
    return Result<std::optional<ToolOutput>>::Success(built.Value());
}

bool ToolSession::UndoLastPoint()
{
    if (points_.empty()) {
        return false;
    }
    points_.pop_back();
    return true;
}

Result<ToolOutput> ToolSession::Finish()
{
    if (RequiredPointCount() >= 0) {
        return Result<ToolOutput>::Failure(MakeError(kNotSupported,
            "このツールは点の数が決まっています。",
            std::string(DrawingToolNameJa(tool_)) + " は途中で確定できません。"));
    }
    auto built = Build(points_);
    if (built.HasValue()) {
        points_.clear();
    }
    return built;
}

void ToolSession::Cancel()
{
    points_.clear();
}

std::vector<CurveSegment> ToolSession::Preview(const Vector3& pointer) const
{
    if (points_.empty() || !pointer.IsFinite()) {
        return {};
    }
    std::vector<Vector3> candidate = points_;
    candidate.push_back(pointer);
    const int required = RequiredPointCount();
    if (required >= 0 && static_cast<int>(candidate.size()) > required) {
        return {};
    }
    if (required >= 0 && static_cast<int>(candidate.size()) < required) {
        // まだ足りない。分かるところまでを直線で見せる。
        std::vector<CurveSegment> preview;
        for (std::size_t index = 1; index < candidate.size(); ++index) {
            auto made = CurveSegment::MakeLine(candidate[index - 1], candidate[index]);
            if (made.HasValue()) {
                preview.push_back(made.Value());
            }
        }
        return preview;
    }
    const auto built = Build(candidate);
    if (built.HasValue()) {
        return built.Value().segments;
    }
    // 作れない途中経過は、線で繋いだだけのものを見せる。
    std::vector<CurveSegment> preview;
    for (std::size_t index = 1; index < candidate.size(); ++index) {
        auto made = CurveSegment::MakeLine(candidate[index - 1], candidate[index]);
        if (made.HasValue()) {
            preview.push_back(made.Value());
        }
    }
    return preview;
}

} // namespace kachakacha::v2::modeling
