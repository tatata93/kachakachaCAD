//! 作図の棚(V1 の「作図」タブ)のケース。
//!
//! 円弧の作り方、補助線として作図、指定した点を作図点として残す、数値で線を作る。
//! core(ToolSettings / DirectWireEntry)は前から揃っていたが、画面から触る道が無かった。
//! ここでは「棚を通って道具の振る舞いと文書が変わるか」を見る。

#include "V2SelfTest.h"

#include "V2DisplayDock.h"
#include "V2DrawingDock.h"
#include "V2GridDock.h"
#include "V2ParameterDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/CommandParameters.h"
#include "kachakacha/app/DirectWireEntry.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/modeling/ToolController.h"

#include <QColor>
#include <QPointF>
#include <QString>

#include <cmath>
#include <string>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::modeling::ArcMode;
using kachakacha::v2::modeling::DrawingTool;
using kachakacha::v2::modeling::ToolSettings;

//! 上から見て、幅 200mm が画面に入る状態。1mm が何 px かを返す。
[[nodiscard]] double PrepareTopView(V2MainWindow& window)
{
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(200.0);
    return viewport.width() / 200.0;
}

[[nodiscard]] bool CaseArcModeFromDock(V2MainWindow& window)
{
    // 「両端 + 半径」にすると、円弧は2点で決まり、半径は欄の値になる。
    const double pxPerMm = PrepareTopView(window);
    auto& viewport = window.Viewport();
    ToolSettings settings;
    settings.arcMode = ArcMode::EndpointsAndRadius;
    settings.radiusMm = 30.0;
    window.DrawingDock().SetSettings(settings);
    window.SelectTool(DrawingTool::Arc);
    const int before = CountOfKind(window, EntityKind::Wire);
    const QPointF center(viewport.width() * 0.5, viewport.height() * 0.5);
    viewport.ClickAt(center);
    viewport.ClickAt(QPointF(center.x() + 40.0 * pxPerMm, center.y()));
    if (!Explain((std::string("2点で円弧ができる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            CountOfKind(window, EntityKind::Wire) == before + 1)) {
        return false;
    }
    const auto& curves = window.Session().Scene().curves;
    if (!Explain("種類が円弧",
            !curves.empty()
                && curves.back().segment.Kind() == kachakacha::v2::geometry::CurveKind::CircularArc)) {
        return false;
    }
    // 弦 40mm、半径 30mm。中点から円弧の中央までの矢高は r - sqrt(r² - 20²)。
    const auto start = curves.back().segment.StartPoint();
    const auto end = curves.back().segment.EndPoint();
    const auto middle = curves.back().segment.Evaluate(0.5);
    const double sagitta = (middle - (start + end) * 0.5).Length();
    const double expected = 30.0 - std::sqrt(30.0 * 30.0 - 20.0 * 20.0);
    if (!Explain((std::string("半径 30 の矢高(") + std::to_string(sagitta) + " と "
                     + std::to_string(expected) + ")").c_str(),
            std::abs(sagitta - expected) < 0.05)) {
        return false;
    }
    // 3点へ戻すと、また3点要る。
    settings.arcMode = ArcMode::ThreePoints;
    window.DrawingDock().SetSettings(settings);
    window.SelectTool(DrawingTool::Arc);
    viewport.ClickAt(center);
    viewport.ClickAt(QPointF(center.x() + 40.0 * pxPerMm, center.y()));
    return Explain("3点では2点で確定しない",
        CountOfKind(window, EntityKind::Wire) == before + 1);
}

[[nodiscard]] bool CaseConstructionAndKeepPointsFromDock(V2MainWindow& window)
{
    // 補助線として作図・指定した点を作図点として残す。線と点はひとまとまりで戻せる。
    const double pxPerMm = PrepareTopView(window);
    auto& viewport = window.Viewport();
    ToolSettings settings;
    settings.construction = true;
    settings.keepPoints = true;
    window.DrawingDock().SetSettings(settings);
    window.SelectTool(DrawingTool::Line);
    const int wires = CountOfKind(window, EntityKind::Wire);
    const int points = CountOfKind(window, EntityKind::Point);
    const QPointF center(viewport.width() * 0.5, viewport.height() * 0.5);
    viewport.ClickAt(center);
    viewport.ClickAt(QPointF(center.x() + 50.0 * pxPerMm, center.y()));
    if (!Explain("線が1本増える", CountOfKind(window, EntityKind::Wire) == wires + 1)) {
        return false;
    }
    if (!Explain((std::string("指した2点が作図点として残る(実際は ")
                     + std::to_string(CountOfKind(window, EntityKind::Point) - points) + ")")
                     .c_str(),
            CountOfKind(window, EntityKind::Point) == points + 2)) {
        return false;
    }
    bool construction = false;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Wire) {
            construction = entity.construction;
        }
    }
    if (!Explain("補助線になっている", construction)) {
        return false;
    }
    // 元に戻すのは一度で線も点も消える。
    window.RunCommand("edit.undo");
    if (!Explain("一度の取り消しで線も点も消える",
            CountOfKind(window, EntityKind::Wire) == wires
                && CountOfKind(window, EntityKind::Point) == points)) {
        return false;
    }
    // 外せば元どおり。
    window.DrawingDock().SetSettings(ToolSettings{});
    window.SelectTool(DrawingTool::Line);
    viewport.ClickAt(center);
    viewport.ClickAt(QPointF(center.x() + 50.0 * pxPerMm, center.y()));
    return Explain("既定では点を残さない",
        CountOfKind(window, EntityKind::Point) == points
            && CountOfKind(window, EntityKind::Wire) == wires + 1);
}

[[nodiscard]] bool CaseDirectWireFromDock(V2MainWindow& window)
{
    // 数値で線を作る。座標を打って「線を作る」。作れないときは理由が棚に出る。
    (void)PrepareTopView(window);
    kachakacha::v2::app::DirectWireRequest request;
    request.kind = kachakacha::v2::app::DirectWireKind::PlanarLine;
    request.points = {kachakacha::v2::geometry::Vector3{0.0, 0.0, 0.0},
        kachakacha::v2::geometry::Vector3{25.0, 0.0, 0.0}};
    window.DrawingDock().SetDirectWire(request, QStringLiteral("基準線"));
    const int before = CountOfKind(window, EntityKind::Wire);
    window.DrawingDock().PressCreateWire();
    if (!Explain((std::string("線が増える(") + window.StatusText().toStdString() + ")").c_str(),
            CountOfKind(window, EntityKind::Wire) == before + 1)) {
        return false;
    }
    bool named = false;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        named = named || entity.displayName == "基準線";
    }
    if (!Explain("付けた名前で一覧に出る", named)) {
        return false;
    }
    const auto& curves = window.Session().Scene().curves;
    const auto delta = curves.back().segment.EndPoint() - curves.back().segment.StartPoint();
    if (!Explain((std::string("長さ 25(実際は ") + std::to_string(delta.Length()) + ")").c_str(),
            std::abs(delta.Length() - 25.0) < 1.0e-9)) {
        return false;
    }
    // 半径 0 の円は断る。文書は変わらない。
    request.kind = kachakacha::v2::app::DirectWireKind::PlanarCircle;
    request.radiusMm = 0.0;
    window.DrawingDock().SetDirectWire(request, QString());
    window.DrawingDock().PressCreateWire();
    if (!Explain((std::string("半径 0 は断る(") + window.StatusText().toStdString() + ")").c_str(),
            CountOfKind(window, EntityKind::Wire) == before + 1
                && window.StatusText().contains(QStringLiteral("半径")))) {
        return false;
    }
    // 3D 直線は世界座標のまま。
    request.kind = kachakacha::v2::app::DirectWireKind::SpatialLine;
    request.points = {kachakacha::v2::geometry::Vector3{0.0, 0.0, 5.0},
        kachakacha::v2::geometry::Vector3{0.0, 0.0, 45.0}};
    window.DrawingDock().SetDirectWire(request, QStringLiteral("柱"));
    window.DrawingDock().PressCreateWire();
    const auto& made = window.Session().Scene().curves.back().segment;
    return Explain("3D 直線は z へ伸びる",
        CountOfKind(window, EntityKind::Wire) == before + 2
            && std::abs(made.EndPoint().z - 45.0) < 1.0e-9);
}

