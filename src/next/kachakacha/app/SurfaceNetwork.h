#pragma once
#include "kachakacha/app/SurfaceRoleAssist.h"

namespace kachakacha::v2::app {
struct SurfaceNetworkPlan {
    std::vector<modeling::GuideTable> tables;
    std::vector<modeling::GuideTableSelection> unusedSegments;
};
//! 閉じた区画と、輪に使わなかった実区間を返す。余る枝は閉区画を無効にしない。
[[nodiscard]] SurfaceNetworkPlan PlanSurfaceNetwork(
    const std::vector<RoleWire>& wires, const geometry::GeometryTolerance& tolerance);
//! 役割分類で作れない閉じた空間線網を、接続区画ごとの表へ変換する。
[[nodiscard]] std::vector<modeling::GuideTable> SurfaceNetworkTables(
    const std::vector<RoleWire>& wires, const geometry::GeometryTolerance& tolerance);
}
