//! 部品モードの押し出しの欄(HP-PA)。正本 part mock(2026-09-18、Inventor 風)、指示書 P-02〜P-07。
//!
//! 範囲(距離/対称/非対称/面まで/貫通)と方向(7通り)が **棚に全部** 出ていて、選ぶと
//! 要る欄だけが生え、その値が作る形(ExtrudeChoice)へ届くこと。
//! 核に無い From とテーパーは押せない形で理由が出ていること。

#include "V2SelfTest.h"

#include "V2ExtrudeDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/modeling/ExtrudeInput.h"

#include <QString>
#include <QComboBox>
#include <QPointF>
#include "kachakacha/app/Selection.h"
#include "kachakacha/modeling/ToolController.h"

#include <cmath>
#include <variant>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::Shelf;
using kachakacha::v2::app::UiMode;
using kachakacha::v2::modeling::ExtrudeDirectionMode;
using kachakacha::v2::modeling::ExtrudeExtentMode;

//! 矩形を引き、拾って、押し出しを構えるところまで。
[[nodiscard]] bool ArmExtrude(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))
        || !Explain("引いた線を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.SetMode(UiMode::Part);
    window.RunCommand("part.extrude");
    return Explain("押し出しの棚が構える",
        window.Viewport().ExtrudeHandleShown() && window.ShelfShown(Shelf::Extrude));
}

//! HP-PA-01。範囲 5 通り・方向 7 通りが棚に並び、選ぶと要る欄だけが生えて、作る形へ届く。
[[nodiscard]] bool CaseExtrudeShelfListsAllExtentsAndDirections(V2MainWindow& window)
{
    if (!ArmExtrude(window)) {
        return false;
    }
    auto& dock = window.ExtrudeDock();
    if (!Explain((std::string("範囲は 5 通り(実際 ") + std::to_string(dock.ExtentLabels().size()) + ")").c_str(),
            dock.ExtentLabels().size() == 5)
        || !Explain((std::string("方向は 7 通り(実際 ") + std::to_string(dock.DirectionLabels().size()) + ")").c_str(),
            dock.DirectionLabels().size() == 7)
        || !Explain("最初は距離で、逆側の距離も相手も出ていない",
            dock.ExtentMode() == ExtrudeExtentMode::Distance && !dock.SecondDistanceShown()
                && !dock.TargetRowShown())) {
        return false;
    }
    // 非対称(両方向に別々の距離)を選ぶと逆側の距離が生え、作る形へ届く。
    // **人が選んだのと同じ道**(combo の signal を通す)で選ぶ。映すだけの ChooseExtent で
    // 選んで RefreshExtrudeFromDock を手で呼ぶと、配線が切れていても気づけない。
    if (!Explain("棚で「両方向に別々」を選べる", dock.PickExtent(ExtrudeExtentMode::TwoDistances))) {
        return false;
    }
    if (!Explain("選んだだけで作る形の範囲が変わる(棚の配線が生きている)",
            window.ExtrudeChoice().extent == ExtrudeExtentMode::TwoDistances)) {
        return false;
    }
    dock.SetSecondDistanceMm(3.0);
    window.RefreshExtrudeFromDock();
    if (!Explain("非対称では逆側の距離の欄が生える", dock.SecondDistanceShown())
        || !Explain("作る形の範囲が非対称", window.ExtrudeChoice().extent == ExtrudeExtentMode::TwoDistances)
        || !Explain("逆側の距離が届く", std::abs(window.ExtrudeChoice().secondDistanceMm - 3.0) < 1.0e-9)) {
        return false;
    }
    // 面までを選ぶと相手の欄が生える(作業平面が無ければその旨)。
    dock.ChooseExtent(ExtrudeExtentMode::ToTarget);
    window.RefreshExtrudeFromDock();
    if (!Explain("面までは相手の欄が生える", dock.TargetRowShown() && !dock.SecondDistanceShown())
        || !Explain("作る形の範囲が面まで", window.ExtrudeChoice().extent == ExtrudeExtentMode::ToTarget)) {
        return false;
    }
    // 数値で決める向きを選ぶと x y z の欄が生え、その値が向きになる。
    dock.ChooseExtent(ExtrudeExtentMode::Distance);
    if (!Explain("棚で「数値で決める」向きを選べる",
            dock.PickDirection(ExtrudeDirectionMode::CustomXYZ))) {
        return false;
    }
    dock.SetCustomDirection(kachakacha::v2::geometry::Vector3{0.0, 3.0, 4.0});
    window.RefreshExtrudeFromDock();
    const auto direction = window.ExtrudeDirectionNow();
    if (!Explain("作る形の向きが数値で決める", window.ExtrudeChoice().direction == ExtrudeDirectionMode::CustomXYZ)
        || !Explain((std::string("矢印が (0, 0.6, 0.8) を向く(y=") + std::to_string(direction.y) + " z="
                        + std::to_string(direction.z) + ")").c_str(),
            std::abs(direction.y - 0.6) < 1.0e-6 && std::abs(direction.z - 0.8) < 1.0e-6)) {
        return false;
    }
    // 核に無い欄は押せない形で理由が読める。
    if (!Explain("From は押せない形で理由がある", dock.FromBlockedReasonJa().contains(QStringLiteral("核")))
        || !Explain("テーパーは押せない形で理由がある", dock.TaperBlockedReasonJa().contains(QStringLiteral("核")))) {
        return false;
    }
    dock.ChooseDirection(ExtrudeDirectionMode::ProfileNormal);
    window.RefreshExtrudeFromDock();
    return Explain("Esc でやめられる", window.HandleToolKey(Qt::Key_Escape, nullptr))
        && Explain("棚が引っ込む", !window.ShelfShown(Shelf::Extrude));
}

