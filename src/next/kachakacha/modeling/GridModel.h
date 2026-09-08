#pragma once

//! グリッド(PRD-064 / 065、AT-UIX-005)。
//!
//! 大事な決まりが3つある。
//!   1. 原点は作業平面の UV で持つ。作業平面が動けば、UV を保ったまま付いていく。
//!   2. 指している作業平面が無くなったら、別の平面へ勝手に付け替えない。
//!   3. 画面での間隔が細かすぎる副点は出さない。出すと点だらけで選べなくなる。
//!
//! V1 はグリッド原点をワールド座標で持っていたので、作業平面を動かすと
//! グリッドだけ取り残された。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <optional>
#include <vector>

namespace kachakacha::v2::modeling {

using base::EntityId;
using geometry::Vector3;

//! 保存する形。ワールド座標は持たない。
struct GridDefinition {
    bool visible = true;
    //! どの作業平面の上に置くか。空なら世界の XY 平面。
    std::optional<EntityId> workPlaneId;
    //! その平面の上での原点(mm)。
    double originUmm = 0.0;
    double originVmm = 0.0;
    double majorSpacingMm = 10.0;
    //! 0 = 副点なし、2 = 1/2、3 = 1/3、4 = 1/4。
    int subdivision = 0;
};

//! 画面へ出すために評価した形。
struct GridEvaluation {
    bool visible = false;
    Vector3 originXyz{};
    Vector3 uDirection{1.0, 0.0, 0.0};
    Vector3 vDirection{0.0, 1.0, 0.0};
    double majorSpacingMm = 10.0;
    double minorSpacingMm = 0.0;
    int subdivision = 0;
    //! 主点1マスの中にある副点の数(主点そのものは数えない)。
    int minorPointsPerCell = 0;
    //! 主点の間の辺1本にある副点の数。
    int minorPointsPerEdge = 0;
    //! いまの倍率で副点を出すか。細かすぎるときは出さない。
    bool minorVisible = false;
    //! 主点の画面での間隔(px)。
    double majorSpacingPx = 0.0;
};

//! 副点の数。1/2 なら辺に1点、マスの中に3点。
[[nodiscard]] int MinorPointsPerEdge(int subdivision) noexcept;
[[nodiscard]] int MinorPointsPerCell(int subdivision) noexcept;

//! 副点を出してよい最小の画面間隔(px)。geometry-contract §6.3。
inline constexpr double kMinimumGridSpacingPx = 6.0;

//! 評価する。
//!
//! `plane` は `workPlaneId` が指す作業平面。指す先が無いときは値を渡さない。
//! そのとき、別の平面へ勝手に付け替えず、壊れた参照として断る。
[[nodiscard]] base::Result<GridEvaluation> EvaluateGrid(const GridDefinition& definition,
    const std::optional<WorkPlaneFrame>& plane, double pixelsPerMillimeter);

//! 画面で指した位置へ原点を動かす。UV へ直して保存する。
[[nodiscard]] base::Result<GridDefinition> MoveGridOrigin(const GridDefinition& definition,
    const WorkPlaneFrame& plane, const Vector3& worldTarget);

//! 数値で原点を決める。UV でも XYZ でも受ける(PRD-065)。
[[nodiscard]] base::Result<GridDefinition> SetGridOriginUv(
    const GridDefinition& definition, double u, double v);
[[nodiscard]] base::Result<GridDefinition> SetGridOriginXyz(
    const GridDefinition& definition, const WorkPlaneFrame& plane, const Vector3& xyz);

//! 間隔と細かさを決める。
[[nodiscard]] base::Result<GridDefinition> SetGridSpacing(const GridDefinition& definition,
    double majorSpacingMm, int subdivision);

} // namespace kachakacha::v2::modeling
