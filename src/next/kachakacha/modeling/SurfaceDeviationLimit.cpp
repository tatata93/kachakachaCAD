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
    case GuideSurfaceMethod::FourEdgePatch:
        // 四辺面(GeomFill_BSplineCurves)は 4 辺をそのまま面の縁にする。
        break;
    case GuideSurfaceMethod::CurveNetworkExact:
        return SurfaceFidelity::Fitted;
    }
    return SurfaceFidelity::Interpolating;
}

SurfaceFidelity FidelityOf(const GuideSurfaceRequest& request) noexcept
{
    const auto has = [&](ChainRole role) {
        for (const GuideChain& chain : request.chains) {
            if (chain.role == role) {
                return true;
            }
        }
        return false;
    };
    switch (request.method) {
    case GuideSurfaceMethod::LoftSections:
        // ガイドも中心線も無いロフトは断面を通す。どちらかがあれば近づける作り方。
        return has(ChainRole::GuideU) || has(ChainRole::Centerline)
            ? SurfaceFidelity::Approximating
            : SurfaceFidelity::Interpolating;
    case GuideSurfaceMethod::FourEdgePatch: {
        // 内側の通る線か G1/G2 があれば、4 辺を境界に張り直す(近似拘束)。
        bool continuity = false;
        for (const GuideChain& chain : request.chains) {
            continuity = continuity || chain.continuity != SurfaceContinuity::G0;
        }
        return has(ChainRole::GuideU) || continuity ? SurfaceFidelity::Approximating
                                                    : SurfaceFidelity::Interpolating;
    }
    default:
        break;
    }
    return FidelityOf(request.method);
}

double SurfaceDeviationLimitMm(GuideSurfaceMethod method,
    const geometry::GeometryTolerance& tolerance) noexcept
{
    if (FidelityOf(method) == SurfaceFidelity::Interpolating) {
        return ExactLimitMm(tolerance);
    }
    if (FidelityOf(method) == SurfaceFidelity::Fitted) {
        return std::max(kNetworkFitDeviationMm, ExactLimitMm(tolerance));
    }
    // 近づける作り方。後の工程(板材の曲げ近似)が許している量までとする。
    // **その量より粗い面は、後で直しようがない。**
    return std::max(kFabricationDeviationMm, ExactLimitMm(tolerance));
}

bool SurfaceDeviationIsWorthSaying(GuideSurfaceMethod method, double deviationMm,
    const geometry::GeometryTolerance& tolerance) noexcept
{
    return deviationMm > ExactLimitMm(tolerance)
        && FidelityOf(method) != SurfaceFidelity::Interpolating;
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
    if (FidelityOf(method) == SurfaceFidelity::Fitted) {
        return "全部の線を通る形を面へ写しました。写す誤差で線から最大 " + value + " mm 外れています。";
    }
    return "近づけて作る面です。指定した線から最大 " + value + " mm 外れています。";
}

double SurfaceDeviationLimitMm(const GuideSurfaceRequest& request,
    const geometry::GeometryTolerance& tolerance) noexcept
{
    if (FidelityOf(request) == SurfaceFidelity::Interpolating) {
        return ExactLimitMm(tolerance);
    }
    if (FidelityOf(request) == SurfaceFidelity::Fitted) {
        return std::max(kNetworkFitDeviationMm, ExactLimitMm(tolerance));
    }
    return std::max(kFabricationDeviationMm, ExactLimitMm(tolerance));
}

std::string SurfaceDeviationNoteJa(const GuideSurfaceRequest& request, double deviationMm,
    const geometry::GeometryTolerance& tolerance)
{
    if (!(deviationMm > ExactLimitMm(tolerance))
        || FidelityOf(request) == SurfaceFidelity::Interpolating) {
        return {};
    }
    // 言い方は作り方の名前で決める版と同じにする。
    return SurfaceDeviationNoteJa(FidelityOf(request) == SurfaceFidelity::Fitted
            ? GuideSurfaceMethod::CurveNetworkExact : GuideSurfaceMethod::GuidedLoft,
        deviationMm, tolerance);
}

} // namespace kachakacha::v2::modeling
