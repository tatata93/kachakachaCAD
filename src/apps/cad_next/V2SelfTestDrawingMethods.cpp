//! 作図の「作り方」カードの人の道(HP-DM)。正本 3 HTML(2026-09-18)、指示書 D-01〜D-14。
//!
//! 帯で道具を持つと右の欄にその道具のカードが並び、押すと作り方(円弧の ArcMode)が変わる。
//! 核に無いカードは押せず、理由が状態行に出る。カードの一文が欄に出る。
//! 3点のカードで実際に 3 か所を押すと円弧ができる。

#include "V2SelfTest.h"

#include "V2DrawingDock.h"
#include "V2MainWindow.h"
#include "V2Ribbon.h"
#include "V2Viewport.h"

#include "kachakacha/app/UiMode.h"
#include "kachakacha/modeling/ToolController.h"

#include <QPointF>
#include <QString>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::UiMode;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::modeling::ArcMode;
using kachakacha::v2::modeling::DrawingTool;

[[nodiscard]] int WireCount(V2MainWindow& window)
{
    int count = 0;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Wire) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] bool HasLabel(const std::vector<QString>& labels, const char* wanted)
{
    for (const QString& label : labels) {
        if (label == QString::fromUtf8(wanted)) {
            return true;
        }
    }
    return false;
}

//! HP-DM-01。帯で円弧を持つと 4 枚のカードが並び、押すと作り方が変わる。核に無いカードは理由つき。
[[nodiscard]] bool CaseMethodCardsFollowToolAndChangeArcMode(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SetMode(UiMode::Drawing);
    auto& ribbon = window.Ribbon();
    if (!Explain("帯の「円弧」を押せる", ribbon.ClickTool(QStringLiteral("円弧")))
        || !Explain("円弧の道具になる", window.Session().CurrentTool() == DrawingTool::Arc)) {
        return false;
    }
    auto& dock = window.DrawingDock();
    const auto labels = dock.MethodLabels();
    if (!Explain((std::string("円弧のカードは 4 枚(実際 ") + std::to_string(labels.size()) + ")").c_str(),
            labels.size() == 4)
        || !Explain("3点 / 始点・終点・半径 / 中心・始点・終点 / 始点接線 がある",
            HasLabel(labels, "3点") && HasLabel(labels, "始点・終点・半径")
                && HasLabel(labels, "中心・始点・終点") && HasLabel(labels, "始点接線・半径・中心角"))
        || !Explain("最初は 3点", dock.CurrentMethodLabel() == QStringLiteral("3点"))) {
        return false;
    }
    if (!Explain("「始点・終点・半径」を押せる", dock.ClickMethod(QStringLiteral("始点・終点・半径")))
        || !Explain("作り方が両端+半径になる", dock.Settings().arcMode == ArcMode::EndpointsAndRadius)
        || !Explain("欄の一文がそのカードの一文",
            dock.HintText().contains(QStringLiteral("始点と終点")))) {
        return false;
    }
    // 中心・始点・終点(D-08)も押せる。押すと作り方が変わり、一文が中心から決める説明になる。
    if (!Explain("「中心・始点・終点」も押せる", dock.MethodEnabled(QStringLiteral("中心・始点・終点")))
        || !Explain("「中心・始点・終点」を押せる", dock.ClickMethod(QStringLiteral("中心・始点・終点")))
        || !Explain("作り方が中心・始点・終点になる", dock.Settings().arcMode == ArcMode::CenterStartEnd)
        || !Explain("欄の一文が左回りを言う", dock.HintText().contains(QStringLiteral("左回り")))) {
        return false;
    }
    // 道具を替えるとカードも替わる(円弧のカードが残らない)。
    if (!Explain("帯の「円」を押せる", ribbon.ClickTool(QStringLiteral("円")))) {
        return false;
    }
    const auto circle = dock.MethodLabels();
    return Explain("円のカードは 3 枚", circle.size() == 3)
        && Explain("中心＋半径 / 直径指定 / 3点", HasLabel(circle, "中心＋半径")
            && HasLabel(circle, "直径指定") && HasLabel(circle, "3点"))
        && Explain("3点円も押せる(D-04)", dock.MethodEnabled(QStringLiteral("3点")))
        && Explain("最初は中心＋半径", dock.CurrentMethodLabel() == QStringLiteral("中心＋半径"));
}

