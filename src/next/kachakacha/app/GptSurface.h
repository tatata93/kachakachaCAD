#pragma once

#include "kachakacha/document/Document.h"
#include "kachakacha/modeling/SnapEngine.h"

namespace kachakacha::v2::app {

//! 保存上のmethod/rolesは共通の語彙だが、既存の面生成器は呼ばない。
inline constexpr int kGptBoundaryMethod = 5;
inline constexpr int kGptSectionsMethod = 2;
inline constexpr int kGptBoundaryRole = 5;
inline constexpr int kGptInteriorRole = 3;
inline constexpr int kGptSectionRole = 2;

struct GptSurfaceCurve {
    std::vector<geometry::CurveSegment> segments;
    std::string label;
    bool closed = false;
    int role = kGptBoundaryRole;
};

struct GptSurfaceRequest {
    bool loft = false;
    double maximumDeviationMm = 0.01;
    std::vector<GptSurfaceCurve> curves;
};

//! 全参照を解決する。不明な参照・役割・入力を読み飛ばさない。
[[nodiscard]] base::Result<GptSurfaceRequest> ResolveGptSurface(
    const document::Document& document, const modeling::SnapScene& scene,
    const domain::CreateGuideSurfaceDefinition& definition);

//! 外周の接続と各断面を検証する。外周の線は1本の順序付き鎖へまとめる。
[[nodiscard]] base::Result<GptSurfaceRequest> ValidateGptSurface(
    GptSurfaceRequest request, const geometry::GeometryTolerance& tolerance);

} // namespace kachakacha::v2::app
