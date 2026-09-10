//! 作図の棚(V1 の「作図」タブ)のケース。
//!
//! 円弧の作り方、補助線として作図、指定した点を作図点として残す、数値で線を作る。
//! core(ToolSettings / DirectWireEntry)は前から揃っていたが、画面から触る道が無かった。
//! ここでは「棚を通って道具の振る舞いと文書が変わるか」を見る。

#include "V2SelfTest.h"

#include "V2DrawingDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/DirectWireEntry.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/modeling/ToolController.h"

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

} // namespace

std::vector<SelfTestCase> DrawingCases()
{
    return {
        {"作図の棚で円弧の作り方を変えられる", &CaseArcModeFromDock},
        {"補助線として作図し指定点を残せる", &CaseConstructionAndKeepPointsFromDock},
        {"数値で線を作れる", &CaseDirectWireFromDock},
    };
}

} // namespace kachakacha::v2::selftest
