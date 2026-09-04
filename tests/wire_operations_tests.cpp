#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/model/WireOperations.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

using kachakacha::geometry::AlmostEqual;
using kachakacha::geometry::Vector3;
using kachakacha::model::ApplyWireCurveConstraints;
using kachakacha::model::ChamferIntersectingLines;
using kachakacha::model::ApplyWireLineConstraints;
using kachakacha::model::FilletIntersectingLines;
using kachakacha::model::ExtendWireToBoundary;
using kachakacha::model::MeetLinesAtIntersection;
using kachakacha::model::OffsetPlanarWire;
using kachakacha::model::JoinLineChain;
using kachakacha::model::JoinWireChain;
using kachakacha::model::RetainedLineEnd;
using kachakacha::model::TrimWireAtBoundaries;
using kachakacha::model::Wire;
using kachakacha::model::WireCurveConstraints;
using kachakacha::model::WireKind;
using kachakacha::model::WireLineConstraints;
using kachakacha::model::WorkPlane;

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

void ChamfersAndFilletsSeparatedLinesWithAutoExtension()
{
    // 離れた2本(仮想交点 10,0,0)。allowExtension=true なら延長して面取り。
    const Wire horizontal = Wire::Line({0.0, 0.0, 0.0}, {6.0, 0.0, 0.0});
    const Wire vertical = Wire::Line({10.0, 4.0, 0.0}, {10.0, 12.0, 0.0});

    bool separatedRejected = false;
    try {
        (void)ChamferIntersectingLines(
            horizontal, RetainedLineEnd::Automatic, 2.0,
            vertical, RetainedLineEnd::Automatic, 2.0);
    } catch (const std::invalid_argument&) {
        separatedRejected = true;
    }
    Require(separatedRejected, "separated lines rejected without extension");

    const auto chamfered = ChamferIntersectingLines(
        horizontal, RetainedLineEnd::Automatic, 2.0,
        vertical, RetainedLineEnd::Automatic, 3.0,
        1.0e-8, true);
    RequireNear(chamfered.intersection, {10.0, 0.0, 0.0}, "virtual intersection");
    RequireNear(chamfered.trimmedFirst.Start(), {0.0, 0.0, 0.0}, "extended line keeps its far end");
    RequireNear(chamfered.trimmedFirst.End(), {8.0, 0.0, 0.0}, "line extends to the setback");
    RequireNear(chamfered.trimmedSecond.Start(), {10.0, 3.0, 0.0}, "second line extends to the setback");
    RequireNear(chamfered.chamfer.Start(), {8.0, 0.0, 0.0}, "chamfer start on extension");
    RequireNear(chamfered.chamfer.End(), {10.0, 3.0, 0.0}, "chamfer end on extension");

    const auto filleted = FilletIntersectingLines(
        horizontal, RetainedLineEnd::Automatic,
        vertical, RetainedLineEnd::Automatic, 2.0, 1.0e-8, true);
    RequireNear(filleted.center, {8.0, 2.0, 0.0}, "fillet center from virtual corner");
    RequireNear(filleted.firstTangentPoint, {8.0, 0.0, 0.0}, "fillet tangent on extension");
    RequireNear(filleted.secondTangentPoint, {10.0, 2.0, 0.0}, "fillet tangent on second extension");
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

void ExtendsSeparatedLinesToTheirIntersection()
{
    const Wire horizontal = Wire::Line({0.0, 0.0, 0.0}, {4.0, 0.0, 0.0});
    const Wire vertical = Wire::Line({6.0, 2.0, 0.0}, {6.0, 5.0, 0.0});

    const auto result = MeetLinesAtIntersection(
        horizontal, RetainedLineEnd::Automatic,
        vertical, RetainedLineEnd::Automatic);

    RequireNear(result.intersection, {6.0, 0.0, 0.0}, "extended intersection");
    RequireNear(result.first.Start(), {0.0, 0.0, 0.0}, "extended first retained end");
    RequireNear(result.first.End(), {6.0, 0.0, 0.0}, "extended first intersection end");
    RequireNear(result.second.Start(), {6.0, 0.0, 0.0}, "extended second intersection start");
    RequireNear(result.second.End(), {6.0, 5.0, 0.0}, "extended second retained end");
}

void TrimsCrossingLinesToChosenBranches()
{
    const Wire horizontal = Wire::Line({-5.0, 0.0, 0.0}, {5.0, 0.0, 0.0});
    const Wire vertical = Wire::Line({0.0, -4.0, 0.0}, {0.0, 7.0, 0.0});

    const auto result = MeetLinesAtIntersection(
        horizontal, RetainedLineEnd::Start,
        vertical, RetainedLineEnd::End);

    RequireNear(result.first.Start(), {-5.0, 0.0, 0.0}, "trimmed first retained branch");
    RequireNear(result.first.End(), {0.0, 0.0, 0.0}, "trimmed first intersection");
    RequireNear(result.second.Start(), {0.0, 0.0, 0.0}, "trimmed second intersection");
    RequireNear(result.second.End(), {0.0, 7.0, 0.0}, "trimmed second retained branch");
}

void TrimsTheDirectlyPickedLinePortion()
{
    const Wire target = Wire::Line({0.0, 0.0, 0.0}, {12.0, 0.0, 0.0});
    const std::vector<Wire> boundaries = {
        Wire::Line({3.0, -2.0, 0.0}, {3.0, 2.0, 0.0}),
        Wire::Line({8.0, -2.0, 0.0}, {8.0, 2.0, 0.0}),
    };

    const auto middle = TrimWireAtBoundaries(target, 0.5, boundaries);
    RequireNear(middle.removed.Start(), {3.0, 0.0, 0.0}, "middle trim start");
    RequireNear(middle.removed.End(), {8.0, 0.0, 0.0}, "middle trim end");
    Require(middle.retained.size() == 2, "middle trim keeps two sides");
    RequireNear(middle.retained[0].Start(), {0.0, 0.0, 0.0}, "middle trim first start");
    RequireNear(middle.retained[0].End(), {3.0, 0.0, 0.0}, "middle trim first end");
    RequireNear(middle.retained[1].Start(), {8.0, 0.0, 0.0}, "middle trim second start");
    RequireNear(middle.retained[1].End(), {12.0, 0.0, 0.0}, "middle trim second end");

    const auto end = TrimWireAtBoundaries(target, 0.95, boundaries);
    Require(end.retained.size() == 1, "end trim keeps one side");
    RequireNear(end.removed.Start(), {8.0, 0.0, 0.0}, "end trim boundary");
    RequireNear(end.removed.End(), {12.0, 0.0, 0.0}, "end trim removed endpoint");
    RequireNear(end.retained.front().End(), {8.0, 0.0, 0.0}, "end trim retained endpoint");
}

void ExtendsTheDirectlyPickedLineEndToNearestBoundary()
{
    const Wire target = Wire::Line({2.0, 0.0, 0.0}, {6.0, 0.0, 0.0});
    const std::vector<Wire> boundaries = {
        Wire::Line({0.0, -3.0, 0.0}, {0.0, 3.0, 0.0}),
        Wire::Line({9.0, -3.0, 0.0}, {9.0, 3.0, 0.0}),
        Wire::Line({12.0, -3.0, 0.0}, {12.0, 3.0, 0.0}),
    };

    const auto start = ExtendWireToBoundary(target, 0.1, boundaries);
    Require(start.extendedEnd == RetainedLineEnd::Start, "start end selected");
    RequireNear(start.extended.Start(), {0.0, 0.0, 0.0}, "start extension intersection");
    RequireNear(start.extended.End(), {6.0, 0.0, 0.0}, "start extension retained end");
    RequireNear(start.added.End(), {2.0, 0.0, 0.0}, "start extension preview end");

    const auto end = ExtendWireToBoundary(target, 0.9, boundaries);
    Require(end.extendedEnd == RetainedLineEnd::End, "end selected");
    RequireNear(end.extended.Start(), {2.0, 0.0, 0.0}, "end extension retained start");
    RequireNear(end.extended.End(), {9.0, 0.0, 0.0}, "nearest end boundary");
    RequireNear(end.added.Start(), {6.0, 0.0, 0.0}, "end extension preview start");
}

void TrimsAndExtendsNativeCurvesWithoutFlattening()
{
    const Wire arc = Wire::CircularArc(
        {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0},
        5.0, 0.0, std::numbers::pi);
    const std::vector<Wire> arcBoundaries = {
        Wire::Line({3.0, -1.0, 0.0}, {3.0, 6.0, 0.0}),
        Wire::Line({-3.0, -1.0, 0.0}, {-3.0, 6.0, 0.0}),
    };
    const auto trimmedArc = TrimWireAtBoundaries(arc, 0.5, arcBoundaries);
    Require(trimmedArc.removed.Kind() == WireKind::CircularArc,
        "arc trim keeps a circular arc");
    Require(trimmedArc.retained.size() == 2,
        "arc middle trim keeps both native arc sides");
    Require(trimmedArc.retained[0].Kind() == WireKind::CircularArc
            && trimmedArc.retained[1].Kind() == WireKind::CircularArc,
        "retained arc pieces stay circular");

    const Wire bezier = Wire::CubicBezier(
        {0.0, 0.0, 0.0}, {3.0, 0.0, 0.0},
        {7.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    const std::vector<Wire> curveBoundaries = {
        Wire::Line({3.0, -2.0, 0.0}, {3.0, 2.0, 0.0}),
        Wire::Line({7.0, -2.0, 0.0}, {7.0, 2.0, 0.0}),
    };
    const auto trimmedBezier = TrimWireAtBoundaries(bezier, 0.5, curveBoundaries);
    Require(trimmedBezier.removed.Kind() == WireKind::CubicBezier,
        "Bezier trim keeps the exact curve type");
    Require(trimmedBezier.retained.size() == 2,
        "Bezier middle trim keeps two sides");
    RequireNear(trimmedBezier.retained[0].End(), trimmedBezier.removed.Start(),
        "Bezier first trim boundary");
    RequireNear(trimmedBezier.removed.End(), trimmedBezier.retained[1].Start(),
        "Bezier second trim boundary");

    const auto extendedBezier = ExtendWireToBoundary(
        bezier, 0.9, {Wire::Line({14.0, -2.0, 0.0}, {14.0, 2.0, 0.0})});
    Require(extendedBezier.extended.Kind() == WireKind::CubicBezier,
        "Bezier extension stays cubic");
    RequireNear(extendedBezier.extended.End(), {14.0, 0.0, 0.0},
        "Bezier reaches curve boundary");

    const Wire spline = Wire::CubicBSpline({
        {0.0, 2.0, 0.0}, {3.0, 2.0, 0.0},
        {7.0, 2.0, 0.0}, {10.0, 2.0, 0.0},
    });
    const auto trimmedSpline = TrimWireAtBoundaries(spline, 0.5, curveBoundaries);
    Require(trimmedSpline.removed.Kind() == WireKind::CubicBSpline,
        "B-spline trim keeps a B-spline");
    Require(trimmedSpline.retained.size() == 2,
        "B-spline middle trim keeps two sides");
    const auto extendedSpline = ExtendWireToBoundary(
        spline, 0.9, {Wire::Line({14.0, 0.0, 0.0}, {14.0, 4.0, 0.0})});
    Require(extendedSpline.extended.Kind() == WireKind::CubicBSpline,
        "B-spline extension stays a B-spline");
    RequireNear(extendedSpline.extended.End(), {14.0, 2.0, 0.0},
        "B-spline reaches boundary");
}

void JoinsUnorderedLineChain()
{
    const std::vector<Wire> wires = {
        Wire::Line({4.0, 2.0, 0.0}, {7.0, 2.0, 0.0}),
        Wire::Line({4.0, 0.0, 0.0}, {0.0, 0.0, 0.0}),
        Wire::Polyline({{4.0, 0.0, 0.0}, {4.0, 1.0, 0.0}, {4.0, 2.0, 0.0}}),
    };
    const Wire joined = JoinLineChain(wires);

    Require(joined.Kind() == kachakacha::model::WireKind::Polyline, "joined wire is polyline");
    Require(joined.ControlPoints().size() == 5, "joined control point count");
    const bool forward = AlmostEqual(joined.Start(), {0.0, 0.0, 0.0}, 1.0e-8)
        && AlmostEqual(joined.End(), {7.0, 2.0, 0.0}, 1.0e-8);
    const bool backward = AlmostEqual(joined.Start(), {7.0, 2.0, 0.0}, 1.0e-8)
        && AlmostEqual(joined.End(), {0.0, 0.0, 0.0}, 1.0e-8);
    Require(forward || backward, "joined chain endpoints");
}

void RejectsDisconnectedJoin()
{
    bool rejected = false;
    try {
        (void)JoinLineChain({
            Wire::Line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}),
            Wire::Line({2.0, 0.0, 0.0}, {3.0, 0.0, 0.0}),
        });
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Require(rejected, "disconnected join rejected");
}

void JoinsMixedCurvesIntoAClosedContour()
{
    const std::vector<Wire> wires = {
        Wire::Line({0.0, 8.0, 0.0}, {0.0, 0.0, 0.0}),
        Wire::CircularArcThroughThreePoints(
            {10.0, 8.0, 0.0}, {12.0, 4.0, 0.0}, {10.0, 0.0, 0.0}),
        Wire::Line({10.0, 8.0, 0.0}, {0.0, 8.0, 0.0}),
        Wire::Line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}),
    };
    const Wire joined = JoinWireChain(wires);

    Require(joined.IsClosed(1.0e-9), "mixed curve contour closes exactly");
    Require(joined.ControlPoints().size() > 8, "arc is adaptively sampled");
    Require(std::any_of(
                joined.ControlPoints().begin(), joined.ControlPoints().end(),
                [](Vector3 point) { return point.x > 10.5; }),
        "joined contour keeps the arc bulge");
}

