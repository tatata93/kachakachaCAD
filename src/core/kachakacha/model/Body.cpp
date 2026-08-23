#include "kachakacha/model/Body.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace kachakacha::model {

namespace {

void ValidateRange(const PlateSurfaceRange& range)
{
    if (!std::isfinite(range.minimumU) || !std::isfinite(range.maximumU)
        || !std::isfinite(range.minimumV) || !std::isfinite(range.maximumV)
        || range.minimumU < 0.0 || range.maximumU > 1.0
        || range.minimumV < 0.0 || range.maximumV > 1.0
        || range.maximumU - range.minimumU <= 1.0e-9
        || range.maximumV - range.minimumV <= 1.0e-9) {
        throw std::invalid_argument("Body surface range must be a non-empty part of the source surface.");
    }
}

} // namespace

Body::Body(
    BodyKind kind,
    Surface sourceSurface,
    PlateSurfaceRange range,
    JigSide side,
    double clearanceMillimeters,
    double thicknessMillimeters)
    : kind_(kind),
      sourceSurface_(std::move(sourceSurface)),
      range_(range),
      side_(side),
      clearanceMillimeters_(clearanceMillimeters),
      thicknessMillimeters_(thicknessMillimeters)
{
    ValidateRange(range_);
    if (!std::isfinite(clearanceMillimeters_) || clearanceMillimeters_ < 0.0) {
        throw std::invalid_argument("Jig clearance must be a non-negative finite value.");
    }
    if (!std::isfinite(thicknessMillimeters_) || thicknessMillimeters_ <= 0.0) {
        throw std::invalid_argument("Jig thickness must be a positive finite value.");
    }
    switch (side_) {
    case JigSide::Positive:
    case JigSide::Negative:
        break;
    default:
        throw std::invalid_argument("Jig side is invalid.");
    }
}

Body Body::SurfaceJig(
    Surface sourceSurface,
    PlateSurfaceRange range,
    JigSide side,
    double clearanceMillimeters,
    double thicknessMillimeters)
{
    return Body(
        BodyKind::SurfaceJig,
        std::move(sourceSurface),
        range,
        side,
        clearanceMillimeters,
        thicknessMillimeters);
}

double Body::ContactOffset() const noexcept
{
    return side_ == JigSide::Positive ? clearanceMillimeters_ : -clearanceMillimeters_;
}

double Body::BackingOffset() const noexcept
{
    const double distance = clearanceMillimeters_ + thicknessMillimeters_;
    return side_ == JigSide::Positive ? distance : -distance;
}

double Body::SourceU(double localU) const noexcept
{
    return range_.minimumU
        + (range_.maximumU - range_.minimumU) * std::clamp(localU, 0.0, 1.0);
}

double Body::SourceV(double localV) const noexcept
{
    return range_.minimumV
        + (range_.maximumV - range_.minimumV) * std::clamp(localV, 0.0, 1.0);
}

geometry::Vector3 Body::Evaluate(double u, double v, double throughThickness) const
{
    if (!std::isfinite(u) || !std::isfinite(v) || !std::isfinite(throughThickness)) {
        throw std::invalid_argument("Body parameters must be finite.");
    }
    const double sourceU = SourceU(u);
    const double sourceV = SourceV(v);
    const double offset = ContactOffset()
        + (BackingOffset() - ContactOffset()) * std::clamp(throughThickness, 0.0, 1.0);
    return sourceSurface_.Evaluate(sourceU, sourceV)
        + sourceSurface_.Normal(sourceU, sourceV) * offset;
}

BodyPrintabilityAnalysis Body::AnalyzePrintability(
    double requiredMinimumWallMillimeters,
    int uSamples,
    int vSamples) const
{
    if (!std::isfinite(requiredMinimumWallMillimeters)
        || requiredMinimumWallMillimeters <= 0.0) {
        throw std::invalid_argument("Required minimum wall must be positive.");
    }
    if (uSamples < 2 || vSamples < 2) {
        throw std::invalid_argument("Body analysis requires at least two samples per direction.");
    }

    BodyPrintabilityAnalysis analysis;
    analysis.minimumWallMillimeters = thicknessMillimeters_;
    analysis.meetsMinimumWall = thicknessMillimeters_ + 1.0e-9 >= requiredMinimumWallMillimeters;
    bool firstPoint = true;
    for (int uIndex = 0; uIndex <= uSamples; ++uIndex) {
        const double u = static_cast<double>(uIndex) / static_cast<double>(uSamples);
        for (int vIndex = 0; vIndex <= vSamples; ++vIndex) {
            const double v = static_cast<double>(vIndex) / static_cast<double>(vSamples);
            for (double through : {0.0, 1.0}) {
                const geometry::Vector3 point = Evaluate(u, v, through);
                if (firstPoint) {
                    analysis.minimumBounds = point;
                    analysis.maximumBounds = point;
                    firstPoint = false;
                } else {
                    analysis.minimumBounds.x = std::min(analysis.minimumBounds.x, point.x);
                    analysis.minimumBounds.y = std::min(analysis.minimumBounds.y, point.y);
                    analysis.minimumBounds.z = std::min(analysis.minimumBounds.z, point.z);
                    analysis.maximumBounds.x = std::max(analysis.maximumBounds.x, point.x);
                    analysis.maximumBounds.y = std::max(analysis.maximumBounds.y, point.y);
                    analysis.maximumBounds.z = std::max(analysis.maximumBounds.z, point.z);
                }
            }
        }
    }
    return analysis;
}

} // namespace kachakacha::model
