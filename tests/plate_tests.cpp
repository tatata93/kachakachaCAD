#include "kachakacha/model/Plate.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using kachakacha::geometry::AlmostEqual;
using kachakacha::model::Plate;
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
    } catch (const std::exception& error) {
        std::cerr << "plate_tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "plate_tests passed\n";
    return EXIT_SUCCESS;
}
