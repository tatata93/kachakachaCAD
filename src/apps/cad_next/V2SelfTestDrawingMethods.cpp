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
    if (!Explain("「中心・始点・終点」は押せない形", !dock.MethodEnabled(QStringLiteral("中心・始点・終点")))
        || !Explain("押しても偽", !dock.ClickMethod(QStringLiteral("中心・始点・終点")))
        || !Explain("理由が状態行に出る", window.StatusText().contains(QStringLiteral("まだ作れません")))
        || !Explain("作り方は変わらない", dock.Settings().arcMode == ArcMode::EndpointsAndRadius)
        || !Explain("ツールチップにも理由",
            dock.MethodTip(QStringLiteral("中心・始点・終点")).contains(QStringLiteral("核")))) {
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
        && Explain("3点円は押せない形", !dock.MethodEnabled(QStringLiteral("3点")))
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

//! HP-DM-03。スプラインは制御点だけ押せ、通過点と Fit は理由つきで押せない。ベジェは 1 枚。
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
        || !Explain("通過点は押せない", !dock.MethodEnabled(QStringLiteral("通過点")))
        || !Explain("近似 / Fit は押せない", !dock.MethodEnabled(QStringLiteral("近似 / Fit")))) {
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

} // namespace

std::vector<SelfTestCase> DrawingMethodCases()
{
    return {
        {"HP-DM-01 作り方カードは道具に従い、押すと作り方が変わり、核に無いものは理由つき",
            CaseMethodCardsFollowToolAndChangeArcMode},
        {"HP-DM-02 3点のカードで 3 か所を押すと円弧ができる", CaseThreePointArcByCards},
        {"HP-DM-03 スプラインは制御点だけ押せ、ベジェは 1 枚", CaseSplineAndBezierCards},
    };
}

} // namespace kachakacha::v2::selftest
