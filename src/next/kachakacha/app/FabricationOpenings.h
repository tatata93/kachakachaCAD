#pragma once

//! 開口(窓など)を、帯へ近似した部材の型紙へ切り出す(V1 の「またぐ窓」の対策そのもの)。
//!
//! 帯近似した面の上に描いた窓は、帯の境目をまたぐことがある。
//! またぐ窓を「区間ごと」に切ると、真ん中の帯の取り分が細い三角2つに割れ、
//! 窓の中央が切り抜かれずに板が残る(V1 でオーナーが報告した不具合)。
//! ここは、輪郭を分割軸のパラメータで帯の範囲ごとに多角形として切り出し、
//! 帯ごとの展開座標へ写す。載っていない窓は載っていないと言う(近い帯へ寄せない)。
//!
//! OCCT を使わない。

#include "kachakacha/fabrication/BandApproximation.h"
#include "kachakacha/fabrication/PatternLayout.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <cstddef>
#include <vector>

namespace kachakacha::v2::app {

//! 切り出す先。帯メッシュと、その帯が panels のどこから並んでいるか。
struct BandOpeningTarget {
    const fabrication::BandMesh* mesh = nullptr;
    //! 帯の境目(昇順、帯数+1)。ApproximateBands の railParameters。
    std::vector<double> railParameters;
    //! panels の中で、この面の帯1が入っている位置。
    std::size_t firstPanelIndex = 0;
    //! 輪郭の点がメッシュからこれ以上離れていれば「載っていない」。
    double snapToleranceMm = 0.35;
};

//! 開口1つを帯の型紙へ切り出す。載っていれば true(取り分を panels へ足す)。
//! 載っていなければ false で、panels は変えない。
[[nodiscard]] bool ClipOpeningIntoBandPanels(const BandOpeningTarget& target,
    const std::vector<geometry::CurveSegment>& opening, double toleranceMm,
    std::vector<fabrication::PatternPanel>& panels);

} // namespace kachakacha::v2::app
