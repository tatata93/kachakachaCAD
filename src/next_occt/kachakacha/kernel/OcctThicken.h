#pragma once

//! 面に厚みを付けて立体にする(工程2の「面をソリッド化する」)。
//!
//! オーナーの手順では、3次元曲面は「断面から面を作る → その面を任意の厚みで
//! 立体にする」で立体になる。V2 は面までは作れたのに、その面を立体にする道が
//! 一本も無かった。面の唯一の下流が製作(展開)だけだったので、
//! 「面は作れるが立体には戻れない」状態になっていた。
//!
//! ここは面(またはシェル)を受け取り、法線方向へ厚みを付けた立体を返す。
//! 平らな面なら押し出しと同じ結果になるが、**曲がった面でも通る** ところが違う。
//!
//! OCCT の型を外へ出さない。返すのは core の型だけ。
//! OCCT が投げる例外はここで受けて Diagnostic へ直す。外へ漏らさない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/FabricationSettings.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

namespace kachakacha::v2::kernel {

//! 厚み付けの層の診断コード。
inline constexpr const char* kThickenFailed = "KER-T001";
inline constexpr const char* kThickenBadThickness = "KER-T002";
inline constexpr const char* kThickenSourceMissing = "KER-T003";
inline constexpr const char* kThickenBadTarget = "KER-T004";
inline constexpr const char* kThickenTrimFailed = "KER-T005";

struct ThickenedSolid {
    modeling::KernelShapeHandle handle;
    double volumeMm3 = 0.0;
    //! 実際に付いた厚み。指定と一致する。
    double thicknessMm = 0.0;
    //! 立体の辺。画面に出すために使う。
    std::vector<geometry::CurveSegment> edges;
};

//! 面に厚みを付ける。
//!
//! `placement` は厚みをどちらへ付けるか。外側・中央・内側。
//! 中央は、面を挟んで前後へ半分ずつ付ける。
//! 厚みが正でなければ断る。0mm の板は作れない。
[[nodiscard]] base::Result<ThickenedSolid> ThickenSurface(
    modeling::KernelShapeHandle sourceShape, double thicknessMm,
    fabrication::ThicknessPlacement placement,
    const geometry::GeometryTolerance& tolerance);

//! 面から、相手の平面まで立体にする(「任意の面まで立体化」)。
//!
//! 面と平面の間を埋めた立体を返す。面が平面をまたいでいる、または平面の上に
//! 載っているときは断る(どちら側を埋めるのか決まらない)。
//! 厚みは面の点から平面までの最大距離で決まり、`thicknessMm` にはそれが入る。
[[nodiscard]] base::Result<ThickenedSolid> ThickenSurfaceToPlane(
    modeling::KernelShapeHandle sourceShape, const geometry::Vector3& planeOrigin,
    const geometry::Vector3& planeNormal, const geometry::GeometryTolerance& tolerance);

} // namespace kachakacha::v2::kernel
