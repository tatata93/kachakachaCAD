#pragma once

//! 面の解析の標本を取る(プロンプト surface_analysis)。色は画面が塗る(app/SurfaceAnalysis)。
//!
//! 面を UV の格子で細かく取り、各点で本当の面の法線とガウス曲率・平均曲率を測る。
//! 縁ごとの曲率コーム、U/V 線、隣の面との境目の連続、面を作った線からの離れも測る。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
#include "kachakacha/modeling/SurfaceAnalysisData.h"

#include <vector>

namespace kachakacha::v2::kernel {

inline constexpr const char* kAnalysisFailed = "KER-A101";

//! 面(形状ガイドの面・立体の面)の解析の標本。gridCells は面ごとの UV の区切り数。
[[nodiscard]] base::Result<modeling::SurfaceAnalysisData> AnalyzeSurfaceShape(
    const modeling::KernelShapeHandle& shape, int gridCells = 40, int isoCount = 8);

//! 面の縁ごとに、ほかの面(neighbors)とのつながりを測る。
[[nodiscard]] base::Result<std::vector<modeling::EdgeContinuitySample>> SurfaceEdgeContinuity(
    const modeling::KernelShapeHandle& surface,
    const std::vector<modeling::KernelShapeHandle>& neighbors,
    const geometry::GeometryTolerance& tolerance);

//! 面を作った線ごとに、線の上の点が面からどれだけ離れているか。
[[nodiscard]] base::Result<std::vector<modeling::DeviationSample>> DeviationFromChains(
    const modeling::KernelShapeHandle& surface,
    const std::vector<std::vector<geometry::CurveSegment>>& chains);

} // namespace kachakacha::v2::kernel
