#pragma once
#include "kachakacha/app/SurfaceRoleAssist.h"

namespace kachakacha::v2::app {
//! 役割分類で作れない閉じた空間線網を、接続区画ごとの表へ変換する。
[[nodiscard]] std::vector<modeling::GuideTable> SurfaceNetworkTables(
    const std::vector<RoleWire>& wires, const geometry::GeometryTolerance& tolerance);
}
