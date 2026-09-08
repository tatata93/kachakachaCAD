#pragma once

//! 作業平面(PRD-055、AT-WPL-001 / 002)。
//!
//! 11通りの作り方をすべてここで扱う。OCCT は要らない。
//! 円筒面や円錐面を根拠にする方式では、カーネルが分かった面の正体
//! (`AnalyticSurfaceInfo`)だけを受け取る。B-Rep をここへ持ち込まない(AT-ARC-001)。
//!
//! 作れない条件は、近い平面を作って誤魔化さずに断る。
//! 一直線に並んだ3点、平行でない2面の中間、円筒でない面、長さ0の辺、
//! 直線の曲率法線 ── どれも「それらしい平面」を返してはならない場面である。
//!
//! 基底は必ず右手系の正規直交でなければならない(architecture-and-data.md §5.2)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/SurfacePatch.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/Vector3.h"

#include <string_view>
#include <vector>

namespace kachakacha::v2::modeling {

using base::Diagnostic;
using fabrication::AnalyticSurfaceInfo;
using geometry::CurveSegment;
using geometry::GeometryTolerance;
using geometry::Vector3;

//! 平面そのもの。原点と右手系の正規直交基底。
struct WorkPlaneFrame {
    Vector3 origin{};
    Vector3 uAxis{1.0, 0.0, 0.0};
    Vector3 vAxis{0.0, 1.0, 0.0};
    Vector3 normal{0.0, 0.0, 1.0};

