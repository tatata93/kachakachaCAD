#pragma once

//! 治具(V1 の `body_surface_jig`、走査 §2-4)。
//!
//! V1 は「面 + 範囲 + 側 + すき間 + 厚み」から、面に沿った当て板(治具)の立体を作った
//! (`Body::SurfaceJig`。接触面は面から すき間 だけ離れた位置、裏面はさらに 厚み だけ先)。
//!
//! V2 では専用の立体を作らない。同じものが **既にある二つの作り方の組み合わせ** で出せる:
//!   1. 形状ガイドの「離した面」(OffsetGuide)で、面から すき間 だけ離れた接触面を作る
//!   2. その面に「厚みを付ける」(ThickenSurface)で当て板にする
//! こうすると、治具も普通の部品として展開・書き出し・ブーリアンの道に乗る。
//! 専用の実体を足すと、その道を全部二重に作ることになる。
//!
//! **側は厚みの符号で決める。** V1 の JigSide::Positive / Negative に当たる。
//! 正なら面の表側(法線の向き)、負なら裏側。すき間は 0 でもよい(面にぴったり当てる)。
//!
//! 断られかた:
//! - JIG-E001 治具のすき間は 0 以上にしてください。
//! - JIG-E002 治具の厚みは 0 にできません。
//! - JIG-E003 治具の元にする形状ガイドの面を1つ選んでください。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/FabricationSettings.h"

#include <cstddef>

namespace kachakacha::v2::app {

//! 治具の作り方を、V2 の二つの手順へ翻訳したもの。
struct SurfaceJigPlan {
    //! 接触面を作るときの「離す距離」(mm)。0 なら離さず、元の面をそのまま接触面にする。
    double offsetDistanceMm = 0.0;
    //! 接触面に付ける厚み(mm)。必ず正。
    double thicknessMm = 1.0;
    //! 厚みの付け方。表側なら外側、裏側なら内側(どちらも元の面から離れる向きへ伸ばす)。
    fabrication::ThicknessPlacement placement = fabrication::ThicknessPlacement::Outside;
    //! 接触面を作る手順が要るか(すき間が 0 なら要らない)。
    [[nodiscard]] bool NeedsOffsetSurface() const noexcept { return offsetDistanceMm != 0.0; }
};

//! すき間と厚みから手順を決める。値が使えなければ理由をつけて断る。
//! surfaceCount は選んでいる形状ガイドの面の数。1 でなければ JIG-E003。
[[nodiscard]] base::Result<SurfaceJigPlan> PlanSurfaceJig(double clearanceMm,
    double thicknessMm, std::size_t surfaceCount);

} // namespace kachakacha::v2::app
