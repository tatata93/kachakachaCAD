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

        const Plate variablePlate(
            cylinder, 0.4, 1.2, PlateThicknessDirection::Positive);
        Require(variablePlate.HasVariableThickness(), "variable plate reports thickness profile");
        Require(std::abs(variablePlate.Thickness(0.0) - 0.4) <= 1.0e-12,
            "variable plate start thickness");
        Require(std::abs(variablePlate.Thickness(0.5) - 0.8) <= 1.0e-12,
            "variable plate interpolated thickness");
        Require(std::abs(variablePlate.Thickness(1.0) - 1.2) <= 1.0e-12,
            "variable plate end thickness");
        const auto [variableFront, variableRear] = variablePlate.Split(PlateSplitAxis::V, 0.25);
        Require(std::abs(variableFront.EndThickness() - 0.6) <= 1.0e-12,
            "variable thickness first split end");
        Require(std::abs(variableRear.Thickness() - 0.6) <= 1.0e-12
                && std::abs(variableRear.EndThickness() - 1.2) <= 1.0e-12,
            "variable thickness second split profile");

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

        const Wire joinedEndFirstGuide = Wire::CubicBezier(
            {0.0, 0.0, 0.0}, {10.0 / 3.0, 5.0, 0.0},
            {20.0 / 3.0, 5.0, 0.0}, {10.0, 0.0, 0.0});
        const Wire joinedEndSecondGuide = Wire::Line(
            {0.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
        const Surface joinedEndGuided = Surface::GuidedLoft(
            joinedEndFirstGuide,
            joinedEndSecondGuide,
            {
                Wire::Line(joinedEndFirstGuide.Evaluate(0.3), joinedEndSecondGuide.Evaluate(0.3)),
                Wire::Line(joinedEndFirstGuide.Evaluate(0.7), joinedEndSecondGuide.Evaluate(0.7)),
            });
        const Plate joinedEndPlate(
            joinedEndGuided, 0.4, PlateThicknessDirection::Centered);
        for (double u : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            for (double v : {0.0, 1.0}) {
                const auto front = joinedEndPlate.Evaluate(u, v, 0.0);
                const auto back = joinedEndPlate.Evaluate(u, v, 1.0);
                Require(front.IsFinite() && back.IsFinite(),
                    "point-converged plate edge remains finite");
                Require(std::abs((back - front).Length() - 0.4) <= 1.0e-7,
                    "point-converged plate keeps requested thickness");
            }
        }

        bool excessiveInsideThicknessRejected = false;
        try {
            static_cast<void>(Plate(
                cylinder, 9.0, PlateThicknessDirection::Negative));
        } catch (const std::invalid_argument&) {
            excessiveInsideThicknessRejected = true;
        }
        Require(excessiveInsideThicknessRejected,
            "plate rejects thickness that folds an inward cylindrical offset");
    } catch (const std::exception& error) {
        std::cerr << "plate_tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "plate_tests passed\n";
    return EXIT_SUCCESS;
}
