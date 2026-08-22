#include "kachakacha/model/Surface.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using kachakacha::geometry::AlmostEqual;
using kachakacha::model::Surface;
using kachakacha::model::SurfaceKind;
using kachakacha::model::Wire;
using kachakacha::model::WireKind;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main()
{
    const Wire rectangle = Wire::Polyline({
        {0.0, 0.0, 0.0},
        {20.0, 0.0, 0.0},
        {20.0, 10.0, 0.0},
        {0.0, 10.0, 0.0},
        {0.0, 0.0, 0.0},
    });
    const Surface planar = Surface::Planar(rectangle);
    Require(planar.Kind() == SurfaceKind::Planar, "create planar surface");
    const auto planarProjection = planar.ProjectPointAlongDirection({7.0, 4.0, 8.0}, {0.0, 0.0, -1.0});
    Require(AlmostEqual(planarProjection.point, {7.0, 4.0, 0.0}, 1.0e-8), "project point to bounded plane");
    bool outsideRejected = false;
    try {
        static_cast<void>(planar.ProjectPointAlongDirection({25.0, 4.0, 8.0}, {0.0, 0.0, -1.0}));
    } catch (const std::invalid_argument&) {
        outsideRejected = true;
    }
    Require(outsideRejected, "reject projection outside planar boundary");

    const Wire sectionA = Wire::CubicBezier(
        {0.0, -6.0, 0.0}, {0.0, -2.0, 3.0}, {0.0, 2.0, 3.0}, {0.0, 6.0, 0.0});
    const Wire sectionB = Wire::CubicBezier(
        {12.0, -6.0, 0.0}, {12.0, -2.0, 5.0}, {12.0, 2.0, 5.0}, {12.0, 6.0, 0.0});
    const Surface ruled = Surface::Ruled(sectionA, sectionB.Reversed());
    Require(ruled.Kind() == SurfaceKind::Ruled, "create ruled section surface");
    Require(AlmostEqual(ruled.Evaluate(0.0, 1.0), sectionB.Start(), 1.0e-8), "automatically orient second section");

    const Wire lightDrawing = Wire::Circle({6.0, 0.0, 12.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 1.25);
    const Wire projectedLight = ruled.ProjectWireAlongDirection(lightDrawing, {0.0, 0.0, -1.0});
    Require(projectedLight.Kind() == WireKind::Polyline && projectedLight.IsClosed(), "project closed light drawing to surface");
    for (const auto& point : projectedLight.ControlPoints()) {
        const auto check = ruled.ProjectPointAlongDirection({point.x, point.y, 12.0}, {0.0, 0.0, -1.0});
        Require(std::abs(check.point.z - point.z) <= 1.0e-5, "projected light points remain on surface");
    }

    std::cout << "surface tests passed\n";
    return EXIT_SUCCESS;
}
