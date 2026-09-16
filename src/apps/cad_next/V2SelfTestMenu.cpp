//! 上段メニューを人の目とマウスの条件で確かめる。

#include "V2SelfTest.h"

#include "V2MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>

namespace kachakacha::v2::selftest {
namespace {

[[nodiscard]] bool OpenMenuByMouse(QMenuBar& bar, QAction& action)
{
    const QPoint at = bar.actionGeometry(&action).center();
    const QPoint global = bar.mapToGlobal(at);
    QMouseEvent move(QEvent::MouseMove, QPointF(at), QPointF(global), Qt::NoButton,
        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&bar, &move);
    const bool hovered = bar.activeAction() == &action;
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(at), QPointF(global),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&bar, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(at), QPointF(global),
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&bar, &release);
    QApplication::processEvents();
    const bool opened = action.menu() != nullptr && action.menu()->isVisible();
    if (action.menu() != nullptr) {
        action.menu()->hide();
    }
    return hovered && opened;
}

[[nodiscard]] bool CaseMenuBarIsReadableAndMouseReachable(V2MainWindow& window)
{
    window.resize(1024, 600);
    for (const UiTheme theme : {UiTheme::Normal, UiTheme::Windows95}) {
        window.ApplyTheme(theme);
        QApplication::processEvents();
        QMenuBar* bar = window.menuBar();
        const QList<QAction*> actions = bar->actions();
        if (!Explain("上段メニューは8分類", actions.size() == 8)) {
            return false;
        }
        int previousRight = -1;
        for (QAction* action : actions) {
            const QRect area = bar->actionGeometry(action);
            QString visibleText = action->text();
            visibleText.remove('&');
            const int textWidth = bar->fontMetrics().horizontalAdvance(visibleText);
            if (!Explain("メニュー文字が必要幅を持つ", area.width() >= textWidth + 8)
                || !Explain("メニュー同士が重ならない", area.left() > previousRight)
                || !Explain("中心を押すとそのメニューに当たる",
                    bar->actionAt(area.center()) == action)) {
                return false;
            }
            previousRight = area.right();
        }
        // 座標判定だけでなく、Qtのマウス移動・押下・解放を実際に通す。
        if (!Explain("左端メニューをマウスで開ける",
                OpenMenuByMouse(*bar, *actions.front()))
            || !Explain("右端メニューをマウスで開ける",
                OpenMenuByMouse(*bar, *actions.back()))) {
            return false;
        }
    }
    window.ApplyTheme(UiTheme::Normal);
    return true;
}

[[nodiscard]] bool CaseDetachedSourceDocksStayHidden(V2MainWindow& window)
{
    QApplication::processEvents();
    for (QDockWidget* dock : window.findChildren<QDockWidget*>()) {
        const QRect inWindow(dock->mapTo(&window, QPoint(0, 0)), dock->size());
        if (dock->isVisible() && inWindow.topLeft() == QPoint(0, 0)
            && inWindow.size() == QSize(100, 30)) {
            return Explain("統合前の空パネルが左上へ重ならない", false);
        }
    }
    return Explain("統合前の空パネルが左上へ重ならない", true);
}

} // namespace

std::vector<SelfTestCase> MenuCases()
{
    return {
        {"メニュー文字が潰れずマウスで開ける",
            &CaseMenuBarIsReadableAndMouseReachable},
        {"統合前の空パネルを左上に残さない", &CaseDetachedSourceDocksStayHidden},
    };
}

} // namespace kachakacha::v2::selftest
