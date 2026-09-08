#pragma once

//! 曲線どうしの交差(geometry-contract §4)。
//!
//! 大事な区別が2つある。
//!   実3D交点  : 2曲線が本当に同じ位置を通る。距離が modelLinearMm 以下。
//!   画面交差  : 見た目だけ重なっている。手前と奥で離れている。
//! 後者を既定のスナップ候補にしてはならない。V1はここを分けていなかったので、
//! 「画面では線が交わっているのに、点を打つと奥の別の場所へ飛ぶ」が起きた。

#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/ScreenMapping.h"

#include <vector>

namespace kachakacha::v2::geometry {

struct CurveIntersection {
    Vector3 position{};        //!< 2曲線の最近点の中点
    double firstParameter = 0.0;
    double secondParameter = 0.0;
    double gapMm = 0.0;        //!< 2曲線の隙間。実3D交点なら許容差以下
    bool real = true;          //!< false なら画面上だけの交差
};

//! 3Dでの交差候補。実交点だけを返す。
[[nodiscard]] std::vector<CurveIntersection> IntersectCurves(const CurveSegment& first,
    const CurveSegment& second, const GeometryTolerance& tolerance);

//! 画面上で重なって見える場所も含めて返す。real で見分ける。
[[nodiscard]] std::vector<CurveIntersection> IntersectCurvesOnScreen(
    const CurveSegment& first, const CurveSegment& second, const ScreenMapping& mapping,
    double screenToleranceMm, const GeometryTolerance& tolerance);

//! 曲線上で、指定した点にいちばん近い位置。
[[nodiscard]] ClosestPointResult ClosestOnCurve(const CurveSegment& segment,
    const Vector3& point);

//! 曲線の四半点(円・円弧は90度ごと、それ以外は 1/4 ごとの位置)。
[[nodiscard]] std::vector<Vector3> QuadrantPoints(const CurveSegment& segment);

//! 直線の延長線上で、指定した点にいちばん近い位置。
//! 曲線が延長できない種類(円、閉じたもの)なら値を返さない。
[[nodiscard]] std::optional<Vector3> ExtensionPoint(const CurveSegment& segment,
    const Vector3& point, double maximumExtensionMm);

//! 指定した点から曲線へ下ろした垂線の足。接線と垂直になる位置。
[[nodiscard]] std::vector<Vector3> PerpendicularFeet(const CurveSegment& segment,
    const Vector3& point, const GeometryTolerance& tolerance);

//! 指定した点から曲線へ引いた接線の接点。
[[nodiscard]] std::vector<Vector3> TangentPoints(const CurveSegment& segment,
    const Vector3& point, const GeometryTolerance& tolerance);

} // namespace kachakacha::v2::geometry
