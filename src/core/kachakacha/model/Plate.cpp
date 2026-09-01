#include "kachakacha/model/Plate.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace kachakacha::model {

bool PlateSurfaceRange::IsFull(double tolerance) const noexcept
{
    return std::abs(minimumU) <= tolerance
        && std::abs(maximumU - 1.0) <= tolerance
        && std::abs(minimumV) <= tolerance
        && std::abs(maximumV - 1.0) <= tolerance;
}

Plate::Plate(
    Surface sourceSurface,
    double thickness,
    PlateThicknessDirection direction,
    PlateSurfaceRange range)
    : Plate(std::move(sourceSurface), thickness, thickness, direction, range)
{
}

Plate::Plate(
    Surface sourceSurface,
    double startThickness,
    double endThickness,
    PlateThicknessDirection direction,
    PlateSurfaceRange range)
    : sourceSurface_(std::move(sourceSurface)),
      thickness_(startThickness),
      endThickness_(endThickness),
      direction_(direction),
      range_(range)
{
    if (!std::isfinite(thickness_) || thickness_ <= 0.0
        || !std::isfinite(endThickness_) || endThickness_ <= 0.0) {
        throw std::invalid_argument("Plate start and end thickness must be positive finite values.");
    }
    switch (direction_) {
    case PlateThicknessDirection::Positive:
    case PlateThicknessDirection::Centered:
    case PlateThicknessDirection::Negative:
        break;
    default:
        throw std::invalid_argument("Plate thickness direction is invalid.");
    }
    if (!std::isfinite(range_.minimumU) || !std::isfinite(range_.maximumU)
        || !std::isfinite(range_.minimumV) || !std::isfinite(range_.maximumV)
        || range_.minimumU < 0.0 || range_.maximumU > 1.0
        || range_.minimumV < 0.0 || range_.maximumV > 1.0
        || range_.maximumU - range_.minimumU <= 1.0e-9
        || range_.maximumV - range_.minimumV <= 1.0e-9) {
        throw std::invalid_argument("Plate surface range must be a non-empty part of the source surface.");
    }
    ValidateGeometry();
}

void Plate::ValidateGeometry() const
{
    constexpr int kSamples = 12;
    constexpr double kMinimumLength = 1.0e-9;
    constexpr double kMinimumSine = 1.0e-7;
    const double localStep = 0.2 / static_cast<double>(kSamples);

    for (int uIndex = 0; uIndex < kSamples; ++uIndex) {
        const double localU = (static_cast<double>(uIndex) + 0.5) / kSamples;
        const double uBefore = std::max(0.0, localU - localStep);
        const double uAfter = std::min(1.0, localU + localStep);
        for (int vIndex = 0; vIndex < kSamples; ++vIndex) {
            const double localV = (static_cast<double>(vIndex) + 0.5) / kSamples;
            const double vBefore = std::max(0.0, localV - localStep);
            const double vAfter = std::min(1.0, localV + localStep);

            const double sourceU = SourceU(localU);
            const double sourceV = SourceV(localV);
            const geometry::Vector3 sourceDerivativeU =
                (sourceSurface_.Evaluate(SourceU(uAfter), sourceV)
                    - sourceSurface_.Evaluate(SourceU(uBefore), sourceV))
                / (uAfter - uBefore);
            const geometry::Vector3 sourceDerivativeV =
                (sourceSurface_.Evaluate(sourceU, SourceV(vAfter))
                    - sourceSurface_.Evaluate(sourceU, SourceV(vBefore)))
                / (vAfter - vBefore);
            const double sourceULength = sourceDerivativeU.Length();
            const double sourceVLength = sourceDerivativeV.Length();
            const geometry::Vector3 sourceCross =
                geometry::Cross(sourceDerivativeU, sourceDerivativeV);
            if (!sourceDerivativeU.IsFinite() || !sourceDerivativeV.IsFinite()
                || sourceULength <= kMinimumLength
                || sourceVLength <= kMinimumLength
                || sourceCross.Length()
                    <= sourceULength * sourceVLength * kMinimumSine) {
                throw std::invalid_argument(
                    "Plate source surface has a degenerate interior region.");
            }

            for (const double layer : {0.0, 1.0}) {
                const geometry::Vector3 center = Evaluate(localU, localV, layer);
                const geometry::Vector3 derivativeU =
                    (Evaluate(uAfter, localV, layer)
                        - Evaluate(uBefore, localV, layer))
                    / (uAfter - uBefore);
                const geometry::Vector3 derivativeV =
                    (Evaluate(localU, vAfter, layer)
                        - Evaluate(localU, vBefore, layer))
                    / (vAfter - vBefore);
                const geometry::Vector3 offsetCross =
                    geometry::Cross(derivativeU, derivativeV);
                const double uLength = derivativeU.Length();
                const double vLength = derivativeV.Length();
                if (!center.IsFinite() || !derivativeU.IsFinite()
                    || !derivativeV.IsFinite() || !offsetCross.IsFinite()) {
                    throw std::invalid_argument(
                        "Plate offset produced non-finite coordinates.");
                }
                if (uLength <= kMinimumLength || vLength <= kMinimumLength
                    || offsetCross.Length()
                        <= uLength * vLength * kMinimumSine
                    || geometry::Dot(sourceCross, offsetCross) <= 0.0) {
                    throw std::invalid_argument(
                        "Plate thickness causes the offset surface to fold or collapse.");
                }
            }
        }
    }

    // Include all outer edges in validation. Point-converged edges are allowed
    // when a limiting normal exists, but no invalid coordinate may reach Qt.
    for (int index = 0; index <= kSamples; ++index) {
        const double parameter = static_cast<double>(index) / kSamples;
        for (const auto& uv : std::array<std::pair<double, double>, 4>{
                 std::pair{parameter, 0.0},
                 std::pair{parameter, 1.0},
                 std::pair{0.0, parameter},
                 std::pair{1.0, parameter},
             }) {
            if (!Evaluate(uv.first, uv.second, 0.0).IsFinite()
                || !Evaluate(uv.first, uv.second, 1.0).IsFinite()) {
                throw std::invalid_argument(
                    "Plate boundary produced non-finite coordinates.");
            }
        }
    }
}