//! HP-PA-02。左右対称を棚で選んで Enter すると、対称で作られる(下見と確定が同じ値)。
[[nodiscard]] bool CaseSymmetricExtentFromShelfReachesTheSolid(V2MainWindow& window)
{
    if (!ArmExtrude(window)) {
        return false;
    }
    auto& dock = window.ExtrudeDock();
    dock.ChooseExtent(ExtrudeExtentMode::SymmetricDistance);
    window.RefreshExtrudeFromDock();
    if (!Explain("作る形の範囲が左右対称",
            window.ExtrudeChoice().extent == ExtrudeExtentMode::SymmetricDistance)) {
        return false;
    }
    window.RunCommand("part.extrude");   // 確定
    if (!Explain((std::string("立体ができる(帯は ") + window.StatusText().toStdString() + ")").c_str(),
            CountOfKind(window, kachakacha::v2::domain::EntityKind::Part) == 1)) {
        return false;
    }
    bool symmetric = false;
    for (const auto& feature : window.Session().GetDocument().Snapshot().features) {
        const auto* definition =
            std::get_if<kachakacha::v2::domain::ExtrudeDefinition>(&feature.definition);
        if (definition != nullptr) {
            symmetric = definition->extentMode
                == static_cast<int>(ExtrudeExtentMode::SymmetricDistance);
        }
    }
    return Explain("保存された作り方も左右対称", symmetric);
}


[[nodiscard]] bool CaseExtrudeToolFirstAfterDrawing(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!DrawRectangleByHand(window)) return false;
    window.Viewport().SelectAt(QPointF(2, 2), Qt::NoModifier);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    window.SetMode(UiMode::Part);
    window.RunCommand("part.extrude");
    if (!Explain("描画道具から押し出しへ持ち替える", window.Session().CurrentTool()
            == kachakacha::v2::modeling::DrawingTool::Select)) return false;
    if (!ClickOnAnyCurve(window, Qt::NoModifier)) return false;
    if (!Explain("ツールから入って輪郭を拾うと下見になる", window.Viewport().ExtrudeHandleShown())) return false;
    auto& dock = window.ExtrudeDock();
    (void)dock.PickDirection(ExtrudeDirectionMode::WorldZ);
    dock.TypeDistanceMm(7.0);
    dock.SetSecondDistanceMm(3.0);
    (void)dock.PickExtent(ExtrudeExtentMode::TwoDistances);
    const auto& loops = window.Viewport().ExtrudeHandlePreview();
    if (!Explain("非対称の下見は -3 mm と +7 mm", loops.size() >= 2
            && std::abs(loops[0][0].z + 3.0) < 1.0e-6
            && std::abs(loops[1][0].z - 7.0) < 1.0e-6)) return false;
    dock.PressReverse();
    const auto& reversed = window.Viewport().ExtrudeHandlePreview();
    if (!Explain("反転は両方の側に作用する", std::abs(reversed[0][0].z - 3.0) < 1.0e-6
            && std::abs(reversed[1][0].z + 7.0) < 1.0e-6)) return false;
    dock.PressConfirm();
    if (!window.SaveAndReopen(QStringLiteral("extrude-two-sides.kcd2"))) return false;
    for (const auto& shape : window.Viewport().ShapeViews()) {
        if (!shape.surface) return Explain("再読込しても非対称の距離を保持する",
            std::abs(shape.mesh.minimum.z + 7.0) < 1.0e-5 && std::abs(shape.mesh.maximum.z - 3.0) < 1.0e-5);
    }
    return false;
}

