#pragma once

//! 押し出しの矢印ハンドルを引いたときの距離(オーナー指示 2026-09-14 の §6)。
//!
//! 画面の上でマウスを動かした量を、押し出しの向きに沿った mm へ直す。
//! 「押した場所から、いまの場所まで、**押し出しの向きへ何 mm 進んだか**」だけを出す。
//!
//! 画面の中の直線へ落とす計算をここへ置くのは、
//! 視点を回しても手応えが変わらないようにするためである。
//! 画面の縦の動きをそのまま mm にすると、真上から見たときに
//! いくら引いても距離が変わらない、ということが起きる。
//!
//! Qt を知らない。画面を出さずに確かめられる。

#include "kachakacha/geometry/ScreenMapping.h"
#include "kachakacha/geometry/Vector3.h"

#include <optional>
#include <string>

namespace kachakacha::v2::app {

//! 矢印ハンドルの置き場所と向き。
struct ExtrudeHandle {
    //! 矢印の根元。輪郭の重心を使う。
    geometry::Vector3 origin{};
    //! 押し出しの向き。単位ベクトルであること。
    geometry::Vector3 direction{0.0, 0.0, 1.0};
    //! いまの距離(mm)。負にもなる(向きの逆へ引いたとき)。
    double distanceMm = 0.0;
};

//! 矢印の先端の位置。
[[nodiscard]] geometry::Vector3 ExtrudeHandleTip(const ExtrudeHandle& handle) noexcept;

//! 画面の点を、押し出しの向きの直線へ落として距離を出す。
//!
//! 押し出しの向きが画面と垂直に近いと、画面の上では点にしか見えないので
//! 距離が決まらない。そのときは値を返さない ── 呼ぶ側は数値の欄で入れてもらう。
//! 適当な値を返すと、少し動かしただけで距離が跳ねる。
[[nodiscard]] std::optional<double> ExtrudeDistanceFromPointer(
    const ExtrudeHandle& handle, const geometry::ScreenMapping& mapping,
    const geometry::ScreenPoint& pointer);

//! 引いた量を、押した時点の距離へ足した結果。
//!
//! 押した瞬間の画面位置を基準にするので、矢印のどこを掴んでも飛ばない。
//! 落とせないときは、押した時点の距離をそのまま返す(動かさない)。
[[nodiscard]] double ExtrudeDistanceAfterDrag(const ExtrudeHandle& handle,
    const geometry::ScreenMapping& mapping, const geometry::ScreenPoint& pressedAt,
    const geometry::ScreenPoint& pointer);

//! 矢印の近くに出す文字。「15.0 mm」。
//! 小数1桁にするのは、0.01mm まで出すと引いている間ずっと数字が踊るためである。
[[nodiscard]] std::string ExtrudeDistanceLabel(double distanceMm);

} // namespace kachakacha::v2::app