double Plate::Thickness(double localV) const noexcept
{
    return thickness_ + (endThickness_ - thickness_) * std::clamp(localV, 0.0, 1.0);
}

bool Plate::HasVariableThickness(double tolerance) const noexcept
{
    return std::abs(endThickness_ - thickness_) > tolerance;
}

double Plate::MinimumOffset() const noexcept
{
    return MinimumOffset(0.0);
}

double Plate::MinimumOffset(double localV) const noexcept
{
    const double localThickness = Thickness(localV);
    if (direction_ == PlateThicknessDirection::Negative) {
        return -localThickness;
    }
    if (direction_ == PlateThicknessDirection::Centered) {
        return -localThickness * 0.5;
    }
    return 0.0;
}

double Plate::MaximumOffset() const noexcept
{
    return MaximumOffset(0.0);
}

double Plate::MaximumOffset(double localV) const noexcept
{
    const double localThickness = Thickness(localV);
    if (direction_ == PlateThicknessDirection::Positive) {
        return localThickness;
    }
    if (direction_ == PlateThicknessDirection::Centered) {
        return localThickness * 0.5;
    }
    return 0.0;
}

double Plate::SourceU(double localU) const noexcept
{
    return range_.minimumU
        + (range_.maximumU - range_.minimumU) * std::clamp(localU, 0.0, 1.0);
}

double Plate::SourceV(double localV) const noexcept
{
    return range_.minimumV
        + (range_.maximumV - range_.minimumV) * std::clamp(localV, 0.0, 1.0);
}

geometry::Vector3 Plate::Evaluate(double u, double v, double throughThickness) const
{
    if (!std::isfinite(throughThickness)) {
        throw std::invalid_argument("Plate thickness parameter must be finite.");
    }
    const double offset = MinimumOffset(v)
        + (MaximumOffset(v) - MinimumOffset(v)) * std::clamp(throughThickness, 0.0, 1.0);
    const double sourceU = SourceU(u);
    const double sourceV = SourceV(v);
    return sourceSurface_.Evaluate(sourceU, sourceV) + sourceSurface_.Normal(sourceU, sourceV) * offset;
}

std::pair<Plate, Plate> Plate::Split(PlateSplitAxis axis, double parameter) const
{
    if (!std::isfinite(parameter) || parameter <= 1.0e-4 || parameter >= 1.0 - 1.0e-4) {
        throw std::invalid_argument("Plate split position must be between the two edges.");
    }
    PlateSurfaceRange firstRange = range_;
    PlateSurfaceRange secondRange = range_;
    if (axis == PlateSplitAxis::U) {
        const double splitU = SourceU(parameter);
        firstRange.maximumU = splitU;
        secondRange.minimumU = splitU;
    } else if (axis == PlateSplitAxis::V) {
        const double splitV = SourceV(parameter);
        firstRange.maximumV = splitV;
        secondRange.minimumV = splitV;
    } else {
        throw std::invalid_argument("Plate split axis is invalid.");
    }
    if (axis == PlateSplitAxis::V) {
        const double middleThickness = Thickness(parameter);
        return {
            Plate(sourceSurface_, thickness_, middleThickness, direction_, firstRange),
            Plate(sourceSurface_, middleThickness, endThickness_, direction_, secondRange),
        };
    }
    return {
        Plate(sourceSurface_, thickness_, endThickness_, direction_, firstRange),
        Plate(sourceSurface_, thickness_, endThickness_, direction_, secondRange),
    };
}

