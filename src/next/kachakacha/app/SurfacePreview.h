#pragma once

//! 面の下見を、線の束にする(オーナー指示 2026-09-15 §12)。
//!
//! 「面を作る」は、確定するまで文書へ何も書かない。
//! 書かずに見せるには、出来上がる面を線で描くしかない。
//! ここは、カーネルが返した標本の格子と境界を、画面が描ける折れ線に直すだけの場所。
//! Qt も OCCT も知らない。だから雲の側で試験できる。

#include "kachakacha/fabrication/SurfacePatch.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/Vector3.h"

#include <cstddef>
#include <vector>

namespace kachakacha::v2::app {

using fabrication::SurfacePatchSamples;
using geometry::CurveSegment;
using geometry::Vector3;

//! 下見の線。1本が1つの折れ線。
using PreviewLoops = std::vector<std::vector<Vector3>>;

//! 標本の格子を、縦横の線にする。
//!
//! 全部の行と列を引くと、標本が細かいほど真っ黒になって形が見えない。
//! だから最大 `maxLines` 本まで、端を必ず含めて、等間隔で間引く。
//! 標本が足りないときは空を返す(嘘の形を描かない)。
[[nodiscard]] PreviewLoops SurfaceGridLines(const SurfacePatchSamples& samples,
    std::size_t maxLines = 9);

//! 面の境界を、折れ線にする。
[[nodiscard]] PreviewLoops SurfaceBoundaryLines(const std::vector<CurveSegment>& boundary,
    int stepsPerCurve = 12);

//! 画面へ渡す下見。境界を先に、格子を後に並べる。
[[nodiscard]] PreviewLoops SurfacePreviewLines(const SurfacePatchSamples& samples,
    const std::vector<CurveSegment>& boundary, std::size_t maxLines = 9);

} // namespace kachakacha::v2::app
