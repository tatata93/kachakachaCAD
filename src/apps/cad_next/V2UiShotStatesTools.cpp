//! 新しく作れるようにした道具の撮影場面(回転体 P-08、フィレット P-12、シェル・分割 P-13、面積 C-15)。
//!
//! V2UiShotStates.cpp と同じ考え方: **写真を撮るための場面づくりであり、試験ではない。**
//! 人の道の確かめは V2SelfTestSolid / EdgeFinish / ShellSplit / Drawing が持つ。場面を作る道は、
//! できるだけ人と同じにする(矩形・線を手で引く、画面を押す、帯や棚のボタンを押す)。

#include "V2MainWindow.h"

#include "V2EdgeFinishDock.h"
#include "V2EdgeFinishTool.h"
#include "V2Ribbon.h"
#include "V2ShellSplitDock.h"
#include "V2ShellSplitTool.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/modeling/ToolController.h"

#include <QPointF>
#include <QString>

#include <optional>

namespace {

using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::DrawingTool;

struct ShotBox {
    Vector3 minimum;
    Vector3 maximum;
};

[[nodiscard]] EntityId NewestOfKind(V2MainWindow& window, EntityKind kind)
{
    EntityId newest;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kind) {
            newest = entity.id;
        }
    }
    return newest;
}

//! 上から見た画面に、道具で 2 か所を押して線を引く(矩形・線)。
[[nodiscard]] EntityId DrawTwoClicks(V2MainWindow& window, DrawingTool tool, double x0, double y0,
    double x1, double y1)
{
    auto& viewport = window.Viewport();
    const EntityId before = NewestOfKind(window, EntityKind::Wire);
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetViewCenter(Vector3{});
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(tool);
    viewport.SetSnapSuppressed(true);
    viewport.ClickAt(QPointF(viewport.width() * x0, viewport.height() * y0));
    viewport.HoverAt(QPointF(viewport.width() * x1, viewport.height() * y1));
    viewport.ClickAt(QPointF(viewport.width() * x1, viewport.height() * y1));
    viewport.SetSnapSuppressed(false);
    window.SelectTool(DrawingTool::Select);
    const EntityId after = NewestOfKind(window, EntityKind::Wire);
    return after == before ? EntityId{} : after;
}

