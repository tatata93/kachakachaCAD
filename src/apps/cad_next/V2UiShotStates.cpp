//! 正本(`kachakachaCAD_extrude_surface_UI_final_mock.html`)と見比べるための
//! 画面の状態(オーナー指示 2026-09-15 §20)。
//!
//! ここで作るのは **写真を撮るための場面** である。試験ではない。
//! 人の道の確かめは V2SelfTestHumanPath.cpp が持つ。
//! 場面を作る道は、できるだけ人と同じにする(道具を持って押す・命令を走らせる)。
//! 断面のように手で引くのが難しいものだけ、線を直に足して用意する。

#include "V2MainWindow.h"

#include "V2SurfaceDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/ExtrudeInputState.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/SurfaceInputState.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/modeling/ToolController.h"

#include <QApplication>
#include <QPointF>
#include <QString>

#include <vector>

using kachakacha::v2::app::SurfaceOrdering;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::DrawingTool;
using kachakacha::v2::modeling::GuideSurfaceMethod;

namespace {

//! 四角い輪郭を1本。`z` の高さに、半分の幅 `half` で。
[[nodiscard]] std::vector<CurveSegment> RectangleAt(double z, double half)
{
    const Vector3 a{-half, -half, z};
    const Vector3 b{half, -half, z};
    const Vector3 c{half, half, z};
    const Vector3 d{-half, half, z};
    std::vector<CurveSegment> segments;
    const Vector3 points[5] = {a, b, c, d, a};
    for (int index = 0; index < 4; ++index) {
        const auto made = CurveSegment::MakeLine(points[index], points[index + 1]);
        if (made.HasValue()) {
            segments.push_back(made.Value());
        }
    }
    return segments;
}

} // namespace

//! 画面から線を1本拾う。人と同じ道(画面を押す)で選ぶ。
bool V2MainWindow::PickAnyCurveForShot()
{
    for (const auto& curve : session_->Scene().curves) {
        const auto screen = viewport_->Mapping().Project(curve.segment.Evaluate(0.5));
        if (!screen.has_value()) {
            continue;
        }
        viewport_->SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
        if (!viewport_->Selection().entityIds.empty()) {
            return true;
        }
    }
    return false;
}

//! 上から見た画面に、矩形を1つ手で引く。
bool V2MainWindow::DrawRectangleForShot()
{
    viewport_->SetViewDirection(ViewDirection::Top);
    viewport_->SetViewCenter(Vector3{});
    viewport_->SetVisibleWidthMm(200.0);
    SelectTool(DrawingTool::Rectangle);
    viewport_->SetSnapSuppressed(true);
    viewport_->ClickAt(QPointF(viewport_->width() * 0.35, viewport_->height() * 0.35));
    viewport_->HoverAt(QPointF(viewport_->width() * 0.65, viewport_->height() * 0.65));
    viewport_->ClickAt(QPointF(viewport_->width() * 0.65, viewport_->height() * 0.65));
    viewport_->SetSnapSuppressed(false);
    SelectTool(DrawingTool::Select);
    return true;
}

