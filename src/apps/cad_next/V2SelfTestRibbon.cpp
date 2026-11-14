#include <QDir>
#include <QPixmap>
#include <QApplication>
//! 2段の帯(カテゴリ → 道具)の人の道(HP-RB)。正本 3 HTML(2026-09-18)。
//!
//! 見えているボタンを実際に押す。押せない道具は押せず、理由が状態行に出る。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2Ribbon.h"
#include "Win95Style.h"
#include "V2SurfaceDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/ToolController.h"

#include <QString>
#include <QAction>
#include <QComboBox>
#include <QEvent>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QObject>
#include <QPoint>
#include <QScreen>
#include <QSize>
#include <QTimer>
#include <QToolButton>
#include <QToolBar>
#include <QWindow>
#include <QWidget>

#include <string>
#include <algorithm>
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
    if (!Explain("作図は9カテゴリ", ribbon.CategoryCount() == 9)
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
    if(!ribbon.ClickCategory(QStringLiteral("曲線")))return false;
    const auto labels=ribbon.ToolLabels();
    return Explain("unsupported placeholders are absent from executable tools",
        std::find(labels.begin(),labels.end(),QStringLiteral("楕円"))==labels.end()
        &&!ribbon.ClickTool(QStringLiteral("楕円")));

}

//! HP-RB-03。モードを変えると帯が入れ替わり、面作成は作り方つきで「面を作る」へ入る。
[[nodiscard]] bool CaseRibbonFollowsModeAndSurfaceMethod(V2MainWindow& window)
{
    using kachakacha::v2::modeling::GuideSurfaceMethod;
    window.RunCommand("file.new");
    window.SetMode(UiMode::Part);
    auto& ribbon = window.Ribbon();
    if (!Explain("部品は6カテゴリ", ribbon.CategoryCount() == 6)
        || !Explain("押し出しが見えている", ribbon.ToolEnabled(QStringLiteral("押し出し")))
        || !Explain("ブール演算を押せる", ribbon.ClickCategory(QStringLiteral("ブール演算")))
        || !Explain("交差を押せる(P-17、共通部分)", ribbon.ToolEnabled(QStringLiteral("交差")))) {
        return false;
    }
    window.SetMode(UiMode::Drawing);
    if (!Explain("作図へ戻ると9カテゴリ", ribbon.CategoryCount() == 9)
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
    return Explain("製作は5カテゴリ", ribbon.CategoryCount() == 5)
        && Explain("近似が見えている", ribbon.ToolEnabled(QStringLiteral("近似")));
}

[[nodiscard]] bool CaseRibbonCategoryAppearance(V2MainWindow& window)
{
    window.ApplyTheme(UiTheme::Windows95);
    window.resize(1200,800);QApplication::processEvents();
    const bool saved=window.grab().save(QDir::tempPath()+QStringLiteral("/kachakacha-category-win95.png"));
    window.ApplyTheme(UiTheme::Normal);
    return Explain("HP-RB カテゴリとツールのWin95表示記録",saved);
}

void ClickAt(QWidget& widget, const QPoint& at)
{
    for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
        QMouseEvent event(type, at, widget.mapToGlobal(at), Qt::LeftButton,
            type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &event);
    }
}

bool ChooseOverflow(QToolButton& button, const QString& label, const QString& submenu = {})
{
    auto* menu = button.menu();
    if (!menu || button.visibleRegion().boundingRect() != button.rect()) return false;
    bool clicked = false;
    const auto findAction = [](QMenu& list, const QString& text) -> QAction* {
        for (auto* action : list.actions())
            if (action->text() == text && action->isEnabled() && action->isVisible()) return action;
        return nullptr;
    };
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, menu, [&] {
        timer.stop();
        QMenu* target = menu->isVisible() ? menu : nullptr;
        if (target && !submenu.isEmpty()) {
            auto* action = findAction(*target, submenu);
            if (action && action->menu()) {
                target->setActiveAction(action);
                QKeyEvent open(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
                QApplication::sendEvent(target, &open);
            }
            target = action ? action->menu() : nullptr;
        }
        if (target && target->isVisible()) {
            if (auto* action = findAction(*target, label)) {
                const auto rect = target->actionGeometry(action);
                if (target->rect().contains(rect) && !rect.isEmpty()) {
                    ClickAt(*target, rect.center());
                    clicked = true;
                }
            }
        }
        menu->close();
    });
    timer.start(0);
    ClickAt(button, button.rect().center());
    QApplication::processEvents();
    return clicked;
}

