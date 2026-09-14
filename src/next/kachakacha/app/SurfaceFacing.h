#pragma once

//! 標本の格子から「正対する向き」を出す(オーナー指示 2026-09-14 §5)。
//!
//! 形状ガイドの面は、格子状の標本として持っている。曲がった面には
//! 1つの法線が無いので、**真ん中あたりの向き** を使う。
//! 平らな面なら、どこで測っても同じ向きになる。
//!
//! ここは OCCT を知らない。格子の点だけを見る。

#include "kachakacha/fabrication/SurfacePatch.h"
#include "kachakacha/geometry/Vector3.h"

#include <optional>

namespace kachakacha::v2::app {

//! 面の上の1点と、そこでの向き。
struct SurfacePose {
    geometry::Vector3 point{};
    geometry::Vector3 normal{0.0, 0.0, 1.0};
    //! 面の上の「横」の向き(列方向)。画面の上下を決めるのに使う。
    geometry::Vector3 uAxis{1.0, 0.0, 0.0};
};

//! 格子の真ん中の向き。潰れていて向きが決まらなければ値を返さない。
//!
//! 真ん中で決まらないときは、その周りを少しずつずらして探す。
//! 端が尖っている面(円錐の頂点など)でも、真ん中さえ生きていれば通る。
[[nodiscard]] std::optional<SurfacePose> SurfaceFacingPose(
    const fabrication::SurfacePatchSamples& samples);

} // namespace kachakacha::v2::app