PlateDevelopabilityAnalysis Plate::AnalyzeDevelopability(int uSamples, int vSamples) const
{
    if (uSamples < 2 || vSamples < 2) {
        throw std::invalid_argument("Developability analysis requires at least two samples per direction.");
    }
    if (sourceSurface_.Kind() == SurfaceKind::Planar) {
        return {PlateDevelopability::Planar, 0.0, 0.0};
    }

    constexpr double planarAngleTolerance = 8.726646259971648e-4; // 0.05 degrees
    constexpr double gaussianCurvatureTolerance = 1.0e-6;
    PlateDevelopabilityAnalysis analysis;
    std::optional<geometry::Vector3> referenceNormal;
    int validSamples = 0;
    const double stepU = std::min(1.0e-3, (range_.maximumU - range_.minimumU) / (uSamples * 4.0));
    const double stepV = std::min(1.0e-3, (range_.maximumV - range_.minimumV) / (vSamples * 4.0));

    for (int uIndex = 0; uIndex < uSamples; ++uIndex) {
        const double u = SourceU((static_cast<double>(uIndex) + 0.37) / static_cast<double>(uSamples));
        for (int vIndex = 0; vIndex < vSamples; ++vIndex) {
            const double v = SourceV((static_cast<double>(vIndex) + 0.37) / static_cast<double>(vSamples));
            const double uBefore = std::max(range_.minimumU, u - stepU);
            const double uAfter = std::min(range_.maximumU, u + stepU);
            const double vBefore = std::max(range_.minimumV, v - stepV);
            const double vAfter = std::min(range_.maximumV, v + stepV);
            const double uSpan = uAfter - uBefore;
            const double vSpan = vAfter - vBefore;

            const geometry::Vector3 point = sourceSurface_.Evaluate(u, v);
            const geometry::Vector3 pointUBefore = sourceSurface_.Evaluate(uBefore, v);
            const geometry::Vector3 pointUAfter = sourceSurface_.Evaluate(uAfter, v);
            const geometry::Vector3 pointVBefore = sourceSurface_.Evaluate(u, vBefore);
            const geometry::Vector3 pointVAfter = sourceSurface_.Evaluate(u, vAfter);
            const geometry::Vector3 derivativeU = (pointUAfter - pointUBefore) / uSpan;
            const geometry::Vector3 derivativeV = (pointVAfter - pointVBefore) / vSpan;
            const geometry::Vector3 normalVector = geometry::Cross(derivativeU, derivativeV);
            if (normalVector.LengthSquared() <= 1.0e-18) {
                continue;
            }
            const geometry::Vector3 normal = normalVector.Normalized();
            if (!referenceNormal.has_value()) {
                referenceNormal = normal;
            }
            const double normalDot = std::clamp(geometry::Dot(*referenceNormal, normal), -1.0, 1.0);
            analysis.maximumNormalAngleRadians = std::max(
                analysis.maximumNormalAngleRadians,
                std::acos(normalDot));

            const geometry::Vector3 derivativeUU =
                (pointUAfter - point * 2.0 + pointUBefore) / ((uSpan * 0.5) * (uSpan * 0.5));
            const geometry::Vector3 derivativeVV =
                (pointVAfter - point * 2.0 + pointVBefore) / ((vSpan * 0.5) * (vSpan * 0.5));
            const geometry::Vector3 derivativeUV = (
                sourceSurface_.Evaluate(uAfter, vAfter)
                - sourceSurface_.Evaluate(uAfter, vBefore)
                - sourceSurface_.Evaluate(uBefore, vAfter)
                + sourceSurface_.Evaluate(uBefore, vBefore)) / (uSpan * vSpan);

            const double firstE = geometry::Dot(derivativeU, derivativeU);
            const double firstF = geometry::Dot(derivativeU, derivativeV);
            const double firstG = geometry::Dot(derivativeV, derivativeV);
            const double denominator = firstE * firstG - firstF * firstF;
            if (denominator <= 1.0e-18) {
                continue;
            }
            const double secondL = geometry::Dot(derivativeUU, normal);
            const double secondM = geometry::Dot(derivativeUV, normal);
            const double secondN = geometry::Dot(derivativeVV, normal);
            const double gaussianCurvature = (secondL * secondN - secondM * secondM) / denominator;
            analysis.maximumAbsoluteGaussianCurvature = std::max(
                analysis.maximumAbsoluteGaussianCurvature,
                std::abs(gaussianCurvature));
            ++validSamples;
        }
    }

    if (validSamples == 0) {
        throw std::invalid_argument("Plate surface is degenerate and cannot be analyzed.");
    }
    analysis.classification = analysis.maximumNormalAngleRadians <= planarAngleTolerance
        ? PlateDevelopability::Planar
        : analysis.maximumAbsoluteGaussianCurvature <= gaussianCurvatureTolerance
        ? PlateDevelopability::Developable
        : PlateDevelopability::DoubleCurved;
    return analysis;
}

} // namespace kachakacha::model
