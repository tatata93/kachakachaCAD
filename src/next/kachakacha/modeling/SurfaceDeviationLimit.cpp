#include "kachakacha/modeling/SurfaceDeviationLimit.h"

#include <algorithm>

namespace kachakacha::v2::modeling {
namespace {

//! 数値誤差の桁。ここまでは「通っている」と見なす。
[[nodiscard]] double ExactLimitMm(const geometry::GeometryTolerance& tolerance) noexcept
{
    return std::max(tolerance.modelLinearMm * 10.0, 1.0e-4);
}

} // namespace

SurfaceFidelity FidelityOf(GuideSurfaceMethod method) noexcept
{
    switch (method) {
    case GuideSurfaceMethod::GuidedLoft:
    case GuideSurfaceMethod::GordonNetwork:
    case GuideSurfaceMethod::BoundaryFill:
        // MakePipeShell / MakeFilling。作りからして近似である。
        return SurfaceFidelity::Approximating;
    case GuideSurfaceMethod::PlanarBoundary:
    case GuideSurfaceMethod::RuledSections:
    case GuideSurfaceMethod::LoftSections:
    case GuideSurfaceMethod::Revolve:
    case GuideSurfaceMethod::OffsetGuide:
        break;
    }
    return SurfaceFidelity::Interpolating;
}

double SurfaceDeviationLimitMm(GuideSurfaceMethod method,
    const geometry::GeometryTolerance& tolerance) noexcept
{
    if (FidelityOf(method) == SurfaceFidelity::Interpolating) {
        return ExactLimitMm(tolerance);
    }
    // 近づける作り方。後の工程(板材の曲げ近似)が許している量までとする。
    // **その量より粗い面は、後で直しようがない。**
    return std::max(kFabricationDeviationMm, ExactLimitMm(tolerance));
}

bool SurfaceDeviationIsWorthSaying(GuideSurfaceMethod method, double deviationMm,
    const geometry::GeometryTolerance& tolerance) noexcept
{
    return deviationMm > ExactLimitMm(tolerance)
        && FidelityOf(method) == SurfaceFidelity::Approximating;
}

std::string SurfaceDeviationNoteJa(GuideSurfaceMethod method, double deviationMm,
    const geometry::GeometryTolerance& tolerance)
{
    if (!SurfaceDeviationIsWorthSaying(method, deviationMm, tolerance)) {
        return {};
    }
    // 小数第3位まで。板材の話なので、それより細かい桁は読む意味がない。
    const long long micro = static_cast<long long>(deviationMm * 1000.0 + 0.5);
    std::string value = std::to_string(micro / 1000) + "."
        + std::to_string((micro / 100) % 10) + std::to_string((micro / 10) % 10)
        + std::to_string(micro % 10);
    return "近づけて作る面です。指定した線から最大 " + value + " mm 外れています。";
}

} // namespace kachakacha::v2::modeling