void JoinsMixedCurvesAcrossSmallDrawingGaps()
{
    const std::vector<Wire> wires = {
        Wire::CubicBezier(
            {10.006, 5.0, 0.0}, {7.0, 6.5, 0.0},
            {3.0, 6.5, 0.0}, {0.0, 5.008, 0.0}),
        Wire::Line({0.004, 5.0, 0.0}, {0.0, 0.006, 0.0}),
        Wire::Line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}),
        Wire::CircularArcThroughThreePoints(
            {10.008, 0.0, 0.0}, {12.0, 2.5, 0.0}, {10.0, 4.994, 0.0}),
    };
    const Wire joined = JoinWireChain(wires);
    Require(joined.IsClosed(),
        "mixed curve contour heals sub-0.02 mm endpoint gaps");
    Require(joined.ControlPoints().size() > 12,
        "gap-healed chain preserves sampled curves");
}

void RejectsBranchedAndDuplicateChains()
{
    bool branchRejected = false;
    try {
        (void)JoinWireChain({
            Wire::Line({0.0, 0.0, 0.0}, {5.0, 0.0, 0.0}),
            Wire::Line({5.0, 0.0, 0.0}, {10.0, 0.0, 0.0}),
            Wire::Line({5.0, 0.0, 0.0}, {5.0, 5.0, 0.0}),
        });
    } catch (const std::invalid_argument&) {
        branchRejected = true;
    }
    Require(branchRejected, "branched chain is rejected explicitly");

    bool duplicateRejected = false;
    try {
        (void)JoinWireChain({
            Wire::Line({0.0, 0.0, 0.0}, {5.0, 0.0, 0.0}),
            Wire::Line({5.0, 0.0, 0.0}, {0.0, 0.0, 0.0}),
        });
    } catch (const std::invalid_argument&) {
        duplicateRejected = true;
    }
    Require(duplicateRejected, "duplicate reversed segment is rejected explicitly");

    const Wire lens = JoinWireChain({
        Wire::CircularArcThroughThreePoints(
            {-5.0, 0.0, 0.0}, {0.0, 3.0, 0.0}, {5.0, 0.0, 0.0}),
        Wire::CircularArcThroughThreePoints(
            {5.0, 0.0, 0.0}, {0.0, -3.0, 0.0}, {-5.0, 0.0, 0.0}),
    });
    Require(lens.IsClosed(),
        "two different curves sharing both endpoints form a valid closed loop");
}