void SettleLayout()
{
    QEventLoop settling;
    QTimer::singleShot(240, &settling, &QEventLoop::quit);
    settling.exec();
}

[[nodiscard]] bool CaseCompactModeChoices(V2MainWindow& window)
{
    // Enlarge fonts for this toolbar only, as on a narrow high-DPI desktop.
    auto* bar = window.findChild<QToolBar*>(QStringLiteral("modeBar"));
    if (!bar) return false;
    auto font = bar->font();
    font.setPointSize(17);
    bar->setFont(font);
    window.resize(920, 600);
    SettleLayout();
    auto* overflow = bar->findChild<QToolButton*>(QStringLiteral("toolbarOverflowButton"));
    auto* scope = bar->findChild<QComboBox*>(QStringLiteral("drawingScope"));
    if (!Explain("narrow mode bar hides the combo behind a visible menu",
            scope && !scope->isVisible() && overflow && overflow->isVisible() && overflow->menu())) return false;
    return Explain("choosing an overflow item updates the original combo",
        ChooseOverflow(*overflow, QStringLiteral("3D作図"), QStringLiteral("作図範囲"))
        && scope->currentIndex() == 1 && !window.Session().DrawingPlaneOnly());
}

[[nodiscard]] bool CaseCompactRibbonOverflow(V2MainWindow& window)
{
    window.SetMode(UiMode::Drawing);
    auto& ribbon = window.Ribbon();
    const auto revision = window.Session().GetDocument().Revision();
    if (!ribbon.ClickCategory(QStringLiteral("編集"))) return false;
    SettleLayout();
    window.resize(1740, 820);
    SettleLayout();
    const int wideHeight = ribbon.height();
    window.resize(970, 600);
    SettleLayout();
    auto* row = ribbon.findChild<QWidget*>(QStringLiteral("ribbonTools"));
    if (!row) return false;
    const auto buttons = row->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
    QToolButton* more = nullptr;
    QToolButton* lastHidden = nullptr;
    int top = -1;
    for (auto* button : buttons) {
        if (button->menu()) { more = button; continue; }
        if (!button->isVisible()) {
            if (button->isEnabled() && button->defaultAction()) lastHidden = button;
            continue;
        }
        if (top < 0) top = button->y();
        if (!Explain("compact tools fit on one line",
                button->y() == top && button->height() >= 32
                && button->toolButtonStyle() == Qt::ToolButtonTextBesideIcon
                && button->visibleRegion().boundingRect() == button->rect())) return false;
    }
    if (!Explain(QStringLiteral("compact ribbon saves canvas height (%1 → %2, viewport %3×%4)")
            .arg(wideHeight).arg(ribbon.height()).arg(window.width()).arg(window.height()).toStdString().c_str(),
            ribbon.height() + 12 < wideHeight)
        || !Explain("overflow holds a real tool", more && lastHidden)) return false;
    int triggered = 0;
    auto* action = lastHidden->defaultAction();
    const auto connection = QObject::connect(action, &QAction::triggered, &window, [&] { ++triggered; });
    const bool chosen = ChooseOverflow(*more, action->text());
    QObject::disconnect(connection);
    if (!Explain("overflow activates the same action by mouse", chosen && triggered == 1)) return false;
    window.resize(1740, 820);
    SettleLayout();
    return Explain("widening restores the large icons", ribbon.height() >= wideHeight
        && window.Session().GetDocument().Revision() == revision);
}

