//! 3D の中の役割の札を置く(オーナー指示 2026-09-15 §7、UI の正本の 3D 図)。
//!
//! 言葉は core が決める(`app/ToolRoleLabels.h`)。ここは置き場所だけを出す。
//! 右の棚に「断面 3本」と出ていても、**画面のどの線がその3本なのかが
//! 分からなかった。**選び直すときに、見て確かめられるようにする。

#include "V2MainWindow.h"

#include "V2Viewport.h"

#include "kachakacha/app/ProfileRegion.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/SurfaceRoleAssist.h"
#include "kachakacha/app/ToolRoleLabels.h"

#include <QColor>
#include <QString>

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

using kachakacha::v2::geometry::Vector3;

namespace {

Vector3 RegionLabelPoint(const kachakacha::v2::app::ProfileRegion& region)
{
    Vector3 sum{};
    std::size_t count = 0;
    for (const auto& segment : region.outer.segments) {
        sum = sum + segment.Evaluate(0.5);
        ++count;
    }
    return count == 0 ? region.plane.origin : sum * (1.0 / static_cast<double>(count));
}

} // namespace

//! その番号のものを、画面のどこに指させばよいか。
//!
//! 線なら真ん中の点。立体・面なら外接箱の真ん中。
//! 取れなければ何も返さない。**無いものを指さない。**
std::optional<Vector3> V2MainWindow::PointForRoleLabel(
    const kachakacha::v2::base::EntityId& id) const
{
    kachakacha::v2::app::SelectionSet one;
    one.entityIds.push_back(id);
    const auto curves = kachakacha::v2::app::SelectedCurves(one, session_->Scene());
    if (!curves.empty()) {
        // 真ん中の曲線の真ん中。端だと、隣の線の札と重なりやすい。
        return curves[curves.size() / 2].Evaluate(0.5);
    }
    if (viewport_ == nullptr) {
        return std::nullopt;
    }
    for (const auto& shape : viewport_->ShapeViews()) {
        if (shape.entityId != id || shape.mesh.Empty()) {
            continue;
        }
        return Vector3{(shape.mesh.minimum.x + shape.mesh.maximum.x) * 0.5,
            (shape.mesh.minimum.y + shape.mesh.maximum.y) * 0.5,
            (shape.mesh.minimum.z + shape.mesh.maximum.z) * 0.5};
    }
    return std::nullopt;
}

//! core が決めた札を、置き場所つきで画面へ渡す。
void V2MainWindow::ShowRoleLabels(
    const std::vector<kachakacha::v2::app::ToolRoleLabel>& labels)
{
    if (viewport_ == nullptr) {
        return;
    }
    std::vector<V2Viewport::PlacedRoleLabel> placed;
    for (const auto& label : labels) {
        const auto at = PointForRoleLabel(label.entityId);
        if (!at.has_value()) {
            continue;
        }
        placed.push_back(
            V2Viewport::PlacedRoleLabel{*at, QString::fromStdString(label.text)});
    }
    viewport_->ShowToolRoleLabels(std::move(placed));
}

//! 押し出しの札を出し直す。
void V2MainWindow::RefreshExtrudeRoleLabels(
    const kachakacha::v2::app::ExtrudeInputState& state)
{
    if (!state.profileIsFace && !state.profiles.empty()) {
        auto withoutProfiles = state;
        withoutProfiles.profiles.clear();
        std::vector<V2Viewport::PlacedRoleLabel> placed;
        for (const auto& label : kachakacha::v2::app::ExtrudeRoleLabels(withoutProfiles)) {
            if (const auto at = PointForRoleLabel(label.entityId); at.has_value()) {
                placed.push_back({*at, QString::fromStdString(label.text)});
            }
        }
        const auto regions = kachakacha::v2::app::DetectProfileRegions(session_->Scene(),
            state.profiles, session_->GetDocument().Snapshot().settings.tolerance);
        for (std::size_t index = 0; index < regions.size(); ++index) {
            placed.push_back({RegionLabelPoint(regions[index]),
                QStringLiteral("PROFILE %1").arg(static_cast<int>(index + 1))});
        }
        viewport_->ShowToolRoleLabels(std::move(placed));
        return;
    }
    ShowRoleLabels(kachakacha::v2::app::ExtrudeRoleLabels(state));
}

//! 「面を作る」の札を出し直す。
void V2MainWindow::RefreshSurfaceRoleLabels()
{
    if (!surfaceShelfShown_) {
        if (viewport_ != nullptr) {
            viewport_->HideToolRoleLabels();
            viewport_->SetRoleColors({});
        }
        return;
    }
    if (viewport_ == nullptr) {
        return;
    }
    // 役割の色分け(ガイド = 青、断面 = 橙、境界 = 紫、通る線 = 緑)。線と札を同じ色にする。
    std::vector<std::pair<kachakacha::v2::base::EntityId, QColor>> colors;
    for (const auto& [id, rgb] : kachakacha::v2::app::SurfaceRoleColors(surfaceInput_)) {
        colors.emplace_back(id, QColor(rgb.r, rgb.g, rgb.b));
    }
    std::vector<V2Viewport::PlacedRoleLabel> placed;
    for (const auto& label : kachakacha::v2::app::SurfaceRoleLabels(surfaceInput_)) {
        const auto at = PointForRoleLabel(label.entityId);
        if (!at.has_value()) {
            continue;
        }
        QColor color;
        for (const auto& [id, roleColor] : colors) {
            if (id == label.entityId) {
                color = roleColor;
            }
        }
        placed.push_back(V2Viewport::PlacedRoleLabel{*at, QString::fromStdString(label.text), color});
    }
    viewport_->ShowToolRoleLabels(std::move(placed));
    viewport_->SetRoleColors(std::move(colors));
}
