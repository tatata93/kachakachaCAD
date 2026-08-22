#include "kachakacha/model/Plate.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace kachakacha::model {

Plate::Plate(Surface sourceSurface, double thickness, PlateThicknessDirection direction)
    : sourceSurface_(std::move(sourceSurface)), thickness_(thickness), direction_(direction)
{
    if (!std::isfinite(thickness_) || thickness_ <= 0.0) {
        throw std::invalid_argument("Plate thickness must be a positive finite value.");
    }
    switch (direction_) {
    case PlateThicknessDirection::Positive:
    case PlateThicknessDirection::Centered:
    case PlateThicknessDirection::Negative:
        break;
    default:
        throw std::invalid_argument("Plate thickness direction is invalid.");
    }
}

double Plate::MinimumOffset() const noexcept
{
    if (direction_ == PlateThicknessDirection::Negative) {
        return -thickness_;
    }
    if (direction_ == PlateThicknessDirection::Centered) {
        return -thickness_ * 0.5;
    }
    return 0.0;
}

double Plate::MaximumOffset() const noexcept
{
    if (direction_ == PlateThicknessDirection::Positive) {
        return thickness_;
    }
    if (direction_ == PlateThicknessDirection::Centered) {
        return thickness_ * 0.5;
    }
    return 0.0;
}

geometry::Vector3 Plate::Evaluate(double u, double v, double throughThickness) const
{
    if (!std::isfinite(throughThickness)) {
        throw std::invalid_argument("Plate thickness parameter must be finite.");
    }
    const double offset = MinimumOffset()
        + (MaximumOffset() - MinimumOffset()) * std::clamp(throughThickness, 0.0, 1.0);
    return sourceSurface_.Evaluate(u, v) + sourceSurface_.Normal(u, v) * offset;
}

PlateDevelopabilityAnalysis Plate::AnalyzeDevelopability(int uSamples, int vSamples) const
{
    if (uSamples < 2 || vSamples < 2) {
        throw std::invalid_argument("Developability analysis requires at least two samples per direction.");
    }
    if (sourceSurface_.Kind() == SurfaceKind::Planar) {
        return {PlateDevelopability::Planar, 0.0, 0.0};
    }

    constexpr double step = 1.0e-3;
    constexpr double planarAngleTolerance = 8.726646259971648e-4; // 0.05 degrees
    constexpr double gaussianCurvatureTolerance = 1.0e-6;
    PlateDevelopabilityAnalysis analysis;
    std::optional<geometry::Vector3> referenceNormal;
    int validSamples = 0;

    for (int uIndex = 0; uIndex < uSamples; ++uIndex) {
        const double u = (static_cast<double>(uIndex) + 0.37) / static_cast<double>(uSamples);
        for (int vIndex = 0; vIndex < vSamples; ++vIndex) {
            const double v = (static_cast<double>(vIndex) + 0.37) / static_cast<double>(vSamples);
            const double uBefore = std::max(0.0, u - step);
            const double uAfter = std::min(1.0, u + step);
            const double vBefore = std::max(0.0, v - step);
            const double vAfter = std::min(1.0, v + step);
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
