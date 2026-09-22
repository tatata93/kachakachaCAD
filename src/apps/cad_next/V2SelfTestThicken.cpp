//! 「厚み」の人の道(HP-TH、指示書 matrix P-10)。
//!
//! 厚みを押す → 面待ち → 3D で形状ガイドの面を押すと入る(押し直すと外れる) →
//! 作り方(外側/中央/内側)を選ぶ → Enter で確定、1回の取り消しで消える。
//! Esc でやめれば何も作らない。
//!
//! 選ぶのは実際に拾う道(`SelectAt` 経由の `ClickOnAnyGuideSurface`)だけ。
//! ID の注入も、見えない widget を叩くこともしない。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2ThickenDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/ThickenInputState.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/fabrication/FabricationSettings.h"

#include <QPointF>
#include <QString>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::Shelf;
using kachakacha::v2::app::UiMode;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::fabrication::ThicknessPlacement;

//! 何も無いところから、矩形 → 境界を拾う → 面を確定、まで(HumanPathApprox と同じ下ごしらえ)。
[[nodiscard]] bool MakeFreshGuideSurface(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))
        || !Explain("境界を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.RunCommand("surface.create");
    return Explain("面の下見が見える", window.SurfacePreviewShown())
        && Explain("Enterで面を確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        && Explain("形状ガイドができる", CountOfKind(window, EntityKind::GuideSurface) == 1);
}

//! 面を作ったあと、選択を外して部品モードへ、「厚み」を構えるところまで。
[[nodiscard]] bool ArmThickenOnFreshSurface(V2MainWindow& window)
{
    if (!MakeFreshGuideSurface(window)) {
        return false;
    }
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);   // 空所を押して選択を外す
    window.SetMode(UiMode::Part);
    window.RunCommand("part.thicken");
    return Explain("厚みを押すと棚が構える",
               window.ThickenShelfShown() && window.ShelfShown(Shelf::Thicken))
        && Explain("構えただけでは部品は増えない",
            CountOfKind(window, EntityKind::Part) == 0);
}

//! HP-TH-01。面が3Dで拾えて欄に入り、作り方を選んで Enter すると部品になり、
//! 1回の取り消しで消える。
[[nodiscard]] bool CaseHumanPathThickenPickPlacementConfirm(V2MainWindow& window)
{
    if (!ArmThickenOnFreshSurface(window)) {
        return false;
    }
    if (!Explain("構えてから面を画面で拾える", ClickOnAnyGuideSurface(window))
        || !Explain("面の欄に入る", !window.ThickenInput().surfaces.empty())
        || !Explain("欄に名前が出る",
            window.ThickenDock().SurfaceTextJa() != QStringLiteral("(選んでいません)"))) {
        return false;
    }
    bool sawSurface = false;
    for (const auto& label : window.Viewport().ToolRoleLabels()) {
        sawSurface = sawSurface || label.text == QStringLiteral("SURFACE");
    }
    if (!Explain("3D に SURFACE の札が出る", sawSurface)) {
        return false;
    }
    if (!Explain("「内側」のカードを押せる",
            window.ThickenDock().ClickPlacementCard(ThicknessPlacement::Inside))
        || !Explain("作り方が内側になる、平面までではない",
            window.ThickenInput().placement == ThicknessPlacement::Inside
                && !window.ThickenInput().toPlane)) {
        return false;
    }
    if (!Explain("Enterで確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("部品が1つできる", CountOfKind(window, EntityKind::Part) == 1)
        || !Explain("確定すると棚が引っ込む",
            !window.ThickenShelfShown() && !window.ShelfShown(Shelf::Thicken))) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1回の取り消しで部品が消える", CountOfKind(window, EntityKind::Part) == 0);
}

//! HP-TH-02。Esc は何も作らずにやめられ、同じ面をもう一度押すと欄が空になる。
[[nodiscard]] bool CaseHumanPathThickenEscAndRepick(V2MainWindow& window)
{
    if (!ArmThickenOnFreshSurface(window)) {
        return false;
    }
    if (!Explain("構えてから面を画面で拾える", ClickOnAnyGuideSurface(window))
        || !Explain("面の欄に入る", !window.ThickenInput().surfaces.empty())) {
        return false;
    }
    if (!Explain("Escでやめられる", window.HandleToolKey(Qt::Key_Escape, nullptr))
        || !Explain("やめると構えが解ける", !window.ThickenShelfShown())
        || !Explain("何も作っていない", CountOfKind(window, EntityKind::Part) == 0)) {
        return false;
    }
    // もう一度構えて、同じ面を押すと空になる(3D で押し直すと外れる)。
    window.RunCommand("part.thicken");
    if (!Explain("もう一度構えられる", window.ThickenShelfShown())
        || !Explain("同じ面をまた拾える", ClickOnAnyGuideSurface(window))
        || !Explain("面の欄に入る", !window.ThickenInput().surfaces.empty())) {
        return false;
    }
    // 押し直すと外れるので、ClickOnAnyGuideSurface は「選択に残っているか」で偽になる。
    // ここでは戻り値を見ず、欄が空になったことだけを見る。
    (void)ClickOnAnyGuideSurface(window);
    return Explain("同じ面を押し直すと欄が空になる", window.ThickenInput().surfaces.empty());
}

//! その線から平面の面を作る(線を拾う → 面を作る → Enter)。
[[nodiscard]] bool PlaneFromWire(V2MainWindow& window, const kachakacha::v2::base::EntityId& wire)
{
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);
    if (!ClickOnCurveOf(window, wire)) {
        return false;
    }
    window.RunCommand("surface.create");
    return window.SurfacePreviewShown() && window.HandleToolKey(Qt::Key_Return, nullptr);
}

