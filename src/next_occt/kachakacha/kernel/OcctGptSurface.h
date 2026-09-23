#pragma once

#include "kachakacha/app/GptSurface.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

namespace kachakacha::v2::kernel {
//! GPT版専用。旧面生成の推奨・役割推定・生成処理を通さない。
[[nodiscard]] base::Result<modeling::GuideSurfaceResult> BuildGptSurface(
    const app::GptSurfaceRequest& request, const geometry::GeometryTolerance& tolerance);
} // namespace kachakacha::v2::kernel
