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
    //! 指定厚み。補正使用時の実測範囲は以下を参照。
    double thicknessMm = 0.0;
    //! 補正使用時のみtrue。計測値は対応点の法線距離と位置偏差の標本結果。
    //! 全域の誤差上界や、任意の方向での最短壁厚を意味しない。
    bool corrected = false;
    double normalThicknessMinMm = 0.0, normalThicknessMaxMm = 0.0;
    double maxOffsetDeviationMm = 0.0, rmsOffsetDeviationMm = 0.0;
    //! 立体の辺。画面に出すために使う。
    std::vector<geometry::CurveSegment> edges;
};

//! 面に厚みを付ける。
//!
//! `placement` は厚みをどちらへ付けるか。外側・中央・内側。
//! 中央は、面を挟んで前後へ半分ずつ付ける。
//! 厚みが正でなければ断る。0mm の板は作れない。
//! correctOppositeは明示的な補正選択。指定値を厳密に満たせる場合は既存方式を使う。
[[nodiscard]] base::Result<ThickenedSolid> ThickenSurface(
    modeling::KernelShapeHandle sourceShape, double thicknessMm,
    fabrication::ThicknessPlacement placement,
    const geometry::GeometryTolerance& tolerance, bool correctOpposite = false);

//! 1枚の平らな面から、相手の平面まで立体にする。
//! 曲面の一様法線オフセットでは平面到達を保証できないため、その入力は理由付きで断る。
//! 平らな面は傾いた相手にも全域が届く距離を計算し、結果の安全検査を行う。
//!
//! 面と平面の間を埋めた立体を返す。面が平面をまたいでいる、または平面の上に
//! 載っているときは断る(どちら側を埋めるのか決まらない)。
//! 厚みは面の点から平面までの最大距離で決まり、`thicknessMm` にはそれが入る。
[[nodiscard]] base::Result<ThickenedSolid> ThickenSurfaceToPlane(
    modeling::KernelShapeHandle sourceShape, const geometry::Vector3& planeOrigin,
    const geometry::Vector3& planeNormal, const geometry::GeometryTolerance& tolerance);

} // namespace kachakacha::v2::kernel