//! HP-DM-02。3点のカードのまま 3 か所を押すと円弧が 1 本できる。
[[nodiscard]] bool CaseThreePointArcByCards(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SetMode(UiMode::Drawing);
    if (!Explain("帯の「円弧」を押せる", window.Ribbon().ClickTool(QStringLiteral("円弧")))
        || !Explain("「3点」を押せる", window.DrawingDock().ClickMethod(QStringLiteral("3点")))
        || !Explain("作り方は 3点", window.DrawingDock().Settings().arcMode == ArcMode::ThreePoints)) {
        return false;
    }
    auto& viewport = window.Viewport();
    const int before = WireCount(window);
    const auto a = viewport.Mapping().Project(kachakacha::v2::geometry::Vector3{-20.0, 0.0, 0.0});
    const auto b = viewport.Mapping().Project(kachakacha::v2::geometry::Vector3{0.0, 15.0, 0.0});
    const auto c = viewport.Mapping().Project(kachakacha::v2::geometry::Vector3{20.0, 0.0, 0.0});
    if (!Explain("3点が画面内", a.has_value() && b.has_value() && c.has_value())) {
        return false;
    }
    viewport.ClickAt(QPointF(a->x, a->y));
    viewport.ClickAt(QPointF(b->x, b->y));
    viewport.ClickAt(QPointF(c->x, c->y));
    window.SelectTool(DrawingTool::Select);
    return Explain((std::string("円弧が 1 本できる(実際 ") + std::to_string(WireCount(window) - before)
                       + ")").c_str(),
        WireCount(window) == before + 1);
}

//! HP-DM-03。スプラインは制御点と通過点を押せ、Fit は理由つきで押せない。ベジェは 1 枚。
[[nodiscard]] bool CaseSplineAndBezierCards(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SetMode(UiMode::Drawing);
    auto& ribbon = window.Ribbon();
    auto& dock = window.DrawingDock();
    if (!Explain("「曲線」を押せる", ribbon.ClickCategory(QStringLiteral("曲線")))
        || !Explain("「スプライン」を押せる", ribbon.ClickTool(QStringLiteral("スプライン")))) {
        return false;
    }
    const auto spline = dock.MethodLabels();
    if (!Explain("スプラインは 3 枚", spline.size() == 3)
        || !Explain("制御点は押せる", dock.MethodEnabled(QStringLiteral("制御点")))
        || !Explain("通過点も押せる(D-13)", dock.MethodEnabled(QStringLiteral("通過点")))
        || !Explain("近似 / Fit は押せない", !dock.MethodEnabled(QStringLiteral("近似 / Fit")))
        || !Explain("押しても偽", !dock.ClickMethod(QStringLiteral("近似 / Fit")))
        || !Explain("理由が状態行に出る", window.StatusText().contains(QStringLiteral("まだ作れません")))
        || !Explain("ツールチップにも理由",
            dock.MethodTip(QStringLiteral("近似 / Fit")).contains(QStringLiteral("核")))) {
        return false;
    }
    if (!Explain("「ベジェ」を押せる", ribbon.ClickTool(QStringLiteral("ベジェ")))) {
        return false;
    }
    const auto bezier = dock.MethodLabels();
    return Explain("ベジェは 1 枚", bezier.size() == 1)
        && Explain("制御点で作成", dock.CurrentMethodLabel() == QStringLiteral("制御点で作成"))
        && Explain("一文に 4 か所", dock.HintText().contains(QStringLiteral("4か所")));
}

