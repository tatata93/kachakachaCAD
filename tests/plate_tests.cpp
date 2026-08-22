#include "kachakacha/model/Plate.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using kachakacha::geometry::AlmostEqual;
using kachakacha::model::Plate;
using kachakacha::model::PlateDevelopability;
using kachakacha::model::PlateSplitAxis;
using kachakacha::model::PlateSurfaceRange;
using kachakacha::model::PlateThicknessDirection;
using kachakacha::model::Surface;
using kachakacha::model::Wire;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main()
{
    try {
        const Surface panel = Surface::Planar(Wire::Polyline({
            {0.0, 0.0, 0.0},
            {20.0, 0.0, 0.0},
            {20.0, 10.0, 0.0},
            {0.0, 10.0, 0.0},
            {0.0, 0.0, 0.0},
        }));
        const Plate outward(panel, 1.2, PlateThicknessDirection::Positive);
        Require(std::abs(outward.Thickness() - 1.2) <= 1.0e-12, "plate thickness");
        Require(AlmostEqual(outward.Evaluate(0.5, 0.5, 0.0), panel.Evaluate(0.5, 0.5), 1.0e-9), "positive plate starts at source surface");
        Require(std::abs((outward.Evaluate(0.5, 0.5, 1.0) - outward.Evaluate(0.5, 0.5, 0.0)).Length() - 1.2) <= 1.0e-8,
            "positive plate adds exact thickness");

        const Plate centered(panel, 0.5, PlateThicknessDirection::Centered);
        Require(std::abs(centered.MinimumOffset() + 0.25) <= 1.0e-12, "centered minimum offset");
        Require(std::abs(centered.MaximumOffset() - 0.25) <= 1.0e-12, "centered maximum offset");

        const Plate inward(panel, 0.8, PlateThicknessDirection::Negative);
        Require(std::abs(inward.MinimumOffset() + 0.8) <= 1.0e-12, "negative minimum offset");
        Require(std::abs(inward.MaximumOffset()) <= 1.0e-12, "negative maximum offset");

        bool invalidDirectionRejected = false;
        try {
            static_cast<void>(Plate(panel, 0.8, static_cast<PlateThicknessDirection>(99)));
        } catch (const std::invalid_argument&) {
            invalidDirectionRejected = true;
        }
        Require(invalidDirectionRejected, "invalid plate direction is rejected");

        const auto planarAnalysis = centered.AnalyzeDevelopability();
        Require(planarAnalysis.classification == PlateDevelopability::Planar, "planar sheet is classified as planar");

        const Surface cylinder = Surface::Ruled(
            Wire::Circle({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, 8.0),
            Wire::Circle({20.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, 8.0));
        const auto cylinderAnalysis = Plate(cylinder, 0.5, PlateThicknessDirection::Centered).AnalyzeDevelopability();
        Require(cylinderAnalysis.classification == PlateDevelopability::Developable, "cylindrical sheet is developable");

        const Plate cylinderPlate(cylinder, 0.5, PlateThicknessDirection::Centered);
        const auto [cylinderTop, cylinderBottom] = cylinderPlate.Split(PlateSplitAxis::U, 0.25);
        Require(std::abs(cylinderTop.Range().maximumU - 0.25) <= 1.0e-12, "U split first range");
        Require(std::abs(cylinderBottom.Range().minimumU - 0.25) <= 1.0e-12, "U split second range");
        Require(AlmostEqual(cylinderTop.Evaluate(1.0, 0.4, 0.5), cylinderPlate.Evaluate(0.25, 0.4, 0.5), 1.0e-9),
            "U split preserves shared edge");
        Require(AlmostEqual(cylinderBottom.Evaluate(0.0, 0.4, 0.5), cylinderTop.Evaluate(1.0, 0.4, 0.5), 1.0e-9),
            "U split pieces meet exactly");

        const auto [cylinderFront, cylinderRear] = cylinderBottom.Split(PlateSplitAxis::V, 0.4);
        Require(std::abs(cylinderFront.Range().maximumV - 0.4) <= 1.0e-12, "V split first range");
        Require(std::abs(cylinderRear.Range().minimumV - 0.4) <= 1.0e-12, "V split second range");
        Require(AlmostEqual(cylinderFront.Evaluate(0.3, 1.0, 0.5), cylinderRear.Evaluate(0.3, 0.0, 0.5), 1.0e-9),
            "V split pieces meet exactly");

        bool invalidRangeRejected = false;
        try {
            static_cast<void>(Plate(cylinder, 0.5, PlateThicknessDirection::Centered,
                PlateSurfaceRange{0.7, 0.2, 0.0, 1.0}));
        } catch (const std::invalid_argument&) {
            invalidRangeRejected = true;
        }
        Require(invalidRangeRejected, "invalid plate range is rejected");

        const Surface saddle = Surface::Ruled(
            Wire::Line({0.0, -5.0, 0.0}, {0.0, 5.0, 0.0}),
            Wire::Line({10.0, -5.0, -4.0}, {10.0, 5.0, 4.0}));
        const auto saddleAnalysis = Plate(saddle, 0.5, PlateThicknessDirection::Centered).AnalyzeDevelopability();
        Require(saddleAnalysis.classification == PlateDevelopability::DoubleCurved, "twisted ruled sheet is double curved");
    } catch (const std::exception& error) {
        std::cerr << "plate_tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "plate_tests passed\n";
    return EXIT_SUCCESS;
}