[[nodiscard]] bool CaseGridDockAppliesSpacingSubdivisionAndOrigin(V2MainWindow& window)
{
    // グリッドの棚(V1 のグリッド欄)。間隔は式で、副点・基準も棚から。文書は変わらない。
    const auto revision = window.Session().GetDocument().Revision();
    auto& dock = window.GridDock();
    window.RunCommand("grid.edit");
    if (!Explain("式が読める", dock.ApplySpacingExpression(QStringLiteral("25.4/8")))) {
        return false;
    }
    if (!Explain((std::string("間隔が 3.175(実際は ")
                     + std::to_string(window.Session().Scene().grid.majorSpacingMm) + ")").c_str(),
            std::abs(window.Session().Scene().grid.majorSpacingMm - 3.175) < 1.0e-9)) {
        return false;
    }
    if (!Explain("読めない式は当てず理由が出る",
            !dock.ApplySpacingExpression(QStringLiteral("abc")) && !dock.MessageText().isEmpty()
                && std::abs(window.Session().Scene().grid.majorSpacingMm - 3.175) < 1.0e-9)) {
        return false;
    }
    V2GridChoice choice = dock.Choice();
    choice.grid.subdivision = 4;
    choice.grid.originUmm = 5.0;
    choice.grid.originVmm = 7.0;
    choice.showInAllModes = false;
    dock.SetChoice(choice);
    dock.Apply();
    const auto& grid = window.Session().Scene().grid;
    if (!Explain("副点 1/4", grid.subdivision == 4)) {
        return false;
    }
    const auto expected = window.Viewport().WorkPlane().PointAt(5.0, 7.0);
    if (!Explain("基準が作業平面の (5, 7)", (grid.origin - expected).Length() < 1.0e-9)) {
        return false;
    }
    // 「作図モード以外でも表示」を外すと、部品モードではグリッドが出ない。
    window.SetMode(kachakacha::v2::app::UiMode::Part);
    if (!Explain("部品モードではグリッドを出さない", window.Viewport().GridSuppressedByMode())) {
        return false;
    }
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    if (!Explain("作図モードでは出す", !window.Viewport().GridSuppressedByMode())) {
        return false;
    }
    return Explain("文書は変わらない", window.Session().GetDocument().Revision() == revision);
}

