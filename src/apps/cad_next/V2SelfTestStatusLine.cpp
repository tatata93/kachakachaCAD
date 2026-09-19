//! 状態行・HUD・測定の重ね道具の人の道(HP-ST)。正本 3 HTML(2026-09-18)、指示書 C-11 / C-12 / C-16。
//!
//! 下の帯の左が「モード ｜ 道具」、右が「座標 ｜ Grid ｜ Snap ｜ キー」であること。
//! 3D の左上に同じ道具と案内が出ること。線を引いている途中で測定を重ね、Esc で線へ戻ること。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2OperationPanelHost.h"
#include "V2Viewport.h"

#include "kachakacha/app/UiMode.h"
#include "kachakacha/modeling/ToolController.h"

#include <QPointF>
#include <QRectF>
#include <QString>

#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::UiMode;
using kachakacha::v2::modeling::DrawingTool;

//! HP-ST-01。状態行の左右と HUD が、いまのモード・道具・吸着を映す。
[[nodiscard]] bool CaseStatusLineShowsModeToolAndSnap(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SetMode(UiMode::Drawing);
    window.SelectTool(DrawingTool::Line);
    // 道具の名前は core(DrawingToolNameJa = 「直線」)のもの。帯の短い言葉(「線」)ではない。
    const QString expectedLeft = QStringLiteral("作図 ｜ ")
        + QString::fromUtf8(std::string(kachakacha::v2::modeling::DrawingToolNameJa(DrawingTool::Line)).c_str());
    const QString left = window.StatusLeftText();
    if (!Explain((std::string("左は「作図 ｜ 直線」(実際 ") + left.toStdString() + ")").c_str(),
            left == expectedLeft)) {
        return false;
    }
    QString right = window.StatusRightText();
    if (!Explain((std::string("右に Grid と Snap とキー(実際 ") + right.toStdString() + ")").c_str(),
            right.contains(QStringLiteral("Grid")) && right.contains(QStringLiteral("Snap ON"))
                && right.contains(QStringLiteral("Esc")))) {
        return false;
    }
    // カーソルを動かすと座標が出る。
    auto& viewport = window.Viewport();
    viewport.HoverAt(QPointF(viewport.width() * 0.5, viewport.height() * 0.5));
    window.RefreshStatusLine();
    right = window.StatusRightText();
    if (!Explain((std::string("座標が出る(実際 ") + right.toStdString() + ")").c_str(),
            right.startsWith(QStringLiteral("U ")))) {
        return false;
    }
    // 吸着を切ると Snap OFF。
    window.RunCommand("snap.toggle");
    if (!Explain("Snap OFF になる", window.StatusRightText().contains(QStringLiteral("Snap OFF")))) {
        window.RunCommand("snap.toggle");
        return false;
    }
    window.RunCommand("snap.toggle");
    // HUD の1行目はモード › 道具。枠は左上で、画面に収まる。
    const auto& hud = viewport.HudLines();
    if (!Explain("HUD の1行目が「作図 › 直線」",
            !hud.empty() && hud.front() == QStringLiteral("作図 › 直線"))) {
        return false;
    }
    const QRectF box = viewport.HudRect();
    if (!Explain("HUD は左上にあり画面に収まる",
            box.left() >= 0.0 && box.top() >= 0.0 && box.top() < 40.0
                && box.right() <= viewport.width() && box.bottom() <= viewport.height())) {
        return false;
    }
    // 右の欄の見出しの下と HUD の2行目にも、帯と同じ案内(3か所で食い違わない)。
    window.SelectTool(DrawingTool::Circle);
    const QString hint = window.StatusText();
    return Explain("案内が空でない", !hint.isEmpty())
        && Explain("右の欄の案内も同じ", window.OperationHost().HintText() == hint)
        && Explain("HUD の2行目も同じ案内",
            viewport.HudLines().size() >= 2 && viewport.HudLines()[1] == hint);
}

//! HP-ST-02。線の道具のまま測定を重ね、Esc で線へ戻る(道具を捨てない)。
[[nodiscard]] bool CaseMeasureOverlayReturnsToRunningTool(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SetMode(UiMode::Drawing);
    window.SelectTool(DrawingTool::Line);
    window.RunCommand("measure.open");
    if (!Explain("測定の道具になる", window.Session().CurrentTool() == DrawingTool::Measure)
        || !Explain("戻り先が線", window.ToolBeforeMeasure().has_value()
            && *window.ToolBeforeMeasure() == DrawingTool::Line)) {
        return false;
    }
    bool hudSaysResume = false;
    for (const QString& line : window.Viewport().HudLines()) {
        if (line.contains(QStringLiteral("測定中")) && line.contains(QStringLiteral("線"))) {
            hudSaysResume = true;
        }
    }
    if (!Explain("HUD に「測定中(Esc で線へ戻る)」", hudSaysResume)) {
        return false;
    }
    // Esc は 3D の普段の道(EscapeAction)を通る。
    (void)window.Viewport().PressEscape();
    if (!Explain((std::string("Esc で線へ戻る(実際 ")
                     + std::string(kachakacha::v2::modeling::DrawingToolNameJa(
                         window.Session().CurrentTool()))
                     + ")").c_str(),
            window.Session().CurrentTool() == DrawingTool::Line)
        || !Explain("戻り先は忘れる", !window.ToolBeforeMeasure().has_value())
        || !Explain("戻ったと言う", window.StatusText().contains(QStringLiteral("戻りました")))) {
        return false;
    }
    // 選択道具から測定へ入ったときは、Esc でふだんどおり選択へ。
    window.SelectTool(DrawingTool::Select);
    window.RunCommand("measure.open");
    if (!Explain("選択からの測定は戻り先を持たない", !window.ToolBeforeMeasure().has_value())) {
        return false;
    }
    (void)window.Viewport().PressEscape();
    return Explain("Esc で選択へ", window.Session().CurrentTool() == DrawingTool::Select);
}

//! HP-ST-03。測定の途中で別の道具を自分で選んだら、戻り先は捨てる(勝手に戻らない)。
[[nodiscard]] bool CaseMeasureOverlayForgetsWhenToolChanged(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SelectTool(DrawingTool::Circle);
    window.RunCommand("measure.open");
    if (!Explain("戻り先が円", window.ToolBeforeMeasure().has_value()
            && *window.ToolBeforeMeasure() == DrawingTool::Circle)) {
        return false;
    }
    window.SelectTool(DrawingTool::Rectangle);
    if (!Explain("別の道具を選ぶと戻り先を忘れる", !window.ToolBeforeMeasure().has_value())) {
        return false;
    }
    (void)window.Viewport().PressEscape();
    return Explain("Esc は選択へ", window.Session().CurrentTool() == DrawingTool::Select);
}

} // namespace

std::vector<SelfTestCase> StatusLineCases()
{
    return {
        {"HP-ST-01 状態行と HUD がモード・道具・吸着・座標を映す", CaseStatusLineShowsModeToolAndSnap},
        {"HP-ST-02 測定を重ねて Esc で元の道具へ戻る", CaseMeasureOverlayReturnsToRunningTool},
        {"HP-ST-03 測定の途中で道具を選び直せば戻り先を忘れる", CaseMeasureOverlayForgetsWhenToolChanged},
    };
}

} // namespace kachakacha::v2::selftest
