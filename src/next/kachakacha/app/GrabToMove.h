#pragma once

//! 選んだ物を掴んで動かす(v1-input-parity.md §1-2 の 11)。
//!
//! V1では、選んでいる線の上を押してそのまま引きずると、その線が付いてきた。
//! V2にはこれが無く、動かすには道具を選んで点を2つ置くしかなかった。
//! 「選ぶ → 掴む → 置く」は一番よく使う手順なので、道具を経由させない。
//!
//! 掴んだかどうかの判断はここに置く。画面側に書くと、
//! 「選択の当たり判定」と「掴む当たり判定」が別々に育って食い違う。

#include "kachakacha/app/Selection.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/SnapEngine.h"

namespace kachakacha::v2::app {

//! いま押した場所が、選んでいる物の上か。
//!
//! 選んでいない線の上なら false を返す。掴みではなく選び直しだからである。
//! 何も選んでいなければ、掴むものが無いので false。
[[nodiscard]] bool PointerGrabsSelection(const modeling::SnapScene& scene,
    const SelectionSet& selection, const geometry::ScreenMapping& mapping,
    const geometry::ScreenPoint& pointer, const geometry::GeometryTolerance& tolerance);

//! 引きずった量が「動かした」と言える大きさか。
//!
//! 押して離すだけの選び直しを、0.0mm の移動として文書へ入れないための門。
//! V1は4pxで区切っていた。同じ値を使う。
[[nodiscard]] bool DragIsFarEnough(double dxPx, double dyPx) noexcept;

//! 掴んでいる間、選んだ線を仮に動かして見せるための量。
[[nodiscard]] geometry::Vector3 DragDelta(const geometry::Vector3& from,
    const geometry::Vector3& to) noexcept;

} // namespace kachakacha::v2::app
