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
#include "kachakacha/modeling/GuideSurfaceResult.h"

namespace kachakacha::v2::kernel {

//! 厚み付けの層の診断コード。
inline constexpr const char* kThickenFailed = "KER-T001";
inline constexpr const char* kThickenBadThickness = "KER-T002";
inline constexpr const char* kThickenSourceMissing = "KER-T003";

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

} // namespace kachakacha::v2::kernel
