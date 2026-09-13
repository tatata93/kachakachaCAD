#include "kachakacha/app/PointerGesture.h"

#include <cmath>

namespace kachakacha::v2::app {

bool ExceedsDragThreshold(double dxPx, double dyPx) noexcept
{
    return std::hypot(dxPx, dyPx) >= kClickDragThresholdPx;
}

void PointerGestureTracker::Begin(const geometry::ScreenPoint& origin) noexcept
{
    origin_ = origin;
    active_ = true;
    drag_ = false;
}

bool PointerGestureTracker::Update(const geometry::ScreenPoint& now) noexcept
{
    if (!active_ || drag_) {
        // すでにドラッグなら、戻ってきても押しただけには戻さない。
        return false;
    }
    if (!ExceedsDragThreshold(now.x - origin_.x, now.y - origin_.y)) {
        return false;
    }
    drag_ = true;
    return true;
}

PointerGesture PointerGestureTracker::Release(const geometry::ScreenPoint& now) noexcept
{
    (void)Update(now);
    const PointerGesture kind = Kind();
    return kind;
}

void PointerGestureTracker::Reset() noexcept
{
    origin_ = geometry::ScreenPoint{};
    active_ = false;
    drag_ = false;
}

} // namespace kachakacha::v2::app