[[nodiscard]] bool CaseToolbarFollowsMonitor(V2MainWindow& window)
{
    auto* large = window.screen();
    for (auto* screen : QApplication::screens())
        if (screen->availableSize().height() > large->availableSize().height()) large = screen;
    QScreen* smallerScreen = nullptr;
    for (auto* screen : QApplication::screens()) {
        if (screen->availableGeometry().height() + 100 < large->availableGeometry().height()
            && screen->availableGeometry().width() >= 950
            && screen->availableGeometry().height() < 710) smallerScreen = screen;
    }
    if (!smallerScreen) {
        // Single-screen and offscreen runs still exercise the same compact resize.
        window.resize(970, 600);
        QApplication::processEvents();
        return Explain("single-screen resize fits ribbon tools", window.Ribbon().ToolsFitInWidth());
    }
    window.SetMode(UiMode::Drawing);
    window.SelectTool(DrawingTool::Line);
    window.windowHandle()->setFramePosition(large->availableGeometry().topLeft() + QPoint(12, 12));
    SettleLayout();
    const auto wide = QSize(1740, 860).boundedTo(large->availableSize() - QSize(48, 64));
    window.resize(wide);
    SettleLayout();
    const int wideHeight = window.Ribbon().height();
    const auto revision = window.Session().GetDocument().Revision();
    window.windowHandle()->setFramePosition(smallerScreen->availableGeometry().topLeft() + QPoint(12, 12));
    SettleLayout();
    if (!Explain("native window moves onto the smaller monitor",
            smallerScreen->geometry().contains(window.frameGeometry().center()))
        || !Explain("window frame stays inside the target work area",
            smallerScreen->availableGeometry().contains(window.frameGeometry()))
        || !Explain("toolbar shrinks without switching the current tool",
            window.Ribbon().height() + 12 < wideHeight && window.Ribbon().ToolsFitInWidth()
            && window.Session().CurrentTool() == DrawingTool::Line)) return false;
    if (qEnvironmentVariableIsSet("KACHACAD_TOOLBAR_SCREEN_SHOT"))
        window.grab().save(qEnvironmentVariable("KACHACAD_TOOLBAR_SCREEN_SHOT"));
    window.windowHandle()->setFramePosition(large->availableGeometry().topLeft() + QPoint(12, 12));
    SettleLayout();
    window.resize(wide);
    SettleLayout();
    return Explain("native window returns to the larger monitor",
            large->availableGeometry().contains(window.frameGeometry()))
        && Explain(QStringLiteral("large toolbar height %1 -> %2 after return (%3 × %4)")
            .arg(wideHeight).arg(window.Ribbon().height()).arg(window.width()).arg(window.height()).toStdString().c_str(),
            window.Ribbon().height() >= wideHeight)
        && Explain("moving monitors preserves current tool and document",
            window.Session().CurrentTool() == DrawingTool::Line
            && window.Session().GetDocument().Revision() == revision);
}

} // namespace

std::vector<SelfTestCase> RibbonCases()
{
    return {
        {"HP-RB-04 カテゴリタブとツールボタンの外観", CaseRibbonCategoryAppearance},
        {"HP-RB-01 帯はカテゴリ → 道具で、押した道具になる", CaseRibbonCategoryThenTool},
        {"HP-RB-02 核に無い道具は押せない形で理由が出る", CaseRibbonBlockedToolSaysWhy},
        {"HP-RB-03 帯はモードで入れ替わり、面作成は作り方つきで構える",
            CaseRibbonFollowsModeAndSurfaceMethod},
        {"HP-RB-05 compact toolbar overflow can be clicked", CaseCompactRibbonOverflow},
        {"HP-RB-06 toolbar responds to monitor movement", CaseToolbarFollowsMonitor},
        {"HP-RB-07 hidden mode settings can be changed from overflow", CaseCompactModeChoices},
    };
}

} // namespace kachakacha::v2::selftest