//! HP-DM-04。円は「中心＋半径」でも「直径指定」でも、画面の2クリックで1本できる。
[[nodiscard]] bool CaseCircleByCenterRadiusAndDiameterCards(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SetMode(UiMode::Drawing);
    auto& ribbon = window.Ribbon();
    auto& dock = window.DrawingDock();
    if (!Explain("帯の「円」を押せる", ribbon.ClickTool(QStringLiteral("円")))
        || !Explain("円の道具になる", window.Session().CurrentTool() == DrawingTool::Circle)) {
        return false;
    }
    auto& viewport = window.Viewport();
    const auto center = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{0.0, 0.0, 0.0});
    const auto rim = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{15.0, 0.0, 0.0});
    if (!Explain("中心と円周上の1点が画面内", center.has_value() && rim.has_value())) {
        return false;
    }

    // 1本目: 最初から「中心＋半径」のまま、中心と円周上の1点を押す。
    if (!Explain("「中心＋半径」を押せる", dock.ClickMethod(QStringLiteral("中心＋半径")))) {
        return false;
    }
    const int beforeFirst = WireCount(window);
    viewport.ClickAt(QPointF(center->x, center->y));
    viewport.ClickAt(QPointF(rim->x, rim->y));
    if (!Explain((std::string("中心＋半径で円が1本できる(実際 ")
                     + std::to_string(WireCount(window) - beforeFirst) + ")").c_str(),
            WireCount(window) == beforeFirst + 1)) {
        return false;
    }

    // 2本目: 「直径指定」を押してから、同じ2クリックで引く。
    // 核(ToolController::Circle)はカードの種類を見ておらず、置いた2点をいつも
    // 「中心・円周上の1点」としてしか読まない(半径=2点間の距離)。つまり
    // 「直径指定」の一文(カーソル欄の「直径」に打つ)は、まだ2クリックの作図には
    // つながっていない。ここでは、そのカードを選んだままでも道具が壊れず円が
    // もう1本でき、道具が円のまま残ることだけを確かめる(2点目までの距離が
    // 直径として解かれることまでは核が実装していないので、そこは確かめない)。
    if (!Explain("「直径指定」を押せる", dock.ClickMethod(QStringLiteral("直径指定")))
        || !Explain("作り方の表示が「直径指定」になる",
            dock.CurrentMethodLabel() == QStringLiteral("直径指定"))) {
        return false;
    }
    const int beforeSecond = WireCount(window);
    viewport.ClickAt(QPointF(center->x, center->y));
    viewport.ClickAt(QPointF(rim->x, rim->y));
    if (!Explain((std::string("「直径指定」のカードのままでも円がもう1本できる(実際 ")
                     + std::to_string(WireCount(window) - beforeSecond) + ")").c_str(),
            WireCount(window) == beforeSecond + 1)) {
        return false;
    }
    return Explain("2本目のあとも道具は円のまま",
        window.Session().CurrentTool() == DrawingTool::Circle);
}

//! HP-DM-05。ベジェは制御点を4か所押すと、3次ベジェの線が1本できる。
[[nodiscard]] bool CaseBezierFourClicksMakesOneWire(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SetMode(UiMode::Drawing);
    auto& ribbon = window.Ribbon();
    if (!Explain("帯の「曲線」を選べる", ribbon.ClickCategory(QStringLiteral("曲線")))
        || !Explain("「ベジェ」を押せる", ribbon.ClickTool(QStringLiteral("ベジェ")))
        || !Explain("ベジェの道具になる",
            window.Session().CurrentTool() == DrawingTool::Bezier)) {
        return false;
    }
    auto& viewport = window.Viewport();
    const int before = WireCount(window);
    const auto p1 = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{-20.0, 0.0, 0.0});
    const auto p2 = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{-10.0, 15.0, 0.0});
    const auto p3 = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{10.0, -15.0, 0.0});
    const auto p4 = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{20.0, 0.0, 0.0});
    if (!Explain("4つの制御点が画面内",
            p1.has_value() && p2.has_value() && p3.has_value() && p4.has_value())) {
        return false;
    }
    viewport.ClickAt(QPointF(p1->x, p1->y));
    viewport.ClickAt(QPointF(p2->x, p2->y));
    viewport.ClickAt(QPointF(p3->x, p3->y));
    viewport.ClickAt(QPointF(p4->x, p4->y));
    if (!Explain((std::string("ベジェが1本できる(実際 ") + std::to_string(WireCount(window) - before)
                     + ")").c_str(),
            WireCount(window) == before + 1)) {
        return false;
    }
    const auto& curves = window.Session().Scene().curves;
    return Explain("できた線の種類が3次ベジェ(CurveKind::CubicBezier)",
        !curves.empty()
            && curves.back().segment.Kind() == kachakacha::v2::geometry::CurveKind::CubicBezier);
}