[[nodiscard]] bool RefreshFixture(V2MainWindow& window)
{
    const auto selected = window.Viewport().Selection();
    if (!window.SaveAndReopen(QStringLiteral("extrude-fixture.kcd2"))) return false;
    window.Viewport().SetSelection(selected);
    return true;
}

[[nodiscard]] bool MakeCurvedGuide(V2MainWindow& window)
{
    using kachakacha::v2::geometry::CurveSegment;
    window.RunCommand("file.new");
    for (const double y : {0.0, 8.0}) {
        const auto curve = CurveSegment::MakeCubicBezier({{0,y,0}, {3,y,5}, {7,y,5}, {10,y,0}});
        if (!curve.HasValue() || !window.Session().AddWire({curve.Value()}, false, "曲面の断面").committed) return false;
    }
    if (!RefreshFixture(window)) return false;
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), kachakacha::v2::domain::EntityKind::Wire));
    window.CreateGuideSurfaceFromSelection();
    return Explain("曲がった面の入力を用意できる", CountOfKind(window, kachakacha::v2::domain::EntityKind::GuideSurface) == 1);
}

[[nodiscard]] bool CaseCurvedFaceExtrudeWithDirectionLine(V2MainWindow& window)
{
    if (!MakeCurvedGuide(window)) return false;
    const auto line = kachakacha::v2::geometry::CurveSegment::MakeLine({20,0,0}, {20,3,4});
    if (!window.Session().AddWire({line.Value()}, false, "斜め方向").committed) return false;
    if (!RefreshFixture(window)) return false;
    window.Viewport().SetViewDirection(ViewDirection::Isometric);
    window.RunCommand("view.fit_all");
    window.Viewport().SelectAt(QPointF(2,2), Qt::NoModifier);
    window.SetMode(UiMode::Part);
    window.RunCommand("part.extrude");
    if (!ClickOnAnyGuideSurface(window)) return false;
    if (!Explain("面を後から選んで押し出しを開始できる", window.Viewport().ExtrudeHandleShown())) return false;
    auto& dock = window.ExtrudeDock();
    (void)dock.PickDirection(ExtrudeDirectionMode::SelectedVector);
    auto* reference = window.findChild<QComboBox*>(QStringLiteral("extrudeDirectionReference"));
    if (reference == nullptr) return false;
    reference->setCurrentIndex(reference->findText(QStringLiteral("斜め方向")));
    dock.TypeDistanceMm(5.0);
    if (!Explain("選んだ線の方向へ下見が向く", std::abs(window.Viewport().ExtrudeHandleDirection().y - 0.6) < 1.0e-6
            && !window.Viewport().ExtrudePreviewFaces().empty())) return false;
    dock.PressConfirm();
    if (!Explain("曲面のまま立体化できる", CountOfKind(window, kachakacha::v2::domain::EntityKind::Part) == 1)) return false;
    if (!window.SaveAndReopen(QStringLiteral("curved-face-extrude.kcd2"))) return false;
    for (const auto& shape : window.Viewport().ShapeViews()) {
        if (!shape.surface) return Explain("曲面の押し出しを保存・再生成できる", shape.mesh.closed && shape.mesh.maximum.y > 10.9);
    }
    return false;
}

