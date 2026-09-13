#include "kachakacha/app/ControlPointPick.h"

namespace kachakacha::v2::app {
namespace {

//! V1の制御点の当たり判定。線の当たり判定より少し広い。
constexpr double kControlPointPickPx = 9.0;

} // namespace

double ControlPointPickPx() noexcept
{
    return kControlPointPickPx;
}

std::vector<ShownControlPoint> ControlPointsForSelection(const modeling::SnapScene& scene,
    const SelectionSet& selection)
{
    std::vector<ShownControlPoint> shown;
    for (const auto& curve : scene.curves) {
        // 線を選択色で描くかどうかと同じ規則で決める。物体単位で見ると、
        // 線分を1本選んだだけで同じワイヤーの他の線分にも選択色の四角が出て、
        // 選んでいない線分まで掴めてしまう(ui-ux-integrated-spec §3)。
        if (!IsCurveSelected(selection, curve.entityId, curve.segmentId)) {
            continue;
        }
        const auto points = geometry::EditableControlPointsOf(curve.segment);
        for (std::size_t index = 0; index < points.size(); ++index) {
            shown.push_back({curve.entityId, curve.segmentId, index,
                points[index].position, points[index].labelJa});
        }
    }
    return shown;
}

std::optional<ShownControlPoint> PickControlPoint(const modeling::SnapScene& scene,
    const SelectionSet& selection, const geometry::ScreenMapping& mapping,
    const geometry::ScreenPoint& pointer)
{
    std::optional<ShownControlPoint> best;
    double bestDistance = kControlPointPickPx;
    for (const auto& point : ControlPointsForSelection(scene, selection)) {
        const auto screen = mapping.Project(point.position);
        if (!screen.has_value()) {
            continue;
        }
        const double distance = geometry::ScreenDistance(*screen, pointer);
        // 同じ距離のものが並んだら、先に見た方を残す。毎回同じ結果になる。
        if (distance < bestDistance) {
            bestDistance = distance;
            best = point;
        }
    }
    return best;
}

} // namespace kachakacha::v2::app
