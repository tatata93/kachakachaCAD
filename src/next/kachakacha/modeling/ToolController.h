#pragma once

//! 作図ツールの状態機械(v1-drawing-parity.md §1、implementation-work-packages §3.1)。
//!
//! V1の23種のツールを、巨大な enum 比較の分岐ではなく、
//! 「いま何点目を待っているか」という共通の状態で表す。
//! こうすると、取り消し(1点戻る)、中断、途中プレビューが全ツールで同じ形になる。
//! V1はツールごとに別々の分岐を持っていたので、
//! 「このツールだけ Esc が効かない」「このツールだけ右クリックで確定できない」が起きた。
//!
//! Qt に依存しない。入力は「点が置かれた」「取り消した」「やめた」の3つだけ。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/ArcBuilders.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"

#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::modeling {

using geometry::CurveSegment;
using geometry::GeometryTolerance;
using geometry::Vector3;

//! V1の `ViewportTool` と1対1。
enum class DrawingTool {
    Select,
    SetGridOrigin,
    Point,
    Line,
    Polyline,
    Rectangle,
    Circle,
    Arc,
    Bezier,
    Spline,
    Move,
    Copy,
    Mirror,
    Rotate,
    Split,
    Trim,
    Extend,
    JoinEndpoints,
    TangentJoin,
    CurvatureJoin,
    Measure,
    ConnectTwoPoints,
    ChamferOrFilletPair,
};

[[nodiscard]] std::string_view DrawingToolNameJa(DrawingTool tool) noexcept;

//! 円弧の作り方(V1の `ArcDrawingMode` と同じ3種)。
enum class ArcMode {
    ThreePoints,
    EndpointsAndRadius,
    StartTangent,
};

struct ToolSettings {
    ArcMode arcMode = ArcMode::ThreePoints;
    //! 円弧の半径や掃引角など、数値欄で決める値。
    double radiusMm = 10.0;
    double sweepAngleRad = 1.5707963267948966;
    //! 補助線として描くか。
    bool construction = false;
};

//! いま何を待っているか。UIはこれを見て案内文を出す。
struct ToolPrompt {
    //! あと何点必要か。0 なら次の点で確定する。
    int remainingPoints = 0;
    //! 画面に出す案内。
    std::string messageJa;
    //! 点をいくつでも受け付けるツール(ポリラインなど)。
    bool acceptsMorePoints = false;
    //! いま確定できるか。
    bool canFinish = false;
};

//! ツールが1回で作るもの。
struct ToolOutput {
    std::vector<CurveSegment> segments;
    //! 作図点だけを作るツール。
    std::vector<Vector3> points;
    //! 移動・複製・鏡映・回転が集めた基準の点。形は作らないが、これが要る。
    //! ここに残さないと、確定した瞬間に points_ が空になって、何を指したか消える。
    std::vector<Vector3> transformPoints;
    //! そのツールが補助線を作ったか。
    bool construction = false;
};

//! 作図ツール1つぶんの進行。
class ToolSession {
public:
    ToolSession(DrawingTool tool, ToolSettings settings, GeometryTolerance tolerance);

    [[nodiscard]] DrawingTool Tool() const noexcept { return tool_; }
    [[nodiscard]] const std::vector<Vector3>& Points() const noexcept { return points_; }
    [[nodiscard]] ToolPrompt Prompt() const;

    //! 点を置く。確定に達したら出力を返す。まだなら値を持たない Result を返す。
    [[nodiscard]] base::Result<std::optional<ToolOutput>> AddPoint(const Vector3& point);

    //! 1点戻す。戻せる点が無ければ false。
    bool UndoLastPoint();

    //! いまの入力で確定する(ポリラインなどで使う)。
    [[nodiscard]] base::Result<ToolOutput> Finish();

    //! やめる。置いた点をすべて捨てる。
    void Cancel();

    //! 途中経過。まだ確定していない形を画面へ出すため。
    //! ポインタの現在位置を渡すと、そこまで引いた形を返す。
    [[nodiscard]] std::vector<CurveSegment> Preview(const Vector3& pointer) const;

private:
    [[nodiscard]] int RequiredPointCount() const;
    [[nodiscard]] base::Result<ToolOutput> Build(const std::vector<Vector3>& points) const;

    DrawingTool tool_ = DrawingTool::Line;
    ToolSettings settings_;
    GeometryTolerance tolerance_;
    std::vector<Vector3> points_;
};

} // namespace kachakacha::v2::modeling