void OffsetsPlanarDrawingWires()
{
    const WorkPlane plane = WorkPlane::FromPointNormal({}, {0.0, 0.0, 1.0});

    const Wire line = OffsetPlanarWire(
        Wire::Line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}), plane, 2.0);
    RequireNear(line.Start(), {0.0, 2.0, 0.0}, "line offset start");
    RequireNear(line.End(), {10.0, 2.0, 0.0}, "line offset end");

    const Wire corner = OffsetPlanarWire(
        Wire::Polyline({{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, {10.0, 10.0, 0.0}}),
        plane,
        1.0);
    RequireNear(corner.ControlPoints()[0], {0.0, 1.0, 0.0}, "open offset start");
    RequireNear(corner.ControlPoints()[1], {9.0, 1.0, 0.0}, "open offset miter");
    RequireNear(corner.ControlPoints()[2], {9.0, 10.0, 0.0}, "open offset end");

    const Wire outline = OffsetPlanarWire(
        Wire::Polyline({
            {0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, {10.0, 10.0, 0.0},
            {0.0, 10.0, 0.0}, {0.0, 0.0, 0.0},
        }),
        plane,
        1.0);
    RequireNear(outline.ControlPoints()[0], {1.0, 1.0, 0.0}, "closed offset first corner");
    RequireNear(outline.ControlPoints()[2], {9.0, 9.0, 0.0}, "closed offset opposite corner");
    Require(outline.IsClosed(), "closed offset remains closed");

    const Wire circle = OffsetPlanarWire(
        Wire::Circle({5.0, 5.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 5.0),
        plane,
        1.25);
    Require(std::abs(circle.ArcData().radius - 3.75) <= 1.0e-8, "circle offset radius");

    const Wire outerCircle = OffsetPlanarWire(
        Wire::Circle({5.0, 5.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 5.0),
        plane,
        -1.25);
    Require(std::abs(outerCircle.ArcData().radius - 6.25) <= 1.0e-8, "circle opposite offset radius");

    const Wire clockwiseArc = OffsetPlanarWire(
        Wire::CircularArc(
            {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0},
            5.0, 0.0, -1.57079632679489661923),
        plane,
        1.0);
    Require(std::abs(clockwiseArc.ArcData().radius - 6.0) <= 1.0e-8, "clockwise arc left offset");
}

void OffsetsOnArbitraryWorkPlaneAndRejectsInvalidInputs()
{
    const WorkPlane plane = WorkPlane::FromPointNormal(
        {2.0, 3.0, 4.0}, {0.0, -1.0, 0.0}, {1.0, 0.0, 0.0});
    const Wire source = Wire::Line(plane.ToWorld(0.0, 0.0), plane.ToWorld(8.0, 0.0));
    const Wire offset = OffsetPlanarWire(source, plane, 1.5);
    const auto start = plane.Project(offset.Start());
    const auto end = plane.Project(offset.End());
    Require(std::abs(start.u) <= 1.0e-8 && std::abs(start.v - 1.5) <= 1.0e-8, "arbitrary plane offset start");
    Require(std::abs(end.u - 8.0) <= 1.0e-8 && std::abs(end.v - 1.5) <= 1.0e-8, "arbitrary plane offset end");

    bool rejected = false;
    try {
        (void)OffsetPlanarWire(
            Wire::CubicBezier({0.0, 0.0, 0.0}, {2.0, 1.0, 0.0}, {4.0, 1.0, 0.0}, {6.0, 0.0, 0.0}),
            plane,
            1.0);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Require(rejected, "Bezier offset rejected instead of approximated");
}

void AppliesPersistentLineConstraints()
{
    const WorkPlane plane = WorkPlane::FromPointNormal({}, {0.0, 0.0, 1.0});
    const Wire constrained = ApplyWireLineConstraints(
        Wire::Line({2.0, 3.0, 0.0}, {5.0, 7.0, 0.0}),
        plane,
        WireLineConstraints{10.0, 30.0});
    RequireNear(constrained.Start(), {2.0, 3.0, 0.0}, "constrained line anchor");
    RequireNear(
        constrained.End(),
        {2.0 + 5.0 * std::sqrt(3.0), 8.0, 0.0},
        "constrained line length and angle");

    const Wire snappedToPlane = ApplyWireLineConstraints(
        Wire::Line({2.0, 3.0, 0.002}, {5.0, 7.0, -0.003}),
        plane,
        WireLineConstraints{std::nullopt, 0.0});
    RequireNear(snappedToPlane.Start(), {2.0, 3.0, 0.0}, "angle constraint snaps anchor to plane");
    Require(std::abs(snappedToPlane.End().z) <= 1.0e-8, "angle constraint snaps end to plane");

    const Wire threeDimensional = ApplyWireLineConstraints(
        Wire::Line({1.0, 2.0, 3.0}, {3.0, 5.0, 9.0}),
        std::nullopt,
        WireLineConstraints{14.0, std::nullopt});
    Require(std::abs((threeDimensional.End() - threeDimensional.Start()).Length() - 14.0) <= 1.0e-8,
        "free 3d line length constraint");

    bool missingPlaneRejected = false;
    try {
        (void)ApplyWireLineConstraints(
            Wire::Line({}, {1.0, 1.0, 0.0}),
            std::nullopt,
            WireLineConstraints{std::nullopt, 45.0});
    } catch (const std::invalid_argument&) {
        missingPlaneRejected = true;
    }
    Require(missingPlaneRejected, "angle constraint without plane rejected");
}

void AppliesPersistentRadiusConstraints()
{
    const Wire circle = ApplyWireCurveConstraints(
        Wire::Circle({1.0, 2.0, 3.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 2.0),
        WireCurveConstraints{4.5});
    Require(std::abs(circle.ArcData().radius - 4.5) <= 1.0e-12, "circle radius constraint");
    RequireNear(circle.ArcData().center, {1.0, 2.0, 3.0}, "circle radius keeps center");

    const Wire arc = ApplyWireCurveConstraints(
        Wire::CircularArc(
            {}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 2.0, 0.25, 1.75),
        WireCurveConstraints{7.25});
    Require(std::abs(arc.ArcData().radius - 7.25) <= 1.0e-12, "arc radius constraint");
    Require(std::abs(arc.ArcData().startAngleRadians - 0.25) <= 1.0e-12,
        "arc radius keeps start angle");
    Require(std::abs(arc.ArcData().sweepAngleRadians - 1.75) <= 1.0e-12,
        "arc radius keeps sweep angle");

    bool lineRejected = false;
    try {
        (void)ApplyWireCurveConstraints(
            Wire::Line({}, {1.0, 0.0, 0.0}), WireCurveConstraints{1.0});
    } catch (const std::invalid_argument&) {
        lineRejected = true;
    }
    Require(lineRejected, "radius constraint rejects non-circular wire");
}

} // namespace

namespace {

void FindsWireIntersectionsIn3d()
{
    // 交差する2直線(3D)。
    const Wire lineA = Wire::Line({-10.0, 0.0, 5.0}, {10.0, 0.0, 5.0});
    const Wire lineB = Wire::Line({0.0, -10.0, 5.0}, {0.0, 10.0, 5.0});
    const auto crossing = kachakacha::model::IntersectWires(lineA, lineB);
    Require(crossing.size() == 1, "crossing lines meet once");
    RequireNear(crossing.front(), {0.0, 0.0, 5.0}, "3d crossing position");

    // 円と直線は2点で交わる。
    const Wire circle = Wire::Circle({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 10.0);
    const Wire chord = Wire::Line({-20.0, 5.0, 0.0}, {20.0, 5.0, 0.0});
    const auto chordHits = kachakacha::model::IntersectWires(circle, chord, 1.0e-4);
    Require(chordHits.size() == 2, "circle and chord meet twice");
    for (const Vector3& hit : chordHits) {
        Require(std::abs(hit.Length() - 10.0) < 1.0e-3, "hits lie on the circle");
        Require(std::abs(hit.y - 5.0) < 1.0e-3, "hits lie on the chord");
    }

    // すれ違う(交わらない)2直線は空。
    const Wire skewA = Wire::Line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    const Wire skewB = Wire::Line({0.0, 5.0, 3.0}, {10.0, 5.0, 3.0});
    Require(kachakacha::model::IntersectWires(skewA, skewB).empty(),
        "skew lines produce no intersection");
}

void CutsAndRoundsPolylineCorners()
{
    // 30x20 の長方形(閉)。
    const Wire rectangle = Wire::Polyline({
        {0.0, 0.0, 0.0}, {30.0, 0.0, 0.0}, {30.0, 20.0, 0.0},
        {0.0, 20.0, 0.0}, {0.0, 0.0, 0.0}});

    // C面取り: 頂点1(30,0)を5mmで落とす。
    const auto cut = kachakacha::model::CutPolylineCorner(rectangle, 1, 5.0);
    RequireNear(cut.firstPoint, {25.0, 0.0, 0.0}, "chamfer start point");
    RequireNear(cut.secondPoint, {30.0, 5.0, 0.0}, "chamfer end point");
    Require(cut.wire.IsClosed(1.0e-9), "chamfered rectangle stays closed");
    Require(cut.wire.ControlPoints().size() == rectangle.ControlPoints().size() + 1,
        "chamfer adds one vertex");

    // R丸め: 頂点2(30,20)をR4で丸める。
    const auto rounded = kachakacha::model::RoundPolylineCorner(rectangle, 2, 4.0);
    RequireNear(rounded.firstPoint, {30.0, 16.0, 0.0}, "fillet tangent on first edge");
    RequireNear(rounded.secondPoint, {26.0, 20.0, 0.0}, "fillet tangent on second edge");
    Require(rounded.wire.IsClosed(1.0e-9), "rounded rectangle stays closed");
    const Vector3 center{26.0, 16.0, 0.0};
    bool sawArcPoint = false;
    for (const Vector3& point : rounded.wire.ControlPoints()) {
        const double distance = (point - center).Length();
        if (point.x > 26.0 + 1.0e-9 && point.y > 16.0 + 1.0e-9) {
            Require(std::abs(distance - 4.0) < 1.0e-6, "arc points lie on the fillet circle");
            sawArcPoint = true;
        }
    }
    Require(sawArcPoint, "fillet inserts arc points");

    // 閉ポリラインの始終点の角(頂点0)も加工できる。
    const auto seamCut = kachakacha::model::CutPolylineCorner(rectangle, 0, 3.0);
    Require(seamCut.wire.IsClosed(1.0e-9), "seam chamfer stays closed");

    // 開ポリラインの端点は角ではない。
    const Wire open = Wire::Polyline({{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, {10.0, 10.0, 0.0}});
    bool rejected = false;
    try {
        (void)kachakacha::model::CutPolylineCorner(open, 0, 2.0);
    } catch (const std::exception&) {
        rejected = true;
    }
    Require(rejected, "open polyline endpoints are rejected");
    // 大きすぎる半径は拒否。
    rejected = false;
    try {
        (void)kachakacha::model::RoundPolylineCorner(rectangle, 1, 25.0);
    } catch (const std::exception&) {
        rejected = true;
    }
    Require(rejected, "oversized fillet radius is rejected");
}

} // namespace

int main()
{
    try {
        FindsWireIntersectionsIn3d();
        CutsAndRoundsPolylineCorners();
        ChamfersSharedCornerAutomatically();
        ChamfersChosenBranchesAtCrossing();
        RejectsInvalidChamfers();
        ChamfersAndFilletsSeparatedLinesWithAutoExtension();
        FilletsSharedCornerWithTangentArc();
        FilletsCornerInArbitrary3DPlane();
        ExtendsSeparatedLinesToTheirIntersection();
        TrimsCrossingLinesToChosenBranches();
        TrimsTheDirectlyPickedLinePortion();
        ExtendsTheDirectlyPickedLineEndToNearestBoundary();
        TrimsAndExtendsNativeCurvesWithoutFlattening();
        JoinsUnorderedLineChain();
        RejectsDisconnectedJoin();
        JoinsMixedCurvesIntoAClosedContour();
        JoinsMixedCurvesAcrossSmallDrawingGaps();
        RejectsBranchedAndDuplicateChains();
        OffsetsPlanarDrawingWires();
        OffsetsOnArbitraryWorkPlaneAndRejectsInvalidInputs();
        AppliesPersistentLineConstraints();
        AppliesPersistentRadiusConstraints();
    } catch (const std::exception& error) {
        std::cerr << "wire_operations_tests failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "wire_operations_tests passed\n";
    return 0;
}
