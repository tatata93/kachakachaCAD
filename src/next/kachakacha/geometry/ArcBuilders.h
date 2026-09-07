#pragma once

//! 円弧の作り方(PRD-052)。
//! 3点、両端+半径、始点+接線方向+半径/中心角/円弧長。
//! どの作り方でも、できあがるのは中心・法線・基準方向・半径・開始角・掃引角を持つ円弧。

#include "kachakacha/geometry/CurveSegment.h"

namespace kachakacha::v2::geometry {

//! 3点を通る円弧。3点が一直線に並んでいたら作れない。
[[nodiscard]] base::Result<CurveSegment> ArcThroughThreePoints(Vector3 start,
    Vector3 middle, Vector3 end);

//! 両端と半径。半径が小さすぎる(弦の半分未満)と作れない。
//! largeArc は優弧を取るか、clockwise は法線の向きを反転するか。
[[nodiscard]] base::Result<CurveSegment> ArcFromEndpointsAndRadius(Vector3 start,
    Vector3 end, double radius, Vector3 planeNormal, bool largeArc, bool clockwise);

//! 始点、始点での接線方向、半径、中心角。
[[nodiscard]] base::Result<CurveSegment> ArcFromStartTangentRadiusSweep(Vector3 start,
    Vector3 tangent, Vector3 planeNormal, double radius, double sweepAngleRad);

//! 始点、始点での接線方向、半径、円弧長。
[[nodiscard]] base::Result<CurveSegment> ArcFromStartTangentRadiusLength(Vector3 start,
    Vector3 tangent, Vector3 planeNormal, double radius, double arcLengthMm);

} // namespace kachakacha::v2::geometry