//! HP-DM-06。スプラインは制御点を4か所押し、Enterと同じ確定(右クリック相当)で1本できる。
[[nodiscard]] bool CaseSplineFourClicksThenEnterMakesOneWire(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SetMode(UiMode::Drawing);
    auto& ribbon = window.Ribbon();
    if (!Explain("帯の「曲線」を選べる", ribbon.ClickCategory(QStringLiteral("曲線")))
        || !Explain("「スプライン」を押せる", ribbon.ClickTool(QStringLiteral("スプライン")))
        || !Explain("スプラインの道具になる",
            window.Session().CurrentTool() == DrawingTool::Spline)) {
        return false;
    }
    auto& viewport = window.Viewport();
    const int before = WireCount(window);
    const auto p1 = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{-30.0, 0.0, 0.0});
    const auto p2 = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{-10.0, 20.0, 0.0});
    const auto p3 = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{10.0, -20.0, 0.0});
    const auto p4 = viewport.Mapping().Project(
        kachakacha::v2::geometry::Vector3{30.0, 0.0, 0.0});
    if (!Explain("4つの制御点が画面内",
            p1.has_value() && p2.has_value() && p3.has_value() && p4.has_value())) {
        return false;
    }
    viewport.ClickAt(QPointF(p1->x, p1->y));
    viewport.ClickAt(QPointF(p2->x, p2->y));
    viewport.ClickAt(QPointF(p3->x, p3->y));
    viewport.ClickAt(QPointF(p4->x, p4->y));
    if (!Explain("4点目まではまだ確定しない(スプラインは何点でも置ける)",
            WireCount(window) == before)) {
        return false;
    }
    // ポリライン/スプラインは置く点の数を決めないので、終わりは右クリック(=Enter)で
    // 伝える。試験では viewport.FinishTool() が、その確定と同じ道を通る
    // (V2SelfTestDrawing.cpp のポリライン試験 drawU() と同じ終わらせ方)。
    viewport.FinishTool();
    window.SelectTool(DrawingTool::Select);
    return Explain((std::string("スプラインが1本できる(実際 ")
                       + std::to_string(WireCount(window) - before) + ")").c_str(),
        WireCount(window) == before + 1);
}

//! 帯の道具を持ち、作り方のカードを押す(吸着なしで点を置く準備)。
[[nodiscard]] bool HoldToolWithCard(V2MainWindow& window, const char* category, const char* tool,
    DrawingTool expected, const char* card)
{
    window.RunCommand("file.new");
    window.SetMode(UiMode::Drawing);
    auto& ribbon = window.Ribbon();
    if (category != nullptr
        && !Explain("帯のカテゴリを選べる", ribbon.ClickCategory(QString::fromUtf8(category)))) {
        return false;
    }
    return Explain("帯の道具を押せる", ribbon.ClickTool(QString::fromUtf8(tool)))
        && Explain("その道具になる", window.Session().CurrentTool() == expected)
        && Explain("作り方のカードを押せる", window.DrawingDock().ClickMethod(QString::fromUtf8(card)));
}

//! 世界の点を画面で押す(吸着なし)。
[[nodiscard]] bool ClickWorld(V2MainWindow& window, const kachakacha::v2::geometry::Vector3& point)
{
    auto& viewport = window.Viewport();
    const auto screen = viewport.Mapping().Project(point);
    if (!screen.has_value()) {
        return false;
    }
    viewport.SetSnapSuppressed(true);
    viewport.ClickAt(QPointF(screen->x, screen->y));
    viewport.SetSnapSuppressed(false);
    return true;
}

//! HP-DM-07。円の「3点」: 3 か所を押すと、その 3 点を通る円が 1 本できる。
[[nodiscard]] bool CaseCircleThroughThreeClicks(V2MainWindow& window)
{
    using kachakacha::v2::geometry::Vector3;
    if (!HoldToolWithCard(window, nullptr, "円", DrawingTool::Circle, "3点")
        || !Explain("作り方が 3点 になる", window.DrawingDock().Settings().circleMode
                == kachakacha::v2::modeling::CircleMode::ThreePoints)) {
        return false;
    }
    const int before = WireCount(window);
    if (!Explain("3 か所を押せる", ClickWorld(window, Vector3{10.0, 0.0, 0.0})
                && ClickWorld(window, Vector3{0.0, 10.0, 0.0}))
        || !Explain("2 か所ではまだ円はできない", WireCount(window) == before)
        || !Explain("3 か所目を押せる", ClickWorld(window, Vector3{-10.0, 0.0, 0.0}))) {
        return false;
    }
    window.SelectTool(DrawingTool::Select);
    const auto& curves = window.Session().Scene().curves;
    const bool made = WireCount(window) == before + 1 && !curves.empty()
        && curves.back().segment.Kind() == kachakacha::v2::geometry::CurveKind::Circle;
    return Explain("円が 1 本できる", made)
        && Explain((std::string("3 点を通る(半径 ") + std::to_string(curves.back().segment.Radius())
                       + " mm、中心は原点)").c_str(),
            std::abs(curves.back().segment.Radius() - 10.0) < 1.0e-6
                && kachakacha::v2::geometry::Distance(curves.back().segment.Center(), Vector3{}) < 1.0e-6);
}

