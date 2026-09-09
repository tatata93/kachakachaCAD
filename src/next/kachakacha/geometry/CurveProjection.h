#pragma once

//! 線を平面へ落とす(AT-FAB-007)。
//!
//! 落とすときに、**線の種類を勝手に変えない。**
//! 直線は直線のまま、ベジエとスプラインは制御点を落とせば同じ次数の曲線になる。
//! これは射影が一次変換だからで、近似ではない。
//!
//! 円と円弧だけは違う。斜めの面へ落とすと **楕円** になる。
//! 楕円は V2 の線の種類に無い。折れ線で近似すれば通せてしまうが、
//! それは「円を落とした」ことにならないので、断る。
//! 面が円の面と平行なときだけは、円のまま落ちるので通す。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"

#include <vector>

namespace kachakacha::v2::geometry {

//! 落とす先の平面。点と法線で持つ。
struct ProjectionPlane {
    Vector3 origin{};
    Vector3 normal{0.0, 0.0, 1.0};
};

//! 点を平面へ落とす。法線の向きに垂直に落とす。
[[nodiscard]] Vector3 ProjectPointOntoPlane(const Vector3& point,
    const ProjectionPlane& plane);

//! 線1本を落とす。落とせない種類は断る。
[[nodiscard]] base::Result<CurveSegment> ProjectCurveOntoPlane(const CurveSegment& curve,
    const ProjectionPlane& plane, const GeometryTolerance& tolerance);

//! まとめて落とす。1本でも落とせなければ、そこで断る。
//! 半分だけ落ちた形を返すと、どこまで落ちたのかが分からなくなる。
[[nodiscard]] base::Result<std::vector<CurveSegment>> ProjectCurvesOntoPlane(
    const std::vector<CurveSegment>& curves, const ProjectionPlane& plane,
    const GeometryTolerance& tolerance);

} // namespace kachakacha::v2::geometry
