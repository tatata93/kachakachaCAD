#pragma once

//! 面の編集(プロンプト additional_surface_tools)。
//!
//!   面を合わせる   直す面の縁を、隣の面の縁へ G0/G1/G2 で合わせる(元の面は残す)
//!   面をつなぐ     2 枚の面の縁のあいだを、両端 G0/G1/G2 の面で渡す
//!   面を整える     形を許容差の内側に保ったまま、制御点の少ない面に作り直す
//!   対称に写す     面を平面に対して写し、境目の滑らかさを測る
//!   U/V 線         面の U 方向・V 方向の線を、ふつうの線として取り出す
//!   面へ投影       線を面へ落とし、面の上の曲線にする(厳密。折れ線にしない)
//!
//! どれも **元の面を壊さない。** 新しい面(または線)を作り、文書へは呼び出し側が
//! ふつうの Feature として入れる(Undo/Redo と保存に載る)。
//! 作ったものは必ず測る。滑らかさを指定したのに届かなければ、作れたことにしない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

#include <string>
#include <vector>

namespace kachakacha::v2::kernel {

inline constexpr const char* kEditSourceMissing = "KER-D001";
inline constexpr const char* kEditEdgeMissing = "KER-D002";
inline constexpr const char* kEditEndsApart = "KER-D003";
inline constexpr const char* kEditMatchFailed = "KER-D004";
inline constexpr const char* kEditBridgeFailed = "KER-D005";
inline constexpr const char* kEditRefitFailed = "KER-D006";
inline constexpr const char* kEditMirrorFailed = "KER-D007";
inline constexpr const char* kEditIsoFailed = "KER-D008";
inline constexpr const char* kEditProjectFailed = "KER-D009";
inline constexpr const char* kEditContinuityMissed = "KER-D010";

//! 面の縁 1 本。番号は面の辺を辿った順(0 始まり)。形を作り直すまで変わらない。
struct SurfaceEdgeInfo {
    int index = -1;
    //! 押した点から縁までの距離(mm)。
    double distanceMm = 0.0;
    //! 下見と札に使う折れ線。
    std::vector<geometry::Vector3> polyline;
};

//! 面の編集の結果。surface は面を作る道と同じ形(標本・外周・核の番号)。
struct SurfaceEditResult {
    modeling::GuideSurfaceResult surface;
    //! 元の面からどれだけ動いたか(合わせる・整える)。負は「測る対象が無い」。
    double deviationMm = -1.0;
    //! 指定した縁(対称なら境目)での滑らかさ。負は「測る対象が無い」。
    double continuityG1Deg = -1.0;
    double continuityG2 = -1.0;
    //! 整える前後の制御点の数(整えるのとき)。
    int polesBefore = 0;
    int polesAfter = 0;
    //! 結果を人に言う一文(測った値を含む)。
    std::string noteJa;
};

//! 点に一番近い、面の縁。
[[nodiscard]] base::Result<SurfaceEdgeInfo> NearestSurfaceEdge(
    const modeling::KernelShapeHandle& surface, const geometry::Vector3& point);

//! 番号の縁の折れ線(札と下見)。
[[nodiscard]] base::Result<SurfaceEdgeInfo> SurfaceEdgeAt(
    const modeling::KernelShapeHandle& surface, int edgeIndex);

//! 直す面の縁を、合わせ先の面の縁へ合わせる。ほかの縁はそのまま、元の面を初期形にして
//! 張り直す(遠いところほど元の形のまま)。両方の縁の端が離れていれば断る。
[[nodiscard]] base::Result<SurfaceEditResult> MatchSurfaceEdge(
    const modeling::KernelShapeHandle& target, int targetEdge,
    const modeling::KernelShapeHandle& reference, int referenceEdge,
    modeling::SurfaceContinuity order, const geometry::GeometryTolerance& tolerance);

//! 2 枚の面の縁のあいだを渡す面。tension は縁から出る向きの強さ(1 = 標準)。
[[nodiscard]] base::Result<SurfaceEditResult> BridgeSurfaceEdges(
    const modeling::KernelShapeHandle& first, int firstEdge,
    modeling::SurfaceContinuity firstOrder, const modeling::KernelShapeHandle& second,
    int secondEdge, modeling::SurfaceContinuity secondOrder, double tension,
    const geometry::GeometryTolerance& tolerance);

//! 面を整える。元の面から toleranceMm の内側に保ったまま、3 次の少ない制御点の面にする。
//! 許容に届かない・制御点が減らない・解析的な面(平面・円筒など)なら、理由を言って断る。
[[nodiscard]] base::Result<SurfaceEditResult> RefitSurface(
    const modeling::KernelShapeHandle& source, double toleranceMm,
    const geometry::GeometryTolerance& tolerance);

//! 面を平面に対して写す。元の面の縁が対称面に乗っていれば、境目の滑らかさを測る。
[[nodiscard]] base::Result<SurfaceEditResult> MirrorSurface(
    const modeling::KernelShapeHandle& source, const geometry::Vector3& planePoint,
    const geometry::Vector3& planeNormal, const geometry::GeometryTolerance& tolerance);

//! 面の U/V 線。direction: 0 = U 方向の線(V 一定)、1 = V 方向の線(U 一定)、2 = 両方。
//! count 本を内側に等間隔で。面の外(穴・切り欠き)に出るところは切って返す。
[[nodiscard]] base::Result<std::vector<std::vector<geometry::CurveSegment>>> ExtractIsoCurves(
    const modeling::KernelShapeHandle& source, int direction, int count,
    const geometry::GeometryTolerance& tolerance);

//! 線を、向き direction に沿って面へ落とす。落ちた線は面の上の曲線(厳密)。
[[nodiscard]] base::Result<std::vector<std::vector<geometry::CurveSegment>>> ProjectWiresOntoSurface(
    const modeling::KernelShapeHandle& surface,
    const std::vector<std::vector<geometry::CurveSegment>>& wires,
    const geometry::Vector3& direction, const geometry::GeometryTolerance& tolerance);

} // namespace kachakacha::v2::kernel