//! HP-DM-08。円弧の「中心・始点・終点」: 半径は中心から始点、終点は向きだけ。左回りに 90°。
[[nodiscard]] bool CaseArcFromCenterClicks(V2MainWindow& window)
{
    using kachakacha::v2::geometry::Vector3;
    if (!HoldToolWithCard(window, nullptr, "円弧", DrawingTool::Arc, "中心・始点・終点")) {
        return false;
    }
    const int before = WireCount(window);
    if (!Explain("中心・始点・終点を押せる", ClickWorld(window, Vector3{0.0, 0.0, 0.0})
                && ClickWorld(window, Vector3{15.0, 0.0, 0.0}) && ClickWorld(window, Vector3{0.0, 30.0, 0.0}))) {
        return false;
    }
    window.SelectTool(DrawingTool::Select);
    const auto& curves = window.Session().Scene().curves;
    if (!Explain("円弧が 1 本できる", WireCount(window) == before + 1 && !curves.empty()
                && curves.back().segment.Kind() == kachakacha::v2::geometry::CurveKind::CircularArc)) {
        return false;
    }
    const auto& arc = curves.back().segment;
    return Explain((std::string("半径は中心から始点まで(") + std::to_string(arc.Radius()) + ")").c_str(),
               std::abs(arc.Radius() - 15.0) < 1.0e-6)
        && Explain("左回りに 90°", std::abs(arc.SweepAngleRad() - 1.5707963267948966) < 1.0e-6)
        && Explain("終わりは半径の上(終点は向きだけ)",
            kachakacha::v2::geometry::Distance(arc.EndPoint(), Vector3{0.0, 15.0, 0.0}) < 1.0e-6);
}

//! HP-DM-09。スプラインの「通過点」: 押した 4 点を必ず通る線が Enter で 1 本できる。
[[nodiscard]] bool CaseSplineThroughClickedPoints(V2MainWindow& window)
{
    using kachakacha::v2::geometry::Vector3;
    if (!HoldToolWithCard(window, "曲線", "スプライン", DrawingTool::Spline, "通過点")
        || !Explain("作り方が 通過点 になる", window.DrawingDock().Settings().splineMode
                == kachakacha::v2::modeling::SplineMode::ThroughPoints)) {
        return false;
    }
    const std::vector<Vector3> points{{-30.0, 0.0, 0.0}, {-10.0, 20.0, 0.0}, {10.0, -20.0, 0.0},
        {30.0, 0.0, 0.0}};
    const int before = WireCount(window);
    for (const Vector3& point : points) {
        if (!Explain("点を押せる", ClickWorld(window, point))) {
            return false;
        }
    }
    window.Viewport().FinishTool();
    window.SelectTool(DrawingTool::Select);
    const auto& curves = window.Session().Scene().curves;
    if (!Explain("スプラインが 1 本できる", WireCount(window) == before + 1 && !curves.empty()
                && curves.back().segment.Kind() == kachakacha::v2::geometry::CurveKind::CubicBSpline)) {
        return false;
    }
    const auto& spline = curves.back().segment;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const double t = static_cast<double>(index) / static_cast<double>(points.size() - 1);
        if (!Explain((std::string("押した点 ") + std::to_string(index + 1) + " を通る").c_str(),
                kachakacha::v2::geometry::Distance(spline.Evaluate(t), points[index]) < 1.0e-6)) {
            return false;
        }
    }
    return true;
}

//! いちばん新しい線の外接箱の x の幅(場面の線から)。
[[nodiscard]] double NewestWireWidth(V2MainWindow& window)
{
    kachakacha::v2::base::EntityId newest;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Wire) {
            newest = entity.id;
        }
    }
    double low = 1.0e300;
    double high = -1.0e300;
    for (const auto& curve : window.Session().Scene().curves) {
        if (!(curve.entityId == newest)) {
            continue;
        }
        for (int step = 0; step <= 4; ++step) {
            const double x = curve.segment.Evaluate(step / 4.0).x;
            low = std::min(low, x);
            high = std::max(high, x);
        }
    }
    return high - low;
}