[[nodiscard]] bool CaseDisplayDockStylesAndStages(V2MainWindow& window)
{
    // 表示の棚(V1 の表示タブ)。太さ・様式・色と、設計/完成形/選択だけ。形は変わらない。
    const auto revision = window.Session().GetDocument().Revision();
    auto& dock = window.DisplayDock();
    window.RunCommand("view.display_settings");
    V2DisplayChoice choice = dock.Choice();
    choice.settings.wireWidthPx = 3.5;
    choice.settings.wireStyle = kachakacha::v2::app::LineStyle::Dotted;
    choice.backgroundColor = QColor(10, 20, 30);
    dock.SetChoice(choice, kachakacha::v2::app::DisplayStage::All);
    dock.Apply();
    const auto& shown = window.Viewport().DisplaySettingsNow();
    if (!Explain("太さ 3.5 と点線が効く",
            std::abs(shown.wireWidthPx - 3.5) < 1.0e-9
                && shown.wireStyle == kachakacha::v2::app::LineStyle::Dotted)) {
        return false;
    }
    if (!Explain("背景色が効く", window.Viewport().Colors().background == QColor(10, 20, 30))) {
        return false;
    }
    // 段を変えても太さは残る。
    dock.PressStage(kachakacha::v2::app::DisplayStage::SelectionOnly);
    if (!Explain("選択だけになる", window.Viewport().DisplaySettingsNow().selectionOnly)) {
        return false;
    }
    if (!Explain("段を変えても太さは残る",
            std::abs(window.Viewport().DisplaySettingsNow().wireWidthPx - 3.5) < 1.0e-9)) {
        return false;
    }
    window.RunCommand("view.stage_all");
    if (!Explain("設計に戻る",
            !window.Viewport().DisplaySettingsNow().selectionOnly
                && window.Viewport().DisplaySettingsNow().gridVisible)) {
        return false;
    }
    return Explain("文書は変わらない", window.Session().GetDocument().Revision() == revision);
}