//! その線の上(1 本目の線分の真ん中)を押す。
[[nodiscard]] bool PressCurveOf(V2MainWindow& window, const EntityId& id)
{
    auto& viewport = window.Viewport();
    for (const auto& curve : window.Session().Scene().curves) {
        if (!(curve.entityId == id)) {
            continue;
        }
        const auto screen = viewport.Mapping().Project(curve.segment.Evaluate(0.5));
        if (screen.has_value()) {
            viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool PressPoint(V2MainWindow& window, const Vector3& point)
{
    auto& viewport = window.Viewport();
    const auto screen = viewport.Mapping().Project(point);
    if (!screen.has_value()) {
        return false;
    }
    viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
    return true;
}

[[nodiscard]] std::optional<ShotBox> PartBox(V2MainWindow& window, const EntityId& id)
{
    for (const auto& shape : window.Viewport().ShapeViews()) {
        if (!shape.surface && shape.entityId == id && !shape.mesh.Empty()) {
            return ShotBox{shape.mesh.minimum, shape.mesh.maximum};
        }
    }
    return std::nullopt;
}

//! 矩形を引いて押し出した箱。上から見た画面のまま返す。
[[nodiscard]] std::optional<ShotBox> MakeBox(V2MainWindow& window, EntityId& part)
{
    const EntityId wire = DrawTwoClicks(window, DrawingTool::Rectangle, 0.40, 0.40, 0.60, 0.60);
    if (wire.IsNil() || !PressCurveOf(window, wire)) {
        return std::nullopt;
    }
    window.RunCommand("part.extrude");
    if (!window.HandleToolKey(Qt::Key_Return, nullptr)) {
        return std::nullopt;
    }
    part = NewestOfKind(window, EntityKind::Part);
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    // 丸め・シェル・分割は部品モードの道具。帯とモード表示も部品にしてから撮る。
    window.SetMode(kachakacha::v2::app::UiMode::Part);
    return PartBox(window, part);
}

void LookIsometric(V2MainWindow& window)
{
    window.Viewport().SetViewDirection(ViewDirection::Isometric);
    window.Viewport().FitToDocument();
}

//! 「ui-tool-revolve」。矩形の輪郭と軸の線を引き、回転体で両方を押して下見を出す。
[[nodiscard]] bool ApplyRevolveShot(V2MainWindow& window)
{
    const EntityId profile = DrawTwoClicks(window, DrawingTool::Rectangle, 0.60, 0.45, 0.70, 0.55);
    const EntityId axis = DrawTwoClicks(window, DrawingTool::Line, 0.50, 0.25, 0.50, 0.75);
    if (profile.IsNil() || axis.IsNil()) {
        return false;
    }
    window.SetMode(kachakacha::v2::app::UiMode::Part);
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("part.revolve");
    if (!PressCurveOf(window, profile) || !PressCurveOf(window, axis)) {
        return false;
    }
    LookIsometric(window);
    return !window.Viewport().ToolPreview().empty();
}

//! 「ui-tool-fillet」。箱の上の手前と奥の辺を押して半径 2 で丸めた下見。
[[nodiscard]] bool ApplyFilletShot(V2MainWindow& window)
{
    EntityId part;
    const auto box = MakeBox(window, part);
    if (!box.has_value()) {
        return false;
    }
    window.RunCommand("part.fillet");
    auto& dock = *window.EdgeFinishTool().Dock();
    const double x = (box->minimum.x + box->maximum.x) * 0.5;
    if (!dock.TypeSize(2.0) || !PressPoint(window, Vector3{x, box->minimum.y + 3.0, box->maximum.z})
        || !PressPoint(window, Vector3{x, box->maximum.y - 3.0, box->maximum.z})) {
        return false;
    }
    LookIsometric(window);
    return window.EdgeFinishTool().Outcome().available;
}

//! 「ui-tool-shell」。箱の上の面を押して肉厚 1 で抜いた下見。
[[nodiscard]] bool ApplyShellShot(V2MainWindow& window)
{
    EntityId part;
    const auto box = MakeBox(window, part);
    if (!box.has_value()) {
        return false;
    }
    window.RunCommand("part.shell");
    auto& dock = *window.ShellSplitTool().Dock();
    const Vector3 top{(box->minimum.x + box->maximum.x) * 0.5, (box->minimum.y + box->maximum.y) * 0.5,
        box->maximum.z};
    if (!dock.TypeThickness(1.0) || !PressPoint(window, top)) {
        return false;
    }
    LookIsometric(window);
    return window.ShellSplitTool().Outcome().available;
}

//! 「ui-tool-split」。箱を押し、作業平面を高さの半分へずらして分けた両側の下見。
[[nodiscard]] bool ApplySplitShot(V2MainWindow& window)
{
    EntityId part;
    const auto box = MakeBox(window, part);
    if (!box.has_value()) {
        return false;
    }
    window.RunCommand("part.split");
    auto& dock = *window.ShellSplitTool().Dock();
    const Vector3 top{(box->minimum.x + box->maximum.x) * 0.5, (box->minimum.y + box->maximum.y) * 0.5,
        box->maximum.z};
    const double middle = (box->minimum.z + box->maximum.z) * 0.5;
    if (!PressPoint(window, top)
        || !dock.TypeOffset(middle - window.Viewport().WorkPlane().origin.z)) {
        return false;
    }
    LookIsometric(window);
    return window.ShellSplitTool().Outcome().available;
}

//! 「ui-tool-area」。矩形を引いて 1 辺を押し、帯の 測定 → 面積 で囲む面積を出す。
[[nodiscard]] bool ApplyAreaShot(V2MainWindow& window)
{
    const EntityId wire = DrawTwoClicks(window, DrawingTool::Rectangle, 0.35, 0.35, 0.65, 0.60);
    if (wire.IsNil() || !PressCurveOf(window, wire)) {
        return false;
    }
    auto& ribbon = window.Ribbon();
    return ribbon.ClickCategory(QStringLiteral("測定")) && ribbon.ClickTool(QStringLiteral("面積"));
}

} // namespace

bool V2MainWindow::ApplyToolShotState(const QString& name)
{
    if (name == QStringLiteral("ui-tool-revolve")) {
        return ApplyRevolveShot(*this);
    }
    if (name == QStringLiteral("ui-tool-fillet")) {
        return ApplyFilletShot(*this);
    }
    if (name == QStringLiteral("ui-tool-shell")) {
        return ApplyShellShot(*this);
    }
    if (name == QStringLiteral("ui-tool-split")) {
        return ApplySplitShot(*this);
    }
    if (name == QStringLiteral("ui-tool-area")) {
        return ApplyAreaShot(*this);
    }
    return false;
}
