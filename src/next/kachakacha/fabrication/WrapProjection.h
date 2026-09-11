#pragma once

//! 回り込み投影(V1 の「複数の面へ回り込み投影」、走査 §2-1)。
//!
//! パノラミックウインドウのように、**角をまたぐ窓** を一度に落とすためのもの。
//! 1枚の面へ落とす投影(SurfaceProjection)は、面から外れた点があるとその線ごと断る。
//! 角をまたぐ窓は、どの面から見ても必ず外れる部分があるので、そのままでは落とせない。
//!
//! やり方は V1 と同じ:
//!   1. 元の線を細かく標本化し、点ごとに **向きに沿っていちばん近い面** を選ぶ
//!   2. 同じ面が続く区間(run)にまとめる。閉じた線は巡回で見る
//!   3. 区間の境目を二分探索で詰める(どこで面が変わるかを、標本の粗さで決めない)
//!   4. 区間ごとに、その面へ落とす
//!
//! **面をまたいだところで勝手につながない。** 区間は面ごとに別の線として返す。
//! つなぐと、面と面の間の谷を横切る直線が1本入り、窓の形が変わってしまう。
//!
//! 断られかた:
//! - FAB-J003 回り込み投影には、落とす先の面が2枚以上要ります。
//! - FAB-J001 どの面にも当たらないところがあります(区間に分けても落ちない)。
//! - FAB-J002 落とす線がありません。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/SurfacePatch.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/Vector3.h"

#include <cstddef>
#include <vector>

namespace kachakacha::v2::fabrication {

inline constexpr const char* kWrapNeedsTwoSurfaces = "FAB-J003";

//! 1つの面に載る区間。
struct WrapProjectionRun {
    //! 入力の面の並びの番号。
    std::size_t surfaceIndex = 0;
    //! 元の線の上での区間(0〜1。閉じた線では終わりが 1 を超えることがある)。
    double startParameter = 0.0;
    double endParameter = 1.0;
    //! その面へ落ちた折れ線。
    std::vector<geometry::CurveSegment> curves;
    //! 元の線が閉じていて、全部が1枚の面に載ったとき true(落ちた線も閉じている)。
    bool closed = false;
};

//! 落とす。区間は元の線の順に並ぶ。
[[nodiscard]] base::Result<std::vector<WrapProjectionRun>> WrapProjectOntoSurfaces(
    const std::vector<SurfacePatchSamples>& surfaces,
    const std::vector<geometry::CurveSegment>& curves, const geometry::Vector3& direction,
    double toleranceMm);

} // namespace kachakacha::v2::fabrication