//! その面の塗りの真ん中を実際に押す。
[[nodiscard]] bool ClickSurfaceMiddle(V2MainWindow& window, const kachakacha::v2::base::EntityId& id)
{
    auto& viewport = window.Viewport();
    for (const auto& shape : viewport.ShapeViews()) {
        if (shape.entityId != id || shape.mesh.Empty()) {
            continue;
        }
        const auto screen = viewport.Mapping().Project((shape.mesh.minimum + shape.mesh.maximum) * 0.5);
        if (!screen.has_value()) {
            return false;
        }
        viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
        return true;
    }
    return false;
}

//! HP-TH-03。面を 2 枚押すと両方が欄に入り、Enter で部品が 2 個でき、1 回の取り消しで両方消える。
[[nodiscard]] bool CaseHumanPathThickenManySurfaces(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const auto left = DrawRectangleAtByHand(window, 0.15, 0.35, 0.40, 0.65);
    const auto right = DrawRectangleAtByHand(window, 0.60, 0.35, 0.85, 0.65);
    if (!Explain("矩形を 2 つ引ける", !left.IsNil() && !right.IsNil())
        || !Explain("面が 2 枚できる", PlaneFromWire(window, left) && PlaneFromWire(window, right)
                && CountOfKind(window, EntityKind::GuideSurface) == 2)) {
        return false;
    }
    const auto surfaces = kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::GuideSurface).entityIds;
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);
    window.SetMode(UiMode::Part);
    window.RunCommand("part.thicken");
    if (!Explain("厚みを押すと棚が構える", window.ThickenShelfShown())
        || !Explain("1 枚目を押せる", ClickSurfaceMiddle(window, surfaces[0]))
        || !Explain("2 枚目を押せる", ClickSurfaceMiddle(window, surfaces[1]))
        || !Explain("2 枚とも欄に入る(面は 1 枚に限らない)",
            window.ThickenInput().surfaces.size() == 2)) {
        return false;
    }
    if (!Explain("Enterで確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("部品が 2 個できる", CountOfKind(window, EntityKind::Part) == 2)) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1 回の取り消しで 2 個とも消える", CountOfKind(window, EntityKind::Part) == 0);
}

} // namespace

std::vector<SelfTestCase> ThickenCases()
{
    return {
        {"HP-TH-03 面を 2 枚押すと両方に厚みが付き、1 回の取り消しで両方消える",
            CaseHumanPathThickenManySurfaces},
        {"HP-TH-01 面を3Dで拾い、作り方を選んで Enter すると部品になり、取り消しで消える",
            CaseHumanPathThickenPickPlacementConfirm},
        {"HP-TH-02 Esc は何も作らずにやめられ、同じ面を押し直すと欄が空になる",
            CaseHumanPathThickenEscAndRepick},
    };
}

} // namespace kachakacha::v2::selftest
