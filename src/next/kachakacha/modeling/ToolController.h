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
    //! 拡大縮小(D-22)。末尾に足す(道具の並びは AllTools と帯が決める)。
    Scale,
};

[[nodiscard]] std::string_view DrawingToolNameJa(DrawingTool tool) noexcept;

//! 点をいくつでも受ける道具(折れ線・スプライン)。締めるのは Enter / 右クリック。
[[nodiscard]] bool TakesAnyNumberOfPoints(DrawingTool tool) noexcept;

//! 円弧の作り方(V1の `ArcDrawingMode` と同じ3種 + 中心・始点・終点)。
//! 末尾に足す(並びの番号を変えない)。
enum class ArcMode {
    ThreePoints,
    EndpointsAndRadius,
    StartTangent,
    //! 中心 → 始点(半径が決まる)→ 終点(向きだけ)。左回り(D-08)。
    CenterStartEnd,
};

//! 円の作り方。直径指定は中心＋半径のまま、カーソル横の欄で直径を打つ。
enum class CircleMode {
    CenterRadius,
    //! 3点を通る円(D-04)。
    ThreePoints,
};

//! スケールの作り方(D-22)。
enum class ScaleMode {
    //! 中心を 1 点押し、倍率は棚の欄で決める。
    Factor,
    //! 中心・基準の点・行き先の点の 3 点。倍率 = 中心から行き先 / 中心から基準。
    Reference,
};

//! スプラインの作り方。
enum class SplineMode {
    //! 押した点が制御点(4 点以上)。
    ControlPoints,
    //! 押した点を必ず通る(3 点以上、D-13)。
    ThroughPoints,
};

struct ToolSettings {
    ArcMode arcMode = ArcMode::ThreePoints;
    CircleMode circleMode = CircleMode::CenterRadius;
    SplineMode splineMode = SplineMode::ControlPoints;
    ScaleMode scaleMode = ScaleMode::Factor;
    //! スケールの倍率(作り方が「倍率」のとき)。0 より大きいこと。
    double scaleFactor = 2.0;
    //! 円弧の半径や掃引角など、数値欄で決める値。
    double radiusMm = 10.0;
    double sweepAngleRad = 1.5707963267948966;
    //! 補助線として描くか。
    bool construction = false;
    //! 指定した点を作図点として残すか(V1 の「指定した点を作図点として残す」)。
    bool keepPoints = false;
    //! ベジェの制御多角形(制御点を順に結んだ折れ線)を補助線として残すか。
    bool keepControlPolygon = false;
    //! いま描いている作業平面の向き。矩形の辺、円と円弧の面はこれで決まる。
    //! XY と決め打ちしていたので、前から見る面(ZX)の上では矩形も円も作れなかった。
    Vector3 planeNormal{0.0, 0.0, 1.0};
    Vector3 planeUAxis{1.0, 0.0, 0.0};
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
    //! 形を決めるのに指した点(keepPoints のとき)。作図点として文書へ残す。
    std::vector<Vector3> keptPoints;
    //! ベジェの制御点(keepControlPolygon のとき)。順に結んだ折れ線を補助線として文書へ残す。
    std::vector<Vector3> controlPolygon;
    //! 作れたが言っておくこと(押した点と出来た線の端がずれた、など)。黙って動かさない。
    std::vector<base::Diagnostic> warnings;
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

    //! 作業平面の向きを差し替える。平面を変えても、置いた点は捨てない。
    void SetPlane(const Vector3& normal, const Vector3& uAxis);

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
    //! Build に加えて、keepPoints なら指した点を出力に乗せる。
    [[nodiscard]] base::Result<ToolOutput> BuildKeepingPoints(
        const std::vector<Vector3>& points) const;
    //! 作業平面の u 軸・v 軸(法線に直交させたもの)。
    [[nodiscard]] Vector3 PlaneU() const;
    [[nodiscard]] Vector3 PlaneV() const;

    DrawingTool tool_ = DrawingTool::Line;
    ToolSettings settings_;
    GeometryTolerance tolerance_;
    std::vector<Vector3> points_;
};

} // namespace kachakacha::v2::modeling