//! 押し出しの場面(01〜03)。
bool V2MainWindow::ApplyExtrudeShotState(const QString& name)
{
    DrawRectangleForShot();
    if (!PickAnyCurveForShot()) {
        return false;
    }
    SetMode(kachakacha::v2::app::UiMode::Part);
    viewport_->SetViewDirection(ViewDirection::Isometric);
    viewport_->FitToDocument();
    RunCommand("part.extrude");
    if (name.endsWith(QStringLiteral("profile-only"))) {
        return viewport_->ExtrudeHandleShown();
    }
    if (name.endsWith(QStringLiteral("outputs"))) {
        // 出力を「ワイヤー+ソリッド」にした場面。4つの欄が全部見える。
        extrudeDock_->ShowOutputs(kachakacha::v2::app::OutputsForPreset(
            kachakacha::v2::app::ExtrudeOutputPreset::WiresAndSolid));
        RefreshExtrudeFromDock();
        return true;
    }
    // with-target: 立体を作ってから、輪郭と立体の両方を選んで「引く」にする。
    RunCommand("part.extrude");   // いったん確定して相手を作る
    DrawRectangleForShot();
    viewport_->SetSelection(kachakacha::v2::app::SelectionSet{});
    RunCommand("part.extrude");   // 構える
    if (!PickAnyCurveForShot()) {
        return false;
    }
    viewport_->SelectAt(QPointF(viewport_->width() * 0.5, viewport_->height() * 0.5),
        Qt::NoModifier);
    RunCommand("part.extrude");
    if (!viewport_->ExtrudeHandleShown()) {
        return false;
    }
    extrudeDock_->ChooseBoolean(kachakacha::v2::modeling::ExtrudeBooleanMode::SubtractFromPart);
    RefreshExtrudeFromDock();
    // 2枚目の矩形を引いたところで上から見に戻っている。斜めから見せる。
    viewport_->SetViewDirection(ViewDirection::Isometric);
    viewport_->FitToDocument();
    return true;
}

//! 「面を作る」の場面(04〜07)。
//!
//! 断面は手で引くのが難しいので、高さ違いの矩形を線として直に足す。
//! **撮るための場面づくりであり、人の道の試験ではない。**
bool V2MainWindow::ApplySurfaceShotState(const QString& name)
{
    if (name.endsWith(QStringLiteral("recommend"))) {
        // 閉じた同一平面の輪郭1本。正本の「選択内容から推奨しています」の場面。
        DrawRectangleForShot();
        if (!PickAnyCurveForShot()) {
            return false;
        }
        viewport_->SetViewDirection(ViewDirection::Isometric);
        viewport_->FitToDocument();
        RunCommand("surface.create");
        return surfaceShelfShown_;
    }
    // 断面3枚。ロフトの場面(05〜07)。
    std::vector<kachakacha::v2::base::EntityId> sections;
    const double heights[3] = {0.0, 25.0, 50.0};
    const double halves[3] = {40.0, 30.0, 16.0};
    const char* const labels[3] = {"断面1", "断面2", "断面3"};
    for (int index = 0; index < 3; ++index) {
        const auto id = AddPlainWire(RectangleAt(heights[index], halves[index]),
            labels[index]);
        if (id.IsNil()) {
            return false;
        }
        sections.push_back(id);
    }
    // **足しただけでは画面に出ない。**場面づくりでも、ふだんの道を通す。
    AdoptCurrentDocument();
    viewport_->SetViewDirection(ViewDirection::Isometric);
    viewport_->FitToDocument();
    kachakacha::v2::app::SelectionSet all;
    all.entityIds = sections;
    viewport_->SetSelection(all);
    RunCommand("surface.create");
    if (!surfaceShelfShown_) {
        return false;
    }
    if (name.endsWith(QStringLiteral("method"))) {
        // 作り方を「ルールド」へ変えた場面。入れたものが残っているのが見える。
        return surfaceDock_->ClickMethodCard(GuideSurfaceMethod::RuledSections);
    }
    if (name.endsWith(QStringLiteral("manual-order"))) {
        // 断面順を手動固定にして、2番目を上へ動かした場面。
        ChooseSurfaceOrdering(SurfaceOrdering::ManualLock);
        MoveSurfaceSection(1, 0);
        return true;
    }
    return true;   // preview: そのまま下見が出ている
}

//! 正本と見比べる場面か。名前は `ui-` で始める。
bool V2MainWindow::ApplyUiShotState(const QString& name)
{
    bool ok = false;
    if (name.startsWith(QStringLiteral("ui-extrude-"))) {
        ok = ApplyExtrudeShotState(name);
    } else if (name.startsWith(QStringLiteral("ui-surface-"))) {
        ok = ApplySurfaceShotState(name);
    } else {
        return false;
    }
    // 撮る直前に、棚の前後をもう一度決める。
    // 途中で別の棚が前に出ていると、見比べる絵にならない。
    RefreshRightShelves();
    QApplication::processEvents();
    return ok;
}