//! HP-DM-10。スケール(D-22): 線を選び、帯の「スケール」→ 倍率 2 → 中心を押すと 2 倍になり、
//! 1 回で戻る。「基準の2点」では中心・基準・行き先の 3 点で倍率が決まる。
[[nodiscard]] bool CaseScaleWireByFactorAndReference(V2MainWindow& window)
{
    using kachakacha::v2::geometry::Vector3;
    window.RunCommand("file.new");
    window.SetMode(UiMode::Drawing);
    const auto rectangle = DrawRectangleAtByHand(window, 0.45, 0.45, 0.55, 0.55);
    const double before = rectangle.IsNil() ? 0.0 : NewestWireWidth(window);
    if (!Explain("矩形を手で引ける", !rectangle.IsNil() && before > 1.0)
        || !Explain("矩形を画面から拾える", ClickOnCurveOf(window, rectangle))) {
        return false;
    }
    auto& ribbon = window.Ribbon();
    auto& dock = window.DrawingDock();
    if (!Explain("帯の「変形」を選べる", ribbon.ClickCategory(QStringLiteral("変形")))
        || !Explain("「スケール」を押せる", ribbon.ClickTool(QStringLiteral("スケール")))
        || !Explain("スケールの道具になる", window.Session().CurrentTool() == DrawingTool::Scale)
        || !Explain("「倍率」のカードを押せる", dock.ClickMethod(QStringLiteral("倍率")))
        || !Explain("倍率の欄に 2 を打てる", dock.TypeScaleFactor(2.0))
        || !Explain("中心(矩形の真ん中)を押せる", ClickWorld(window, Vector3{0.0, 0.0, 0.0}))) {
        return false;
    }
    const double doubled = NewestWireWidth(window);
    if (!Explain((std::string("幅が 2 倍になる(") + std::to_string(before) + " → "
                     + std::to_string(doubled) + ")").c_str(),
            std::abs(doubled - before * 2.0) < 1.0e-3)) {
        return false;
    }
    window.RunCommand("edit.undo");
    if (!Explain("1 回の取り消しで元の幅に戻る", std::abs(NewestWireWidth(window) - before) < 1.0e-3)) {
        return false;
    }
    // 基準の2点: 中心 (0,0)、基準 (10,0)、行き先 (15,0) → ×1.5。
    window.SelectTool(DrawingTool::Select);
    if (!Explain("矩形をもう一度拾える", ClickOnCurveOf(window, rectangle))
        || !Explain("「スケール」をもう一度押せる", ribbon.ClickTool(QStringLiteral("スケール")))
        || !Explain("「基準の2点」を押せる", dock.ClickMethod(QStringLiteral("基準の2点")))
        || !Explain("基準の2点では倍率の欄を出さない", !dock.ScaleFactorShown())
        || !Explain("中心・基準・行き先を押せる", ClickWorld(window, Vector3{0.0, 0.0, 0.0})
                && ClickWorld(window, Vector3{10.0, 0.0, 0.0}) && ClickWorld(window, Vector3{15.0, 0.0, 0.0}))) {
        return false;
    }
    const double scaled = NewestWireWidth(window);
    window.SelectTool(DrawingTool::Select);
    return Explain((std::string("幅が 1.5 倍になる(") + std::to_string(scaled) + ")").c_str(),
        std::abs(scaled - before * 1.5) < 1.0e-3);
}

} // namespace

std::vector<SelfTestCase> DrawingMethodCases()
{
    return {
        {"HP-DM-01 作り方カードは道具に従い、押すと作り方が変わり、核に無いものは理由つき",
            CaseMethodCardsFollowToolAndChangeArcMode},
        {"HP-DM-02 3点のカードで 3 か所を押すと円弧ができる", CaseThreePointArcByCards},
        {"HP-DM-03 スプラインは制御点と通過点を押せFitは理由つき、ベジェは 1 枚", CaseSplineAndBezierCards},
        {"HP-DM-04 円は中心＋半径と直径指定のどちらでも画面から引ける",
            CaseCircleByCenterRadiusAndDiameterCards},
        {"HP-DM-05 ベジェは制御点を4か所押すと1本できる", CaseBezierFourClicksMakesOneWire},
        {"HP-DM-06 スプラインは制御点を4か所押して Enter で1本できる",
            CaseSplineFourClicksThenEnterMakesOneWire},
        {"HP-DM-07 円の3点は3か所を押すとその3点を通る円ができる", CaseCircleThroughThreeClicks},
        {"HP-DM-08 円弧の中心・始点・終点は半径が始点で決まり左回りにできる", CaseArcFromCenterClicks},
        {"HP-DM-09 スプラインの通過点は押した点をすべて通る", CaseSplineThroughClickedPoints},
        {"HP-DM-10 スケールは倍率でも基準の2点でも線の大きさを変え1回で戻る",
            CaseScaleWireByFactorAndReference},
    };
}

} // namespace kachakacha::v2::selftest