    //! 平面上の (u, v) から3D座標へ。
    [[nodiscard]] Vector3 PointAt(double u, double v) const
    {
        return origin + uAxis * u + vAxis * v;
    }
    //! 3D座標を平面へ落としたときの (u, v)。
    [[nodiscard]] double CoordinateU(const Vector3& point) const
    {
        return geometry::Dot(point - origin, uAxis);
    }
    [[nodiscard]] double CoordinateV(const Vector3& point) const
    {
        return geometry::Dot(point - origin, vAxis);
    }
    //! 平面からの符号つき距離。
    [[nodiscard]] double SignedDistance(const Vector3& point) const
    {
        return geometry::Dot(point - origin, normal);
    }
};

//! 標準面の種類。
enum class StandardPlaneKind {
    XY,
    YZ,
    ZX,
};

//! 作り方。11通り(AT-WPL-001)。
enum class WorkPlaneMethod {
    Standard,             //!< 標準面(XY / YZ / ZX)
    OffsetFromPlane,      //!< 既存の平面から距離だけ離す
    ThroughPointParallel, //!< 点を通り、既存の平面と平行
    MidBetweenPlanes,     //!< 平行な2面のちょうど中間
    CylinderAxis,         //!< 円筒の中心軸を含む
    AngleAboutEdge,       //!< 辺のまわりに、既存の平面から角度だけ回す
    ThreePoints,          //!< 3点を通る
    TwoEdges,             //!< 同一平面上の2辺を含む
    TangentThroughEdge,   //!< 曲面に接し、その上の辺(母線)を含む
    TangentThroughPoint,  //!< 曲面上の点で接する
    NormalToCurveAtPoint, //!< 曲線上の点で、その曲線に直角
};

[[nodiscard]] constexpr std::string_view WorkPlaneMethodName(
    WorkPlaneMethod method) noexcept
{
    switch (method) {
    case WorkPlaneMethod::Standard:             return "standard";
    case WorkPlaneMethod::OffsetFromPlane:      return "offset_from_plane";
    case WorkPlaneMethod::ThroughPointParallel: return "through_point_parallel";
    case WorkPlaneMethod::MidBetweenPlanes:     return "mid_between_planes";
    case WorkPlaneMethod::CylinderAxis:         return "cylinder_axis";
    case WorkPlaneMethod::AngleAboutEdge:       return "angle_about_edge";
    case WorkPlaneMethod::ThreePoints:          return "three_points";
    case WorkPlaneMethod::TwoEdges:             return "two_edges";
    case WorkPlaneMethod::TangentThroughEdge:   return "tangent_through_edge";
    case WorkPlaneMethod::TangentThroughPoint:  return "tangent_through_point";
    case WorkPlaneMethod::NormalToCurveAtPoint: return "normal_to_curve_at_point";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view WorkPlaneMethodNameJa(
    WorkPlaneMethod method) noexcept
{
    switch (method) {
    case WorkPlaneMethod::Standard:             return "標準面";
    case WorkPlaneMethod::OffsetFromPlane:      return "平面から離す";
    case WorkPlaneMethod::ThroughPointParallel: return "点を通り平行";
    case WorkPlaneMethod::MidBetweenPlanes:     return "2面の中間";
    case WorkPlaneMethod::CylinderAxis:         return "円筒の中心";
    case WorkPlaneMethod::AngleAboutEdge:       return "辺まわりに角度";
    case WorkPlaneMethod::ThreePoints:          return "3点を通る";
    case WorkPlaneMethod::TwoEdges:             return "2辺を含む";
    case WorkPlaneMethod::TangentThroughEdge:   return "辺を通り接する";
    case WorkPlaneMethod::TangentThroughPoint:  return "点を通り接する";
    case WorkPlaneMethod::NormalToCurveAtPoint: return "点で曲線に直角";
    }
    return "不明";
}

//! 作り方ごとに要る材料。使わない欄は空のままでよい。
struct WorkPlaneRequest {
    WorkPlaneMethod method = WorkPlaneMethod::Standard;
    StandardPlaneKind standard = StandardPlaneKind::XY;

    //! OffsetFromPlane / ThroughPointParallel / MidBetweenPlanes / AngleAboutEdge。
    WorkPlaneFrame referencePlane;
    //! MidBetweenPlanes の相手。
    WorkPlaneFrame secondPlane;

    double offsetMm = 0.0;
    double angleRad = 0.0;

    //! ThroughPointParallel(1点)、ThreePoints(3点)、
    //! TangentThroughPoint(1点)、NormalToCurveAtPoint(未使用)。
    std::vector<Vector3> points;

    //! AngleAboutEdge(1本)、TwoEdges(2本)、TangentThroughEdge(1本)、
    //! NormalToCurveAtPoint(1本)。
    std::vector<CurveSegment> edges;

    //! CylinderAxis / TangentThroughEdge / TangentThroughPoint。
    AnalyticSurfaceInfo surface;

    //! NormalToCurveAtPoint。曲線上の位置 t(0〜1)。
    double curveParameter = 0.0;
    //! NormalToCurveAtPoint。u 軸を曲率法線に合わせるか。
    //! true にすると、直線では作れない(曲率法線が定義できない)。
    bool useCurvatureNormal = true;
};

//! 作れたら平面を返す。作れなければ GEO-P0xx で断る。
[[nodiscard]] base::Result<WorkPlaneFrame> BuildWorkPlane(const WorkPlaneRequest& request,
    const GeometryTolerance& tolerance);

//! 基底が右手系の正規直交かを確かめる。作った後に必ず通す。
[[nodiscard]] bool IsOrthonormalRightHanded(const WorkPlaneFrame& frame,
    double tolerance = 1.0e-9);

//! 法線だけが決まっているとき、u/v を決まった手順で決める。
//! 同じ法線からは必ず同じ u/v が出る(決定的)。
[[nodiscard]] base::Result<WorkPlaneFrame> FrameFromNormal(const Vector3& origin,
    const Vector3& normal, const GeometryTolerance& tolerance);

//! 法線と、u 軸に使いたい向きが決まっているとき。
//! hint が法線と平行なら断る。
[[nodiscard]] base::Result<WorkPlaneFrame> FrameFromNormalAndU(const Vector3& origin,
    const Vector3& normal, const Vector3& uHint, const GeometryTolerance& tolerance);

//! 標準面。
[[nodiscard]] WorkPlaneFrame StandardPlane(StandardPlaneKind kind);

} // namespace kachakacha::v2::modeling
