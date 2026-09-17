//! 正本と見比べる場面の続き(引継ぎ 2026-09-17 の 7)。
//!
//! 案内付きロフト・近似・足す引くの、Windows 実描画の証拠を撮るための場面。
//! **写真を撮るための場面づくりであり、試験ではない。**人の道の確かめは
//! V2SelfTestHumanPath*.cpp が持つ。場面を作る道は、できるだけ人と同じにする
//! (道具を持って押す・命令を走らせる)。断面のように手で引くのが難しいものだけ、
//! 線を直に足して用意する。

#include "V2MainWindow.h"

#include "V2SurfaceDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/app/SurfaceInputState.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#include <QPointF>
#include <QString>

#include <vector>

using kachakacha::v2::base::EntityId;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::GuideSurfaceMethod;

namespace {

//! 折れ線。点をつないで線分にする。
[[nodiscard]] std::vector<CurveSegment> PolylineThrough(const std::vector<Vector3>& points)
{
    std::vector<CurveSegment> segments;
    for (std::size_t index = 1; index < points.size(); ++index) {
        const auto made = CurveSegment::MakeLine(points[index - 1], points[index]);
        if (made.HasValue()) {
            segments.push_back(made.Value());
        }
    }
    return segments;
}

//! 四角い輪郭。中心 (cx, 0, z)、半分の幅 half。
[[nodiscard]] std::vector<CurveSegment> RectangleAround(double cx, double z, double half)
{
    return PolylineThrough({{cx - half, -half, z}, {cx + half, -half, z},
        {cx + half, half, z}, {cx - half, half, z}, {cx - half, -half, z}});
}

} // namespace

//! 立体の塗りの真ん中を、上から素のクリックで押す(人と同じ道)。
bool V2MainWindow::PickShapeCenterForShot(const EntityId& id)
{
    viewport_->SetViewDirection(ViewDirection::Top);
    viewport_->FitToDocument();
    for (const auto& shape : viewport_->ShapeViews()) {
        if (!(shape.entityId == id) || shape.mesh.Empty()) {
            continue;
        }
        const Vector3 center{(shape.mesh.minimum.x + shape.mesh.maximum.x) * 0.5,
            (shape.mesh.minimum.y + shape.mesh.maximum.y) * 0.5, shape.mesh.maximum.z};
        const auto screen = viewport_->Mapping().Project(center);
        if (!screen.has_value()) {
            return false;
        }
        viewport_->SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
        return true;
    }
    return false;
}

//! 08 案内付きロフト。断面3枚と、角をなぞるガイド1本。
bool V2MainWindow::ApplyGuidedLoftShotState()
{
    std::vector<EntityId> sections;
    const double heights[3] = {0.0, 25.0, 50.0};
    const double halves[3] = {40.0, 30.0, 16.0};
    const char* const labels[3] = {"断面1", "断面2", "断面3"};
    for (int index = 0; index < 3; ++index) {
        const auto id = AddPlainWire(RectangleAround(0.0, heights[index], halves[index]),
            labels[index]);
        if (id.IsNil()) {
            return false;
        }
        sections.push_back(id);
    }
    const auto guide = AddPlainWire(
        PolylineThrough({{-40.0, -40.0, 0.0}, {-30.0, -30.0, 25.0}, {-16.0, -16.0, 50.0}}),
        "ガイド");
    if (guide.IsNil()) {
        return false;
    }
    AdoptCurrentDocument();
    viewport_->SetViewDirection(ViewDirection::Isometric);
    viewport_->FitToDocument();
    kachakacha::v2::app::SelectionSet all;
    all.entityIds = sections;
    viewport_->SetSelection(all);
    RunCommand("surface.create");
    if (!surfaceShelfShown_ || !surfaceDock_->ClickMethodCard(GuideSurfaceMethod::GuidedLoft)
        || !surfaceDock_->ClickActivate(ChainRole::GuideU)) {
        return false;
    }
    // ガイドは 3D で押す(欄が「ここへ選ぶ」の状態)。
    for (const auto& curve : session_->Scene().curves) {
        if (!(curve.entityId == guide)) {
            continue;
        }
        const auto screen = viewport_->Mapping().Project(curve.segment.Evaluate(0.5));
        if (screen.has_value()) {
            viewport_->SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
            break;
        }
    }
    return surfaceInput_.guides.size() == 1;
}

//! 09 近似。平らな面を作ってから、近似を押し、3D で面を押して候補を並べる。
bool V2MainWindow::ApplyApproxShotState()
{
    DrawRectangleForShot();
    if (!PickAnyCurveForShot()) {
        return false;
    }
    RunCommand("surface.create");
    if (!surfaceShelfShown_) {
        return false;
    }
    ConfirmSurface();
    EntityId surface;
    for (const auto& entity : session_->GetDocument().Snapshot().entities) {
        if (entity.kind == kachakacha::v2::domain::EntityKind::GuideSurface) {
            surface = entity.id;
        }
    }
    if (surface.IsNil()) {
        return false;
    }
    viewport_->SetSelection(kachakacha::v2::app::SelectionSet{});
    RunCommand("fabrication.create");
    if (!approxShelfShown_ || !PickShapeCenterForShot(surface)) {
        return false;
    }
    viewport_->SetViewDirection(ViewDirection::Isometric);
    viewport_->FitToDocument();
    return approxInput_.sources.size() == 1;
}

//! 10 足す。重なる箱を2つ作り、足すを押し、土台 → 相手の順に 3D で押して下見を出す。
bool V2MainWindow::ApplyBooleanShotState()
{
    std::vector<EntityId> parts;
    for (const double cx : {-20.0, 20.0}) {
        const auto wire = AddPlainWire(RectangleAround(cx, 0.0, 30.0), "輪郭");
        if (wire.IsNil()) {
            return false;
        }
        AdoptCurrentDocument();
        kachakacha::v2::app::SelectionSet one;
        one.entityIds.push_back(wire);
        viewport_->SetSelection(one);
        RunCommand("part.extrude");   // 一度目は下見
        RunCommand("part.extrude");   // 二度目で確定
        EntityId newest;
        for (const auto& entity : session_->GetDocument().Snapshot().entities) {
            if (entity.kind == kachakacha::v2::domain::EntityKind::Part) {
                newest = entity.id;
            }
        }
        if (newest.IsNil()) {
            return false;
        }
        parts.push_back(newest);
    }
    viewport_->SetSelection(kachakacha::v2::app::SelectionSet{});
    RunCommand("part.boolean_add");
    if (!booleanShelfShown_ || !PickShapeCenterForShot(parts[0])
        || !PickShapeCenterForShot(parts[1])) {
        return false;
    }
    viewport_->SetViewDirection(ViewDirection::Isometric);
    viewport_->FitToDocument();
    return booleanBuilt_.has_value();
}