//! 上から見て 2本の線を引く。1本目は横、2本目は縦(十字)。
[[nodiscard]] bool DrawCross(V2MainWindow& window, double pxPerMm)
{
    auto& viewport = window.Viewport();
    const QPointF center(viewport.width() * 0.5, viewport.height() * 0.5);
    window.SelectTool(DrawingTool::Line);
    viewport.ClickAt(QPointF(center.x() - 30.0 * pxPerMm, center.y()));
    viewport.ClickAt(QPointF(center.x() + 30.0 * pxPerMm, center.y()));
    viewport.ClickAt(QPointF(center.x(), center.y() - 30.0 * pxPerMm));
    viewport.ClickAt(QPointF(center.x(), center.y() + 30.0 * pxPerMm));
    window.SelectTool(DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::Wire));
    return CountOfKind(window, EntityKind::Wire) == 2;
}

[[nodiscard]] bool CaseOffsetKeepsOriginalAndMeetLinesJoins(V2MainWindow& window)
{
    // オフセットは元の線を残して平行に写す。2線を交点まで は2本を交点で合わせる。
    const double pxPerMm = PrepareTopView(window);
    if (!Explain("十字を引ける", DrawCross(window, pxPerMm))) {
        return false;
    }
    // 横線だけを選んでオフセット。数の棚の「オフセット距離」を 5 にする。
    auto& viewport = window.Viewport();
    kachakacha::v2::app::SelectionSet one;
    one.entityIds.push_back(viewport.Selection().entityIds.front());
    viewport.SetSelection(one);
    if (!Explain("距離を入れられる",
            window.ParameterDock().Apply(kachakacha::v2::app::ParameterId::OffsetDistanceMm,
                QStringLiteral("5")))) {
        return false;
    }
    window.RunCommand("wire.offset");
    if (!Explain((std::string("線が増え元は残る(") + window.StatusText().toStdString() + ")")
                     .c_str(),
            CountOfKind(window, EntityKind::Wire) == 3)) {
        return false;
    }
    const auto& made = window.Session().Scene().curves.back().segment;
    const double gap = std::abs(made.StartPoint().y - viewport.WorkPlane().origin.y);
    if (!Explain((std::string("5mm 離れる(実際は ") + std::to_string(gap) + ")").c_str(),
            std::abs(gap - 5.0) < 1.0e-6)) {
        return false;
    }
    // 2線を交点まで: 離れた2本の直線を引いて合わせる。
    window.RunCommand("file.new");
    const QPointF center(viewport.width() * 0.5, viewport.height() * 0.5);
    window.SelectTool(DrawingTool::Line);
    viewport.ClickAt(QPointF(center.x() - 40.0 * pxPerMm, center.y()));
    viewport.ClickAt(QPointF(center.x() - 10.0 * pxPerMm, center.y()));
    viewport.ClickAt(QPointF(center.x() + 10.0 * pxPerMm, center.y() - 10.0 * pxPerMm));
    viewport.ClickAt(QPointF(center.x() + 10.0 * pxPerMm, center.y() - 40.0 * pxPerMm));
    window.SelectTool(DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::Wire));
    window.RunCommand("wire.meet_lines");
    if (!Explain((std::string("交点まで延びる(") + window.StatusText().toStdString() + ")")
                     .c_str(),
            window.StatusText().contains(QStringLiteral("2線を交点まで")))) {
        return false;
    }
    // 出来た線は、交点(横線の高さ・縦線の位置)で端が合う。
    const auto& curves = window.Session().Scene().curves;
    bool meets = false;
    for (const auto& a : curves) {
        for (const auto& b : curves) {
            if (&a == &b) {
                continue;
            }
            for (const auto& pa : {a.segment.StartPoint(), a.segment.EndPoint()}) {
                for (const auto& pb : {b.segment.StartPoint(), b.segment.EndPoint()}) {
                    meets = meets || (pa - pb).Length() < 1.0e-6;
                }
            }
        }
    }
    return Explain("端が交点で合う", meets);
}