[[nodiscard]] bool CaseExtrudeToInclinedLine(V2MainWindow& window)
{
    if (!ArmExtrude(window)) return false;
    window.ExtrudeDock().PressCancel();
    const auto line = kachakacha::v2::geometry::CurveSegment::MakeLine({-200,0,20}, {200,0,40});
    if (!window.Session().AddWire({line.Value()}, false, "斜め終端").committed) return false;
    if (!RefreshFixture(window)) return false;
    window.RunCommand("part.extrude");
    auto& dock = window.ExtrudeDock();
    (void)dock.PickDirection(ExtrudeDirectionMode::WorldZ);
    (void)dock.PickExtent(ExtrudeExtentMode::ToTarget);
    auto* target = window.findChild<QComboBox*>(QStringLiteral("extrudeStopReference"));
    if (target == nullptr) return false;
    target->setCurrentIndex(target->findText(QStringLiteral("斜め終端（線の傾きまで）")));
    if (!Explain("線の斜面までプレビューされる", !window.Viewport().ExtrudePreviewFaces().empty())) return false;
    dock.PressConfirm();
    if (!Explain("斜め終端の立体になる", CountOfKind(window, kachakacha::v2::domain::EntityKind::Part) == 1)) return false;
    if (!window.SaveAndReopen(QStringLiteral("extrude-inclined-stop.kcd2"))) return false;
    for (const auto& shape : window.Viewport().ShapeViews()) {
        if (shape.surface) continue;
        bool onCap = false;
        for (const auto& edge : shape.mesh.edges) for (const auto& point : {edge.front(), edge.back()}) {
            if (point.z > 1.0) {
                if (std::abs(point.z - (0.05 * point.x + 30.0)) > 1.0e-4) return false;
                onCap = true;
            }
        }
        return Explain("再生成した終端も線を含む斜面", onCap);
    }
    return false;
}


[[nodiscard]] bool CaseExtrudeStopsAtCurvedFace(V2MainWindow& window)
{
    using namespace kachakacha::v2;
    if (!MakeCurvedGuide(window)) return false;
    std::vector<geometry::CurveSegment> segments;
    const std::vector<geometry::Vector3> points{{1,1,-5},{9,1,-5},{9,7,-5},{1,7,-5}};
    for (std::size_t i = 0; i < points.size(); ++i) segments.push_back(
        geometry::CurveSegment::MakeLine(points[i], points[(i + 1) % points.size()]).Value());
    const auto wire = window.Session().AddWire(segments, false, "押す輪郭");
    if (!wire.committed || !RefreshFixture(window)) return false;
    app::SelectionSet selection;
    selection.entityIds = wire.createdEntityIds;
    window.Viewport().SetSelection(selection);
    window.SetMode(UiMode::Part);
    window.RunCommand("part.extrude");
    auto& dock = window.ExtrudeDock();
    (void)dock.PickDirection(ExtrudeDirectionMode::WorldZ);
    (void)dock.PickExtent(ExtrudeExtentMode::ToTarget);
    auto* target = window.findChild<QComboBox*>(QStringLiteral("extrudeStopReference"));
    if (target == nullptr) return false;
    for (int i = 0; i < target->count(); ++i) {
        if (target->itemText(i).contains(QStringLiteral("（面まで）"))) { target->setCurrentIndex(i); break; }
    }
    if (!Explain("曲面で止まる実形状の下見", !window.Viewport().ExtrudePreviewFaces().empty())) return false;
    dock.PressConfirm();
    if (!Explain("曲面まで閉じた立体になる", CountOfKind(window, domain::EntityKind::Part) == 1)) return false;
    if (!window.SaveAndReopen(QStringLiteral("extrude-curved-stop.kcd2"))) return false;
    for (const auto& shape : window.Viewport().ShapeViews()) {
        if (!shape.surface) return Explain("再生成した終端に曲がりが残る", shape.mesh.closed
            && std::abs(shape.mesh.minimum.z + 5.0) < 1.0e-4 && shape.mesh.maximum.z > 3.5);
    }
    return false;
}

} // namespace

std::vector<SelfTestCase> PartPanelCases()
{
    return {
        {"HP-PA-06 任意曲面を終端として押し出し、保存・再生成", CaseExtrudeStopsAtCurvedFace},
        {"HP-PA-03 ツール先行で輪郭を選び、非対称と反転を保存後も維持", CaseExtrudeToolFirstAfterDrawing},
        {"HP-PA-04 曲面を後から選び、直線方向に押し出して保存・再生成", CaseCurvedFaceExtrudeWithDirectionLine},
        {"HP-PA-05 斜めの線の面まで押し出して保存・再生成", CaseExtrudeToInclinedLine},
        {"HP-PA-01 押し出しの棚に範囲 5 通り・方向 7 通りが並び、要る欄だけ生えて作る形へ届く",
            CaseExtrudeShelfListsAllExtentsAndDirections},
        {"HP-PA-02 棚で選んだ左右対称が確定した立体に残る", CaseSymmetricExtentFromShelfReachesTheSolid},
    };
}

} // namespace kachakacha::v2::selftest
