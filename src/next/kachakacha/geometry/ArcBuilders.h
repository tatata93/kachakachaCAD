#pragma once

//! 円弧の作り方(PRD-052)。
//! 3点、両端+半径、始点+接線方向+半径/中心角/円弧長。
//! どの作り方でも、できあがるのは中心・法線・基準方向・半径・開始角・掃引角を持つ円弧。

#include "kachakacha/geometry/CurveSegment.h"

namespace kachakacha::v2::geometry {

//! 3点を通る円弧。3点が一直線に並んでいたら作れない。
[[nodiscard]] base::Result<CurveSegment> ArcThroughThreePoints(Vector3 start,
    Vector3 middle, Vector3 end);

//! 3点を通る円(D-04)。3点が一直線に並んでいたら作れない。
//! preferredNormal と向きが逆なら法線を裏返す(作業平面の上の円は作業平面と同じ向きにする)。
[[nodiscard]] base::Result<CurveSegment> CircleThroughThreePoints(Vector3 first, Vector3 second,
    Vector3 third, Vector3 preferredNormal);

//! 中心・始点・終点(D-08)。半径は中心から始点まで、終点は向きだけを使う。
//! 始点から終点へ、面の法線まわりに左回り(反時計回り)。始点と終点が同じ向きなら作れない。
//! 円弧の面は 3 点が決める(3D の点へ吸着していても始点を必ず通る)。planeNormal は表裏と、
//! 3 点が一直線のときの面の向きにだけ使う。
[[nodiscard]] base::Result<CurveSegment> ArcFromCenterStartEnd(Vector3 center, Vector3 start,
    Vector3 end, Vector3 planeNormal);

//! 両端と半径。半径が小さすぎる(弦の半分未満)と作れない。
//! 面は弦を含む(両端を必ず通る)。planeNormal は弦と直角に倒して使う。
//! largeArc は優弧を取るか、clockwise は法線の向きを反転するか。
[[nodiscard]] base::Result<CurveSegment> ArcFromEndpointsAndRadius(Vector3 start,
    Vector3 end, double radius, Vector3 planeNormal, bool largeArc, bool clockwise);

//! 始点、始点での接線方向、半径、中心角。面は接線を含む(planeNormal は接線と直角に倒す)。
[[nodiscard]] base::Result<CurveSegment> ArcFromStartTangentRadiusSweep(Vector3 start,
    Vector3 tangent, Vector3 planeNormal, double radius, double sweepAngleRad);

//! 始点、始点での接線方向、半径、円弧長。
[[nodiscard]] base::Result<CurveSegment> ArcFromStartTangentRadiusLength(Vector3 start,
    Vector3 tangent, Vector3 planeNormal, double radius, double arcLengthMm);

} // namespace kachakacha::v2::geometry
