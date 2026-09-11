#pragma once

//! 塗り方と塗る順(棚卸し A-1)。
//!
//! 画面は QPainter の2次元描画である。深度バッファが無いので、
//! **奥から手前へ順に塗る**(画家のやり方)。順を間違えると、
//! 奥の面が手前の面の上に乗って、形が裏返って見える。
//!
//! 明るさは決まった向きの光で決める。光を視線に固定すると、
//! どこから見ても同じ濃さになって、丸みが読めない。
//! V1 と同じく、左上・手前から当てる。
//!
//! ここは Qt を知らない。色の作り方(何色にするか)は画面の仕事で、
//! ここが決めるのは **0〜1 の明るさ** と **並べる順** だけである。

#include "kachakacha/modeling/ShapeMesh.h"

#include <cstddef>
#include <vector>

namespace kachakacha::v2::view {

using geometry::Vector3;
using modeling::MeshTriangle;

//! 光の向き(目からモデルへ向かう向きと同じ向きで表す)。左上・手前から当てる。
[[nodiscard]] Vector3 StandardLightDirection() noexcept;

//! 面の明るさ。0(影)〜1(真正面)。
//!
//! 裏を向いた面も 0 にはしない。開いた面(立体でないもの)は裏から見ることがあり、
//! 真っ黒だと「消えた」ようにしか見えないためである。下限は 0.25。
[[nodiscard]] double LambertShade(const Vector3& normal, const Vector3& lightDirection) noexcept;

//! その三角形が裏を向いているか。閉じた立体では描かなくてよい。
//! 開いた面では **見てはいけない**(裏から見ることがある)。
[[nodiscard]] bool BackFacing(const MeshTriangle& triangle,
    const Vector3& viewDirection) noexcept;

//! 奥から手前へ並べた番号。viewDirection は目からモデルへ向かう向き。
//! 同じ奥行きのものは入っている順を保つ。毎回同じ絵になる。
[[nodiscard]] std::vector<std::size_t> PainterOrder(const std::vector<MeshTriangle>& triangles,
    const Vector3& viewDirection);

} // namespace kachakacha::v2::view
