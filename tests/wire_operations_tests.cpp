#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/model/WireOperations.h"

#include <iostream>
#include <stdexcept>

using kachakacha::geometry::AlmostEqual;
using kachakacha::geometry::Vector3;
using kachakacha::model::ChamferIntersectingLines;
using kachakacha::model::FilletIntersectingLines;
using kachakacha::model::RetainedLineEnd;
using kachakacha::model::Wire;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void RequireNear(Vector3 actual, Vector3 expected, const char* message)
{
    if (!AlmostEqual(actual, expected, 1.0e-8)) {
        throw std::runtime_error(message);
    }
}

void ChamfersSharedCornerAutomatically()
{
    const Wire horizontal = Wire::Line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    const Wire vertical = Wire::Line({0.0, 0.0, 0.0}, {0.0, 10.0, 0.0});

    const auto result = ChamferIntersectingLines(
        horizontal, RetainedLineEnd::Automatic, 2.0,
        vertical, RetainedLineEnd::Automatic, 3.0);

    RequireNear(result.intersection, {0.0, 0.0, 0.0}, "shared intersection");
    RequireNear(result.trimmedFirst.Start(), {2.0, 0.0, 0.0}, "horizontal trim point");
    RequireNear(result.trimmedFirst.End(), {10.0, 0.0, 0.0}, "horizontal retained end");
    RequireNear(result.trimmedSecond.Start(), {0.0, 3.0, 0.0}, "vertical trim point");
    RequireNear(result.chamfer.Start(), {2.0, 0.0, 0.0}, "chamfer start");
    RequireNear(result.chamfer.End(), {0.0, 3.0, 0.0}, "chamfer end");
}

void ChamfersChosenBranchesAtCrossing()
{
    const Wire horizontal = Wire::Line({-10.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    const Wire vertical = Wire::Line({0.0, -10.0, 0.0}, {0.0, 10.0, 0.0});

    const auto result = ChamferIntersectingLines(
        horizontal, RetainedLineEnd::Start, 2.0,
        vertical, RetainedLineEnd::End, 4.0);

    RequireNear(result.trimmedFirst.End(), {-2.0, 0.0, 0.0}, "chosen first branch");
    RequireNear(result.trimmedSecond.Start(), {0.0, 4.0, 0.0}, "chosen second branch");
}

void RejectsInvalidChamfers()
{
    bool parallelRejected = false;
    try {
        (void)ChamferIntersectingLines(
            Wire::Line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}), RetainedLineEnd::Automatic, 1.0,
            Wire::Line({0.0, 1.0, 0.0}, {10.0, 1.0, 0.0}), RetainedLineEnd::Automatic, 1.0);
    } catch (const std::invalid_argument&) {
        parallelRejected = true;
    }
    Require(parallelRejected, "parallel lines rejected");

    bool setbackRejected = false;
    try {
        (void)ChamferIntersectingLines(
            Wire::Line({0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}), RetainedLineEnd::Automatic, 3.0,
            Wire::Line({0.0, 0.0, 0.0}, {0.0, 2.0, 0.0}), RetainedLineEnd::Automatic, 1.0);
    } catch (const std::invalid_argument&) {
        setbackRejected = true;
    }
    Require(setbackRejected, "oversized setback rejected");
}

void FilletsSharedCornerWithTangentArc()
{
    const Wire horizontal = Wire::Line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    const Wire vertical = Wire::Line({0.0, 0.0, 0.0}, {0.0, 10.0, 0.0});

    const auto result = FilletIntersectingLines(
        horizontal, RetainedLineEnd::Automatic,
        vertical, RetainedLineEnd::Automatic,
        2.0);

    RequireNear(result.center, {2.0, 2.0, 0.0}, "fillet center");
    RequireNear(result.firstTangentPoint, {2.0, 0.0, 0.0}, "first tangent point");
    RequireNear(result.secondTangentPoint, {0.0, 2.0, 0.0}, "second tangent point");
    RequireNear(result.fillet.Start(), result.firstTangentPoint, "fillet start");
    RequireNear(result.fillet.End(), result.secondTangentPoint, "fillet end");
    Require(result.fillet.Kind() == kachakacha::model::WireKind::CircularArc, "fillet is an arc");
}

void FilletsCornerInArbitrary3DPlane()
{
    const Wire first = Wire::Line({1.0, 2.0, 3.0}, {11.0, 2.0, 3.0});
    const Wire second = Wire::Line({1.0, 2.0, 3.0}, {1.0, 2.0, 13.0});

    const auto result = FilletIntersectingLines(
        first, RetainedLineEnd::Automatic,
        second, RetainedLineEnd::Automatic,
        1.5);

    RequireNear(result.center, {2.5, 2.0, 4.5}, "3d plane fillet center");
    RequireNear(result.firstTangentPoint, {2.5, 2.0, 3.0}, "3d first tangent");
    RequireNear(result.secondTangentPoint, {1.0, 2.0, 4.5}, "3d second tangent");
}

} // namespace

int main()
{
    try {
        ChamfersSharedCornerAutomatically();
        ChamfersChosenBranchesAtCrossing();
        RejectsInvalidChamfers();
        FilletsSharedCornerWithTangentArc();
        FilletsCornerInArbitrary3DPlane();
    } catch (const std::exception& error) {
        std::cerr << "wire_operations_tests failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "wire_operations_tests passed\n";
    return 0;
}
