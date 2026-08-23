#pragma once

#include "kachakacha/model/Plate.h"

namespace kachakacha::model {

enum class BodyKind {
    SurfaceJig,
};

enum class JigSide {
    Positive,
    Negative,
};

struct BodyPrintabilityAnalysis {
    bool meetsMinimumWall = false;
    double minimumWallMillimeters = 0.0;
    geometry::Vector3 minimumBounds;
    geometry::Vector3 maximumBounds;
};

class Body {
public:
    [[nodiscard]] static Body SurfaceJig(
        Surface sourceSurface,
        PlateSurfaceRange range,
        JigSide side,
        double clearanceMillimeters,
        double thicknessMillimeters);

    [[nodiscard]] BodyKind Kind() const noexcept { return kind_; }
    [[nodiscard]] const Surface& SourceSurface() const noexcept { return sourceSurface_; }
    [[nodiscard]] const PlateSurfaceRange& Range() const noexcept { return range_; }
    [[nodiscard]] JigSide Side() const noexcept { return side_; }
    [[nodiscard]] double ClearanceMillimeters() const noexcept { return clearanceMillimeters_; }
    [[nodiscard]] double ThicknessMillimeters() const noexcept { return thicknessMillimeters_; }
    [[nodiscard]] double ContactOffset() const noexcept;
    [[nodiscard]] double BackingOffset() const noexcept;
    [[nodiscard]] double SourceU(double localU) const noexcept;
    [[nodiscard]] double SourceV(double localV) const noexcept;
    [[nodiscard]] geometry::Vector3 Evaluate(double u, double v, double throughThickness) const;
    [[nodiscard]] BodyPrintabilityAnalysis AnalyzePrintability(
        double requiredMinimumWallMillimeters,
        int uSamples = 16,
        int vSamples = 16) const;

private:
    Body(
        BodyKind kind,
        Surface sourceSurface,
        PlateSurfaceRange range,
        JigSide side,
        double clearanceMillimeters,
        double thicknessMillimeters);

    BodyKind kind_ = BodyKind::SurfaceJig;
    Surface sourceSurface_;
    PlateSurfaceRange range_;
    JigSide side_ = JigSide::Positive;
    double clearanceMillimeters_ = 0.0;
    double thicknessMillimeters_ = 0.0;
};

} // namespace kachakacha::model