[[nodiscard]] bool CaseIntersectionPointsAndDatum(V2MainWindow& window)
{
    // 交点に点: 十字の交点に作図点が1つ。線は変わらない。基準線: 印だけ付く。
    const double pxPerMm = PrepareTopView(window);
    if (!Explain("十字を引ける", DrawCross(window, pxPerMm))) {
        return false;
    }
    window.RunCommand("wire.intersection_points");
    if (!Explain((std::string("交点に点が1つ(") + window.StatusText().toStdString() + ")")
                     .c_str(),
            CountOfKind(window, EntityKind::Point) == 1
                && CountOfKind(window, EntityKind::Wire) == 2)) {
        return false;
    }
    const auto& point = window.Session().Scene().points.back().position;
    if (!Explain("交点は原点", (point - window.Viewport().WorkPlane().origin).Length() < 1.0e-6)) {
        return false;
    }
    window.RunCommand("edit.undo");
    if (!Explain("一度で消える", CountOfKind(window, EntityKind::Point) == 0)) {
        return false;
    }
    // 平行な2本では断る。
    window.RunCommand("file.new");
    auto& viewport = window.Viewport();
    const QPointF center(viewport.width() * 0.5, viewport.height() * 0.5);
    window.SelectTool(DrawingTool::Line);
    viewport.ClickAt(QPointF(center.x() - 30.0 * pxPerMm, center.y()));
    viewport.ClickAt(QPointF(center.x() + 30.0 * pxPerMm, center.y()));
    viewport.ClickAt(QPointF(center.x() - 30.0 * pxPerMm, center.y() + 20.0 * pxPerMm));
    viewport.ClickAt(QPointF(center.x() + 30.0 * pxPerMm, center.y() + 20.0 * pxPerMm));
    window.SelectTool(DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::Wire));
    window.RunCommand("wire.intersection_points");
    if (!Explain((std::string("交わらなければ断る(") + window.StatusText().toStdString() + ")")
                     .c_str(),
            CountOfKind(window, EntityKind::Point) == 0
                && window.StatusText().contains(QStringLiteral("交点")))) {
        return false;
    }
    // 基準線に設定 → 場面の線に印が付く → 解除で消える。形は同じ。
    const auto before = window.Session().Scene().curves.front().segment.StartPoint();
    window.RunCommand("wire.set_datum");
    const auto& curves = window.Session().Scene().curves;
    bool allDatum = !curves.empty();
    for (const auto& curve : curves) {
        allDatum = allDatum && curve.datum;
    }
    if (!Explain("基準線の印が付く", allDatum)) {
        return false;
    }
    if (!Explain("形は同じ",
            (window.Session().Scene().curves.front().segment.StartPoint() - before).Length()
                < 1.0e-9)) {
        return false;
    }
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::Wire));
    window.RunCommand("wire.clear_datum");
    bool anyDatum = false;
    for (const auto& curve : window.Session().Scene().curves) {
        anyDatum = anyDatum || curve.datum;
    }
    return Explain("解除で消える", !anyDatum);
}

} // namespace

std::vector<SelfTestCase> DrawingCases()
{
    return {
        {"オフセットは元を残し2線を交点まで合わせる", &CaseOffsetKeepsOriginalAndMeetLinesJoins},
        {"交点に点と基準線が効く", &CaseIntersectionPointsAndDatum},
        {"グリッドの棚で間隔・副点・基準が変わる", &CaseGridDockAppliesSpacingSubdivisionAndOrigin},
        {"表示の棚で太さ・様式・色と段が変わる", &CaseDisplayDockStylesAndStages},
        {"作図の棚で円弧の作り方を変えられる", &CaseArcModeFromDock},
        {"補助線として作図し指定点を残せる", &CaseConstructionAndKeepPointsFromDock},
        {"数値で線を作れる", &CaseDirectWireFromDock},
    };
}

} // namespace kachakacha::v2::selftest
