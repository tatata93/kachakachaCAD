//! 撮影の5場面(指示書 I-02、1280x720/1920x1080/2560x1440 × 100/125/150%)。
//!
//! V2UiShotStates.cpp と同じ考え方: **写真を撮るための場面づくりであり、試験ではない。**
//! 人の道の確かめは V2SelfTestResponsive.cpp が持つ。場面を作る道は、できるだけ人と
//! 同じにする(帯を押す・道具を持って画面を押す・命令を走らせる)。

#include "V2MainWindow.h"

#include "V2FabricationDock.h"
#include "V2Ribbon.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/modeling/ToolController.h"

#include <QPointF>
#include <QString>

namespace {

using kachakacha::v2::app::SelectAllOfKind;
using kachakacha::v2::app::SelectionSet;
using kachakacha::v2::app::UiMode;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::DrawingTool;

//! 画面に見えている形状ガイドの塗りのどれかを、実際に拾う(V2SelfTestHumanPath.cpp と同じ道)。
[[nodiscard]] bool PickAnyGuideSurfaceForShot(V2MainWindow& window)
{
    auto& viewport = window.Viewport();
    for (const auto& shape : viewport.ShapeViews()) {
        if (!shape.surface || shape.mesh.Empty()) {
            continue;
        }
        const Vector3 center{(shape.mesh.minimum.x + shape.mesh.maximum.x) * 0.5,
            (shape.mesh.minimum.y + shape.mesh.maximum.y) * 0.5,
            (shape.mesh.minimum.z + shape.mesh.maximum.z) * 0.5};
        const auto screen = viewport.Mapping().Project(center);
        if (!screen.has_value()
            || !viewport.PickShapeAt(QPointF(screen->x, screen->y)).has_value()) {
            continue;
        }
        viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
        return true;
    }
    return false;
}

//! 01「ui-ribbon-drawing」。矩形を引いてから、帯で円弧を持ち、曲線カテゴリを前に出す。
//! カテゴリを切り替えても、持っている道具(円弧)は変わらない(帯の仕組みどおり)。
[[nodiscard]] bool ApplyRibbonDrawingShot(V2MainWindow& window)
{
    window.SetMode(UiMode::Drawing);
    if (!window.DrawRectangleForShot()) {
        return false;
    }
    auto& ribbon = window.Ribbon();
    if (!ribbon.ClickTool(QStringLiteral("円弧"))) {
        return false;
    }
    if (!ribbon.ClickCategory(QStringLiteral("曲線"))) {
        return false;
    }
    return window.Session().CurrentTool() == DrawingTool::Arc;
}

//! 02「ui-ribbon-part-thicken」。面を1枚作ってから部品モードで「厚み」を構え、面を拾う。
[[nodiscard]] bool ApplyRibbonThickenShot(V2MainWindow& window)
{
    if (!window.DrawRectangleForShot() || !window.PickAnyCurveForShot()) {
        return false;
    }
    window.RunCommand("surface.create");
    if (!window.SurfacePreviewShown() || !window.HandleToolKey(Qt::Key_Return, nullptr)) {
        return false;
    }
    window.Viewport().SetSelection(SelectionSet{});   // 空所を押して選択を外すのと同じ
    window.SetMode(UiMode::Part);
    window.RunCommand("part.thicken");
    if (!window.ThickenShelfShown()) {
        return false;
    }
    if (!PickAnyGuideSurfaceForShot(window)) {
        return false;
    }
    window.Viewport().SetViewDirection(ViewDirection::Isometric);
    window.Viewport().FitToDocument();
    return !window.ThickenInput().surfaces.empty();
}

//! 03「ui-fab-generate」。近似候補までは ui-approx-candidates と同じ場面。確定して
//! 「2 部材の編集・曲げ確認」(生成のカードがある段)へ切り替える。
[[nodiscard]] bool ApplyFabricationGenerateShot(V2MainWindow& window)
{
    if (!window.ApplyManualState(QStringLiteral("ui-approx-candidates"))) {
        return false;
    }
    if (!window.HandleToolKey(Qt::Key_Return, nullptr) || window.FabricationModelCount() != 1) {
        return false;
    }
    window.Viewport().SetSelection(
        SelectAllOfKind(window.Session().GetDocument().Snapshot(), EntityKind::FabricationModel));
    window.FabricationDock().SetStageIndex(1);
    window.Viewport().SetViewDirection(ViewDirection::Isometric);
    window.Viewport().FitToDocument();
    return window.FabricationDock().StageIndex() == 1;
}

//! 線を3本、上から見て横に並べて引く。
void DrawThreeLinesForShot(V2MainWindow& window)
{
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(200.0);
    const double rows[3] = {0.30, 0.50, 0.70};
    for (const double y : rows) {
        window.SelectTool(DrawingTool::Line);
        viewport.ClickAt(QPointF(viewport.width() * 0.3, viewport.height() * y));
        viewport.HoverAt(QPointF(viewport.width() * 0.7, viewport.height() * y));
        viewport.ClickAt(QPointF(viewport.width() * 0.7, viewport.height() * y));
    }
    window.SelectTool(DrawingTool::Select);
}

//! 04「ui-explorer-groups」。線を3本引いて2本をまとめ、3D で1本選んで木の色付けを見せる。
[[nodiscard]] bool ApplyExplorerGroupsShot(V2MainWindow& window)
{
    DrawThreeLinesForShot(window);
    auto& viewport = window.Viewport();
    int picked = 0;
    for (const auto& curve : window.Session().Scene().curves) {
        if (picked >= 2) {
            break;
        }
        const auto screen = viewport.Mapping().Project(curve.segment.Evaluate(0.5));
        if (!screen.has_value()) {
            continue;
        }
        viewport.SelectAt(QPointF(screen->x, screen->y),
            picked == 0 ? Qt::NoModifier : Qt::ControlModifier);
        ++picked;
    }
    if (picked != 2) {
        return false;
    }
    window.RunCommand("group.create");
    if (window.GroupItems().empty()) {
        return false;
    }
    // 3D で1本を選び直す。一覧の色付けが選択と同期して見える(§7〜13)。
    for (const auto& curve : window.Session().Scene().curves) {
        const auto screen = viewport.Mapping().Project(curve.segment.Evaluate(0.5));
        if (screen.has_value()) {
            viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
            break;
        }
    }
    viewport.FitToDocument();
    return window.Session().Scene().curves.size() == 3;
}

//! 05「ui-measure-overlay」。線の道具のまま測定を重ねる場面(HP-ST-02 と同じ道)。
[[nodiscard]] bool ApplyMeasureOverlayShot(V2MainWindow& window)
{
    window.SetMode(UiMode::Drawing);
    window.SelectTool(DrawingTool::Line);
    window.RunCommand("measure.open");
    return window.Session().CurrentTool() == DrawingTool::Measure;
}

} // namespace

bool V2MainWindow::ApplyResponsiveShotState(const QString& name)
{
    if (name == QStringLiteral("ui-ribbon-drawing")) {
        return ApplyRibbonDrawingShot(*this);
    }
    if (name == QStringLiteral("ui-ribbon-part-thicken")) {
        return ApplyRibbonThickenShot(*this);
    }
    if (name == QStringLiteral("ui-fab-generate")) {
        return ApplyFabricationGenerateShot(*this);
    }
    if (name == QStringLiteral("ui-explorer-groups")) {
        return ApplyExplorerGroupsShot(*this);
    }
    if (name == QStringLiteral("ui-measure-overlay")) {
        return ApplyMeasureOverlayShot(*this);
    }
    return false;
}
