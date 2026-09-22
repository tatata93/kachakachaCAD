#pragma once

//! ロフトの入力検査(断面 2〜任意 + ガイド 0〜任意 + 中心線 0〜1)。
//!
//! これまでは「ロフト = 断面 3 本以上」「案内付きロフト = ガイドちょうど 2 本 + 断面」
//! と別の作り方だった。案内付きロフトの核は 1 本目を背骨、2 本目を補助にするだけで、
//! 3 本目以降を受ける作りになっていなかった。ここでは 1 つの「ロフト」として、
//! 入力のつながりから作り方(LoftSolver)を決める。
//!
//!   ガイド 0 本                      → 断面をなめらかに通す
//!   中心線あり(ガイド 0)           → 中心線に沿って運ぶ
//!   外側のガイドが両脇に 1 本ずつ    → 断面とガイドを網(Gordon)にして全部の線を通す
//!     (内側のガイドは何本でも。外側 2 本だけでガイドが端の断面より外へ伸びていれば
//!      仮想断面を足す。片方だけ伸びている・仮想断面を作らない設定なら 2 本のレールで掃く)
//!   それ以外                         → 断面と全部のガイドを通るように張る(近似、実測で判定)
//!
//! **どのガイドも形に効く。**3 本目以降を黙って使わない作りにはしない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/GuideSurfaceSampling.h"

#include <vector>

namespace kachakacha::v2::modeling::detail {

//! LoftSections と GuidedLoft の検査。GuideSurfaceInput.cpp から呼ぶ。
[[nodiscard]] base::Result<GuideSurfaceAnalysis> AnalyzeLoft(
    const GuideSurfaceRequest& request, const GeometryTolerance& tolerance,
    std::vector<SampledChain>& sampled);

} // namespace kachakacha::v2::modeling::detail

namespace kachakacha::v2::modeling {

//! LoftSolver::RailNetwork の網。U 線 = ガイド(断面の間の部分。仮想断面の側は端まで)、
//! V 線 = 仮想断面 + 断面(並べた順)+ 仮想断面。核はこれを BuildGordonGrid へ渡す。
[[nodiscard]] GuideSurfaceRequest LoftNetworkRequest(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis);

} // namespace kachakacha::v2::modeling
