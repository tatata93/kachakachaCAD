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
#include "kachakacha/geometry/ArcBuilders.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#include <QPointF>
#include <QString>

#include <string>
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

//! 08 案内付きロフト(多レール)。先が細る開いた断面 3 本と、ガイド 3 本
//! (両脇の裾 2 本 + 頂の筋 1 本)。前頭部の形。外側が両脇にあるので断面とガイドの網で作る。
//! 2026-09-22 まで閉じた四角 3 枚 + 角のガイド 1 本だった。閉じた断面にガイドを付けた
//! ロフトは作れない(検査が断る)ので、棚には断る言葉しか写っていなかった。
bool V2MainWindow::ApplyGuidedLoftShotState()
{
    std::vector<EntityId> sections;
    const double xs[3] = {0.0, 40.0, 80.0};
    const double halves[3] = {30.0, 24.0, 14.0};
    const double tops[3] = {20.0, 16.0, 8.0};
    for (int index = 0; index < 3; ++index) {
        const double lift = tops[index] / 0.75;   // 3 次ベジェの山は制御点の高さの 3/4
        const auto arch = CurveSegment::MakeCubicBezier({Vector3{xs[index], -halves[index], 0.0},
            Vector3{xs[index], -halves[index], lift}, Vector3{xs[index], halves[index], lift},
            Vector3{xs[index], halves[index], 0.0}});
        const auto id = arch.HasValue()
            ? AddPlainWire({arch.Value()}, ("断面" + std::to_string(index + 1)).c_str())
            : EntityId{};
        if (id.IsNil()) {
            return false;
        }
        sections.push_back(id);
    }
    std::vector<EntityId> guides;
    const auto through = [&](double side, const char* label) {
        const auto arc = kachakacha::v2::geometry::ArcThroughThreePoints(
            {xs[0], side * halves[0], side == 0.0 ? tops[0] : 0.0},
            {xs[1], side * halves[1], side == 0.0 ? tops[1] : 0.0},
            {xs[2], side * halves[2], side == 0.0 ? tops[2] : 0.0});
        const auto id = arc.HasValue() ? AddPlainWire({arc.Value()}, label) : EntityId{};
        if (!id.IsNil()) {
            guides.push_back(id);
        }
    };
    through(-1.0, "ガイド(裾)");
    through(0.0, "ガイド(頂)");
    through(1.0, "ガイド(裾)");
    if (guides.size() != 3) {
        return false;
    }
    AdoptCurrentDocument();
    viewport_->SetViewDirection(ViewDirection::Isometric);
    viewport_->FitToDocument();
    kachakacha::v2::app::SelectionSet all;
    all.entityIds = sections;
    viewport_->SetSelection(all);
    RunCommand("surface.create");
    // ガイド付きロフトはロフト面へ統合した(互換の入口は「その他」)。ロフト面のカードでガイドの欄を使う。
    if (!surfaceShelfShown_ || !surfaceDock_->ClickMethodCard(GuideSurfaceMethod::LoftSections)
        || !surfaceDock_->ClickActivate(ChainRole::GuideU)) {
        return false;
    }
    // ガイドは 3D で押す(欄が「ここへ選ぶ」の状態)。断面と重ならない、断面の間の点を押す。
    for (const EntityId& guide : guides) {
        for (const auto& curve : session_->Scene().curves) {
            if (!(curve.entityId == guide)) {
                continue;
            }
            const auto screen = viewport_->Mapping().Project(curve.segment.Evaluate(0.25));
            if (screen.has_value()) {
                viewport_->SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
            }
            break;
        }
    }
    return surfaceInput_.guides.size() == 3;
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
