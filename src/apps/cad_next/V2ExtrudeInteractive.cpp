//! 押し出しの下見と矢印ハンドル(オーナー指示 2026-09-14 §6・§7)。
//!
//! ここまでの押し出しは、命令を押すと窓が出て、そこで全部決めてから作る、
//! という道だった。押す前に何ができるのかが見えない。
//!
//! いまは、命令を押すと画面に矢印が出る。引くと距離が変わり、
//! 出来上がる形が破線で見える。数字は右の欄と常に同じ値になる。
//! Enter で確定、Esc でやめる。
//!
//! 出す破線は **本物の輪郭を押し出した形** である。作り物ではない。
//! 輪郭をそのまま距離ぶん動かした先と、角どうしをつないだ側面の線を出す。
//! 確定すると、この線のとおりの立体が出来る。

#include "V2MainWindow.h"

#include "V2ParameterDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/ExtrudeDrag.h"
#include "kachakacha/app/ExtrudeOptions.h"
#include "kachakacha/app/ExtrudePlan.h"
#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/geometry/CurveSampling.h"

#include <QString>

#include <cmath>
#include <vector>

namespace {

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;

//! 曲線1本を折れ線にする。破線で出すだけなので、細かすぎなくてよい。
constexpr int kSamplesPerCurve = 16;

[[nodiscard]] std::vector<Vector3> SampleChain(const std::vector<CurveSegment>& curves)
{
    std::vector<Vector3> points;
    for (const CurveSegment& curve : curves) {
        for (int index = 0; index <= kSamplesPerCurve; ++index) {
            const double t = static_cast<double>(index) / kSamplesPerCurve;
            points.push_back(curve.Evaluate(t));
        }
    }
    return points;
}

} // namespace

void V2MainWindow::BeginExtrudePreview()
{
    using kachakacha::v2::app::ExtrudeHandle;
    const auto plan = PlanExtrudeFromSelection();
    if (!plan.readyToPreview) {
        return;
    }
    const auto curves = kachakacha::v2::app::SelectedCurves(viewport_->Selection(),
        session_->Scene());
    if (curves.empty()) {
        return;
    }
    const std::vector<Vector3> outline = SampleChain(curves);
    if (outline.empty()) {
        return;
    }
    // 矢印の根元は輪郭の重心。端に出すと、どの輪郭のものか分からない。
    Vector3 center{};
    for (const Vector3& point : outline) {
        center = center + point;
    }
    center = center * (1.0 / static_cast<double>(outline.size()));

    ExtrudeHandle handle;
    handle.origin = center;
    // 向きはいまの作業平面の法線。V1 と同じで、面に対してまっすぐ押す。
    handle.direction = viewport_->WorkPlane().normal;
    if (extrudeChoice_.reversed) {
        handle.direction = handle.direction * -1.0;
    }
    handle.distanceMm = ExtrudeDistanceMm();
    extrudeOutline_ = outline;
    viewport_->ShowExtrudeHandle(handle, ExtrudePreviewLoops(handle.distanceMm));
    SetStatus(QStringLiteral("押し出し\n%1\n矢印を引くか、数の棚の「押し出し距離」で"
                             "決めてください。Enter で確定、Esc でやめます。")
            .arg(ExtrudePlanTextJa()));
}

std::vector<std::vector<Vector3>> V2MainWindow::ExtrudePreviewLoops(double distanceMm) const
{
    std::vector<std::vector<Vector3>> loops;
    if (extrudeOutline_.empty()) {
        return loops;
    }
    Vector3 direction = viewport_->WorkPlane().normal;
    if (extrudeChoice_.reversed) {
        direction = direction * -1.0;
    }
    const Vector3 offset = direction * distanceMm;
    // 押し出した先の輪郭。
    std::vector<Vector3> moved;
    moved.reserve(extrudeOutline_.size());
    for (const Vector3& point : extrudeOutline_) {
        moved.push_back(point + offset);
    }
    loops.push_back(extrudeOutline_);
    loops.push_back(moved);
    // 側面の線。全部の点に出すと真っ黒になるので、間引いて出す。
    const std::size_t step = std::max<std::size_t>(1, extrudeOutline_.size() / 12);
    for (std::size_t index = 0; index < extrudeOutline_.size(); index += step) {
        loops.push_back({extrudeOutline_[index], extrudeOutline_[index] + offset});
    }
    return loops;
}

void V2MainWindow::UpdateExtrudePreview(double distanceMm)
{
    if (!viewport_->ExtrudeHandleShown()) {
        return;
    }
    // 右の欄も同じ値にする。片方だけ動くと、どちらが本当か分からなくなる。
    parameterDock_->Apply(kachakacha::v2::app::ParameterId::ExtrudeDistance,
        QString::number(distanceMm, 'f', 2));
    kachakacha::v2::app::ExtrudeHandle handle;
    handle.origin = viewport_->ExtrudeHandleOrigin();
    handle.direction = viewport_->ExtrudeHandleDirection();
    handle.distanceMm = distanceMm;
    viewport_->ShowExtrudeHandle(handle, ExtrudePreviewLoops(distanceMm));
}

void V2MainWindow::EndExtrudePreview()
{
    extrudeOutline_.clear();
    viewport_->HideExtrudeHandle();
}
