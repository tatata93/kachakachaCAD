#pragma once

//! 帯近似の部材ごとの曲げ半径(オーナー指示 2026-09-14 §30・§31、Codex Q1-Q5 B2)。
//!
//! ここが答えるのは2つだけである。
//!   1. **いま出来ている形から、部材ごとの半径を測る。** 推測しない。
//!   2. **人が固定した半径を、実際の形へ効かせる。** 表示だけ変えない。
//!
//! 測り方。帯近似は、細長い平らな帯を折り線でつないだ形である。
//! 折り線が受け持つ長さ L は、両隣の帯の幅の半分ずつを足したもので、
//! その折り線の角を θ とすると、その場所の丸みは `R = L / θ` である。
//! 多角形で円を近似したときの `θ = (w_i + w_{i+1}) / (2R)` そのものであり、
//! 外周の 1/4 を 90 度と決めつけるような当て推量ではない。
//!
//! 効かせ方。折り線の角は `BuildRigidBandTransforms` が
//! 「測った角 × 進行度」で使う。だから半径 R を固定するというのは、
//! その折り線の進行度を `(w / R) / θ` にすることと同じである。
//! 板の幅 w は動かさないので、**面内長は変わらない**(§10.1)。
//!
//! 帯の数は近似をやり直すと変わりうる。数が合わない古い値は捨てて自動へ戻す。
//! 開けなくするより、測り直すほうがよい。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/BandApproximation.h"
#include "kachakacha/fabrication/BendRadius.h"

#include <vector>

namespace kachakacha::v2::fabrication {

//! 部材(帯)1枚ぶんの、いま出来ている形から測った曲げ。
//! 折り線が無い(最後の帯、または平らな帯)なら radiusMm は 0 で lock は Auto。
[[nodiscard]] std::vector<BendRadius> MeasureBandBendRadii(const BandMesh& mesh,
    const std::vector<double>& creaseAnglesRad);

//! 保存してある半径と固定の別を、いま測った値へ重ねる。
//!
//! 数が合わなければ、合わない分は自動として測った値を使う。
//! `lockFlags` は 0 = 自動、1 = 固定。`radiiMm` は固定のときだけ意味を持つ。
[[nodiscard]] std::vector<BendRadius> ApplyStoredBendRadii(
    const std::vector<BendRadius>& measured, const std::vector<double>& radiiMm,
    const std::vector<int>& lockFlags);

//! 固定した半径を、折り線の進行度へ直す。
//!
//! 返る並びは折り線の本数ぶん。自動の帯は `1.0`(測ったとおり)。
//! 固定した帯は `(w / R) / θ`。θ が 0(平ら)なら曲げようがないので 1.0 のまま。
//! 進行度は 0〜4 に収める。半径を極端に小さくして形が裏返るのを防ぐ。
[[nodiscard]] std::vector<double> BendRadiusCreaseFactors(const BandMesh& mesh,
    const std::vector<double>& creaseAnglesRad, const std::vector<BendRadius>& bends);

//! 部材ごとの半径を一文にする。画面の帯へ出す。
[[nodiscard]] std::string DescribeBandBendRadiiJa(const std::vector<BendRadius>& bends,
    double percent);

} // namespace kachakacha::v2::fabrication
