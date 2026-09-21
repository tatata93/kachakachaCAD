#pragma once

//! 四辺面の入力検査(U0・U1・V0・V1 の 4 辺 + 内側の通る線 0〜任意)。
//!
//! 鉄道車両の前頭部のように、意味のある 4 辺で囲われた 1 枚の面を張る。
//! 4 辺は渡した順も向きも問わない。端点のつながりから輪の順と向きを決め直す。
//! 閉じない 4 辺、ねじれた並び(端が 3 本以上集まる)ははっきり断る。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/GuideSurfaceSampling.h"

#include <vector>

namespace kachakacha::v2::modeling::detail {

[[nodiscard]] base::Result<GuideSurfaceAnalysis> AnalyzeFourEdgePatch(
    const GuideSurfaceRequest& request, const GeometryTolerance& tolerance,
    std::vector<SampledChain>& sampled);

} // namespace kachakacha::v2::modeling::detail
