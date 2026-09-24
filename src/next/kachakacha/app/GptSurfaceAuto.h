#pragma once
#include "kachakacha/app/GptSurface.h"

namespace kachakacha::v2::app {
struct GptSurfaceAutoResult {
    domain::CreateGuideSurfaceDefinition definition;
    std::size_t candidateCount = 0;
    std::size_t candidateIndex = 0;
    std::size_t boundaryCount = 0;
};
//! 推定結果だけを返す。全Segmentの参照を保持し、保存時は役割を固定する。
[[nodiscard]] base::Result<GptSurfaceAutoResult> AutoGptSurfaceBoundary(
    const modeling::SnapScene& scene, const domain::CreateGuideSurfaceDefinition& input,
    const geometry::GeometryTolerance& tolerance, std::size_t candidate = 0);
}
