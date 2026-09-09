#include "kachakacha/app/GrabToMove.h"

#include <cmath>

namespace kachakacha::v2::app {
namespace {

//! V1の `kGrabSlopPx`。これ未満は「押しただけ」とみなす。
constexpr double kGrabSlopPx = 4.0;

} // namespace

bool PointerGrabsSelection(const modeling::SnapScene& scene, const SelectionSet& selection,
    const geometry::ScreenMapping& mapping, const geometry::ScreenPoint& pointer,
    const geometry::GeometryTolerance& tolerance)
{
    if (selection.entityIds.empty()) {
        return false;
    }
    const auto picked = PickCurve(scene, mapping, pointer, tolerance);
    if (!picked.has_value()) {
        return false;
    }
    // 拾えたものが、いま選んでいるものでなければ掴みではない。
    // ここを「拾えたら掴み」にすると、隣の線を選び直せなくなる。
    return IsSelected(selection, picked->entityId);
}

bool DragIsFarEnough(double dxPx, double dyPx) noexcept
{
    return std::sqrt(dxPx * dxPx + dyPx * dyPx) >= kGrabSlopPx;
}

geometry::Vector3 DragDelta(const geometry::Vector3& from,
    const geometry::Vector3& to) noexcept
{
    return to - from;
}

} // namespace kachakacha::v2::app
