//! 2段の帯(カテゴリ → 道具)の人の道(HP-RB)。正本 3 HTML(2026-09-18)。
//!
//! 見えているボタンを実際に押す。押せない道具は押せず、理由が状態行に出る。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2Ribbon.h"
#include "V2SurfaceDock.h"

#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/ToolController.h"

#include <QString>

#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::Shelf;
using kachakacha::v2::app::UiMode;
using kachakacha::v2::modeling::DrawingTool;

//! HP-RB-01。カテゴリを押すと下段が入れ替わり、道具を押すとその道具になる。
[[nodiscard]] bool CaseRibbonCategoryThenTool(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SetMode(UiMode::Drawing);
    auto& ribbon = window.Ribbon();
    if (!Explain("作図は8カテゴリ", ribbon.CategoryCount() == 8)
        || !Explain("最初は基本作図", ribbon.CategoryLabel(ribbon.CurrentCategory()) == QStringLiteral("基本作図"))
        || !Explain("「円」が見えている", ribbon.ToolEnabled(QStringLiteral("円")))
        || !Explain("「円」を押せる", ribbon.ClickTool(QStringLiteral("円")))
        || !Explain("円の道具になる", window.Session().CurrentTool() == DrawingTool::Circle)) {
        return false;
    }
    if (!Explain("「曲線」を押せる", ribbon.ClickCategory(QStringLiteral("曲線")))
        || !Explain("下段がベジェ/スプラインになる",
            ribbon.ToolEnabled(QStringLiteral("ベジェ")) && !ribbon.ToolEnabled(QStringLiteral("円")))
        || !Explain("「スプライン」を押せる", ribbon.ClickTool(QStringLiteral("スプライン")))
        || !Explain("スプラインの道具になる", window.Session().CurrentTool() == DrawingTool::Spline)) {
        return false;
    }
    // 近道で道具を持つと、その道具のカテゴリが前に出る(帯と道具が食い違わない)。
    window.SelectTool(DrawingTool::Line);
    return Explain("線を持つと基本作図が前に出る",
        ribbon.CategoryLabel(ribbon.CurrentCategory()) == QStringLiteral("基本作図"));
}

//! HP-RB-02。核に無い道具は押せない形で、理由が日本語で出る。「押せるが何も起きない」は無い。
[[nodiscard]] bool CaseRibbonBlockedToolSaysWhy(V2MainWindow& window)
{
    window.SetMode(UiMode::Drawing);
    auto& ribbon = window.Ribbon();
    if (!Explain("「変形」を押せる", ribbon.ClickCategory(QStringLiteral("変形")))
        || !Explain("「スケール」は押せない形", !ribbon.ToolEnabled(QStringLiteral("スケール")))
        || !Explain("理由が付いている",
            ribbon.ToolTip(QStringLiteral("スケール")).contains(QStringLiteral("まだ")))) {
        return false;
    }
    const DrawingTool before = window.Session().CurrentTool();
    const bool pressed = ribbon.ClickTool(QStringLiteral("スケール"));
    return Explain("押しても押したことにならない", !pressed)
        && Explain("道具は変わらない", window.Session().CurrentTool() == before)
        && Explain("状態行に理由が出る",
            window.StatusText().contains(QStringLiteral("スケール")));
}

//! HP-RB-03。モードを変えると帯が入れ替わり、面作成は作り方つきで「面を作る」へ入る。
[[nodiscard]] bool CaseRibbonFollowsModeAndSurfaceMethod(V2MainWindow& window)
{
    using kachakacha::v2::modeling::GuideSurfaceMethod;
    window.RunCommand("file.new");
    window.SetMode(UiMode::Part);
    auto& ribbon = window.Ribbon();
    if (!Explain("部品は5カテゴリ", ribbon.CategoryCount() == 5)
        || !Explain("押し出しが見えている", ribbon.ToolEnabled(QStringLiteral("押し出し")))
        || !Explain("ブール演算を押せる", ribbon.ClickCategory(QStringLiteral("ブール演算")))
        || !Explain("交差を押せる(P-17、共通部分)", ribbon.ToolEnabled(QStringLiteral("交差")))) {
        return false;
    }
    window.SetMode(UiMode::Drawing);
    if (!Explain("作図へ戻ると8カテゴリ", ribbon.CategoryCount() == 8)
        || !Explain("「面作成」を押せる", ribbon.ClickCategory(QStringLiteral("面作成")))
        || !Explain("「ロフト面」を押せる", ribbon.ClickTool(QStringLiteral("ロフト面")))
        || !Explain("面を作るの棚が構える", window.ShelfShown(Shelf::Surface))
        || !Explain("作り方はロフト", window.SurfaceInput().method == GuideSurfaceMethod::LoftSections)
        || !Explain("「平面」を押すと作り方が平面に変わる(入力は捨てない)",
            ribbon.ClickTool(QStringLiteral("平面"))
                && window.SurfaceInput().method == GuideSurfaceMethod::PlanarBoundary)) {
        return false;
    }
    if (!Explain("Escでやめられる", window.HandleToolKey(Qt::Key_Escape, nullptr))) {
        return false;
    }
    window.SetMode(UiMode::Fabrication);
    return Explain("製作は4カテゴリ", ribbon.CategoryCount() == 4)
        && Explain("近似が見えている", ribbon.ToolEnabled(QStringLiteral("近似")));
}

} // namespace

std::vector<SelfTestCase> RibbonCases()
{
    return {
        {"HP-RB-01 帯はカテゴリ → 道具で、押した道具になる", CaseRibbonCategoryThenTool},
        {"HP-RB-02 核に無い道具は押せない形で理由が出る", CaseRibbonBlockedToolSaysWhy},
        {"HP-RB-03 帯はモードで入れ替わり、面作成は作り方つきで構える",
            CaseRibbonFollowsModeAndSurfaceMethod},
    };
}

} // namespace kachakacha::v2::selftest
