#pragma once

//! 画面で「閉じた線の内側」を選ぶための一時的な領域。
//!
//! ProfileRegion は文書へ保存するEntityではない。Wireを正本のまま参照し、
//! 押し出しや平面Surfaceの入力を人間が内側クリックで指定する間だけ使う。

#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/modeling/SnapEngine.h"

#include <vector>

namespace kachakacha::v2::app {

struct ProfileBoundary {
    std::vector<base::EntityId> entityIds;
    std::vector<base::SegmentId> segmentIds;
    std::vector<geometry::CurveSegment> segments;
    std::vector<geometry::Vector3> sampled;
    std::vector<geometry::Point2> planar;
};

struct ProfileRegion {
    ProfileBoundary outer;
    std::vector<ProfileBoundary> holes;
    geometry::PlaneFit plane;
    geometry::PlanarFrame frame;
    double areaMm2 = 0.0;
};

//! 場面にある非補助線を端点接続で組み立て、閉じた平面領域を列挙する。
//! 分岐や開いた鎖は領域にせず、別の正当な閉領域まで巻き添えにしない。
[[nodiscard]] std::vector<ProfileRegion> DetectProfileRegions(
    const modeling::SnapScene& scene, const geometry::GeometryTolerance& tolerance);

//! 指定したWireだけから領域を作る。押し出しFeatureの再評価にも同じ認識を使う。
[[nodiscard]] std::vector<ProfileRegion> DetectProfileRegions(
    const modeling::SnapScene& scene, const std::vector<base::EntityId>& entityIds,
    const geometry::GeometryTolerance& tolerance);

//! 3D点を領域平面へ落として内外を調べる。穴の中はfalse。
[[nodiscard]] bool ProfileRegionContains(const ProfileRegion& region,
    const geometry::Vector3& point, double planeToleranceMm);

//! 外周と穴が参照するEntityを重複なしで返す。
[[nodiscard]] std::vector<base::EntityId> ProfileRegionEntityIds(
    const ProfileRegion& region);

} // namespace kachakacha::v2::app
