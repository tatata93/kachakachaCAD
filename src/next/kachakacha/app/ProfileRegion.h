#pragma once

//! 画面で「閉じた線の内側」を選ぶための一時的な領域。
//!
//! ProfileRegion は文書へ保存するEntityではない。Wireを正本のまま参照し、
//! 押し出しや平面Surfaceの入力を人間が内側クリックで指定する間だけ使う。

#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/modeling/SnapEngine.h"

#include <cstddef>
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

//! 境界や穴を避けた決定的な内点。領域のクリック選択に使う。
[[nodiscard]] geometry::Vector3 ProfileRegionInterior(const ProfileRegion& region);
//! 明示した内点を含む区画だけを保持。空なら全区画（従来の物体選択）。
void FilterProfileRegions(std::vector<ProfileRegion>& regions,
    const std::vector<geometry::Vector3>& seeds, double toleranceMm);

//! 場面にある非補助線を端点接続で組み立て、閉じた平面領域を列挙する。
//! T/X分岐を一時的に区切り、平面ごとに閉領域を列挙する。空間交差は投影しない。
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

//! 領域を、載っている平面ごとの組に分ける(押し出しの輪郭が違う平面にあるとき、
//! 平面ごとに別の押し出しにするため)。返すのは regions の添字の組。
//! 向きがそろい(裏表は問わない)、外周の点がどれも組の最初の領域の平面から
//! limitMm 以内なら同じ組。組の並びは、その組の最初の領域が出てきた順。
[[nodiscard]] std::vector<std::vector<std::size_t>> GroupProfileRegionsByPlane(
    const std::vector<ProfileRegion>& regions, double limitMm);

} // namespace kachakacha::v2::app
