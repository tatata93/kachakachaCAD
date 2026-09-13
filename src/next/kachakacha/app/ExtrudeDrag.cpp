#include "kachakacha/app/ExtrudeDrag.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace kachakacha::v2::app {
namespace {

using geometry::ScreenPoint;
using geometry::Vector3;

//! 向きが画面の上でこれより短く見えるなら、距離を決めない(logical px)。
//! 短いほど、1px の揺れが大きな mm に化ける。
constexpr double kMinimumScreenSpanPx = 4.0;
//! 直線へ落とすときに使う、世界の上での物差しの長さ(mm)。
constexpr double kProbeLengthMm = 10.0;

} // namespace

Vector3 ExtrudeHandleTip(const ExtrudeHandle& handle) noexcept
{
    return handle.origin + handle.direction * handle.distanceMm;
}

std::optional<double> ExtrudeDistanceFromPointer(const ExtrudeHandle& handle,
    const geometry::ScreenMapping& mapping, const ScreenPoint& pointer)
{
    // 根元と、そこから 10mm 進んだ点を画面へ写す。この2点で、
    // 「画面の 1px が何 mm か」がその向きについて分かる。
    const auto base = mapping.Project(handle.origin);
    const auto probe = mapping.Project(handle.origin + handle.direction * kProbeLengthMm);
    if (!base.has_value() || !probe.has_value()) {
        return std::nullopt;
    }
    const double dx = probe->x - base->x;
    const double dy = probe->y - base->y;
    const double spanPx = std::hypot(dx, dy);
    if (spanPx < kMinimumScreenSpanPx) {
        // 向きが画面と垂直に近い。画面では点にしか見えないので決められない。
        return std::nullopt;
    }
    // マウスの位置を、その直線へ落とす。
    const double toX = pointer.x - base->x;
    const double toY = pointer.y - base->y;
    const double alongPx = (toX * dx + toY * dy) / spanPx;
    return alongPx / spanPx * kProbeLengthMm;
}

double ExtrudeDistanceAfterDrag(const ExtrudeHandle& handle,
    const geometry::ScreenMapping& mapping, const ScreenPoint& pressedAt,
    const ScreenPoint& pointer)
{
    const auto atPress = ExtrudeDistanceFromPointer(handle, mapping, pressedAt);
    const auto now = ExtrudeDistanceFromPointer(handle, mapping, pointer);
    if (!atPress.has_value() || !now.has_value()) {
        return handle.distanceMm;   // 決められない。動かさない。
    }
    return handle.distanceMm + (*now - *atPress);
}

std::string ExtrudeDistanceLabel(double distanceMm)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.1f mm", distanceMm);
    return std::string(buffer);
}

} // namespace kachakacha::v2::app
