//! 指示書 known_regressions(I-01)の退行を固定する試験(RG-*)。
//!
//! 15件のうち、他のケースが既にカバーしているものは REGRESSIONS.md に
//! マップするだけにして、ここへは重ねて置かない。ここに置くのは、
//! どのケースも触れていなかった3件だけである。
//!
//! すべて実際の viewport 操作・見えているボタン・帯のクリックで確かめる。
//! hidden widget を直に触るだけの試験は置かない(退行15番そのもの)。

#include "V2SelfTest.h"

#include "V2DrawingDock.h"
#include "V2MainWindow.h"
#include "V2OperationPanelHost.h"
#include "V2Ribbon.h"
#include "V2Viewport.h"

#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/UiMode.h"
#include "kachakacha/modeling/ToolController.h"

#include <QApplication>
#include <QSize>
#include <QString>

#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::Shelf;
using kachakacha::v2::app::UiMode;
using kachakacha::v2::modeling::DrawingTool;

//! RG-02。道具を先に構えたときの右の欄が、次の操作へ替わったら
//! 古いまま残らないこと(指示書 known_regressions #2)。
//!
//! 作図モードで円を持つ → 測定を重ねる(古い作図の棚が残らない)→
//! Esc で円へ戻ってから選択道具に替える(測定の棚が残らない)→
//! 部品モードへ移る(作図の棚が残らない)。
//!
//! `V2SelfTestScreen.cpp` の「右は道具に従う」試験が土台の部分は見ているが、
//! measure.open を挟んだ差し替えと `OperationHost().CurrentShelf()` そのものは
//! 見ていなかった。
[[nodiscard]] bool CaseOldRightPanelDoesNotLingerAfterToolSwitch(V2MainWindow& window)
{
    window.RunCommand("file.new");
    window.SetMode(UiMode::Drawing);
    window.SelectTool(DrawingTool::Circle);
    auto& host = window.OperationHost();
    if (!Explain("作図モードで円を持つと作図の棚", host.CurrentShelf() == Shelf::Drawing)
        || !Explain("道具も円のまま", window.DrawingDock().Tool() == DrawingTool::Circle)) {
        return false;
    }

    // 測定を重ねる。古い作図の棚が残ったまま測定の棚も出る、が退行だった。
    window.RunCommand("measure.open");
    if (!Explain((std::string("測定を開くと棚が測定に替わる(実際は ")
                     + std::to_string(static_cast<int>(host.CurrentShelf())) + ")").c_str(),
            host.CurrentShelf() == Shelf::Measure)
        || !Explain("古い作図の棚は残らない", !window.ShelfShown(Shelf::Drawing))) {
        return false;
    }

    // Esc で元の道具(円)へ戻ってから、選択道具に持ち替える。
    window.Viewport().PressEscape();
    window.SelectTool(DrawingTool::Select);
    if (!Explain("選択に持ち替えると編集の棚", host.CurrentShelf() == Shelf::Edit)
        || !Explain("測定の棚は残らない", !window.ShelfShown(Shelf::Measure))) {
        return false;
    }

    // 部品モードへ移る。作図の棚が残ったままではいけない。
    window.SetMode(UiMode::Part);
    if (!Explain("部品モードでは部品の棚", host.CurrentShelf() == Shelf::Part)) {
        return false;
    }
    return Explain("作図の棚は出ない", !window.ShelfShown(Shelf::Drawing));
}

//! RG-12。「まとまり」という言葉が、画面に出る文字のどこにも残っていないこと
//! (指示書 known_regressions #12。呼び方は「グループ」に統一する)。
[[nodiscard]] bool CaseNoGroupingWordInVisibleLabels(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const QString word = QString::fromUtf8("まとまり");

    // 上の帯の作業中グループの文言。
    if (!Explain("作業中グループの文言に「まとまり」が無い(既定)",
            !window.ActiveGroupText().contains(word))) {
        return false;
    }
    // 実際にグループを1つ作って、名前つきでも確かめる。
    window.RunCommand("group.create");
    if (!Explain("グループができる", !window.GroupItems().empty())) {
        return false;
    }
    window.SelectGroupCombo(window.GroupComboCount() - 1);
    if (!Explain("グループを作業中にしても「まとまり」が無い",
            !window.ActiveGroupText().contains(word))) {
        return false;
    }
    for (int index = 0; index < window.GroupComboCount(); ++index) {
        if (!Explain((std::string("上の帯のコンボ ") + std::to_string(index)
                         + " に「まとまり」が無い").c_str(),
                !window.GroupComboText(index).contains(word))) {
            return false;
        }
    }

    // 左の一覧の8つの節見出し(原点/作業面/グループ/ワイヤー/面/立体/近似/生成物)。
    for (int row = 0; row < window.GroupRowCount(); ++row) {
        if (!Explain((std::string("一覧の節 ") + std::to_string(row) + " に「まとまり」が無い")
                         .c_str(),
                !window.GroupRowText(row).contains(word))) {
            return false;
        }
    }

    // 右クリックの献立(「グループへ移動」であって「まとまりへ移動」ではない)。
    for (const QString& label : window.ExplorerMenuLabels()) {
        if (!Explain((std::string("献立の1行に「まとまり」が無い(") + label.toStdString() + ")")
                         .c_str(),
                !label.contains(word))) {
            return false;
        }
    }

    // 命令台帳の表示名・案内文・断り文言。メニュー・帯・右パネルはすべてここを指す。
    for (const auto& command : kachakacha::v2::app::CommandCatalog()) {
        const QString labelJa = QString::fromUtf8(command.labelJa.data(),
            static_cast<int>(command.labelJa.size()));
        const QString guideJa = QString::fromUtf8(command.operationGuideJa.data(),
            static_cast<int>(command.operationGuideJa.size()));
        const QString failureJa = QString::fromUtf8(command.predicateFailureJa.data(),
            static_cast<int>(command.predicateFailureJa.size()));
        if (!Explain((std::string("台帳 ") + std::string(command.id) + " の表示名に「まとまり」が無い")
                         .c_str(),
                !labelJa.contains(word))
            || !Explain((std::string("台帳 ") + std::string(command.id) + " の案内に「まとまり」が無い")
                            .c_str(),
                    !guideJa.contains(word))
            || !Explain((std::string("台帳 ") + std::string(command.id) + " の断り文言に「まとまり」が無い")
                            .c_str(),
                    !failureJa.contains(word))) {
            return false;
        }
    }
    return true;
}

//! RG-14。狭い画面(1280x720)でも、各モード・各カテゴリの道具ボタンが
//! 右端で切れないこと(指示書 known_regressions #14)。
//!
//! `V2SelfTestBasics.cpp` の「狭い画面でも部品がはみ出さない」は窓とビューポートの
//! 大きさだけを見ていて、帯の道具が右端で切れているかは見ていなかった
//! (`V2Ribbon::ToolsFitInWidth` は、これまでどの試験からも呼ばれていない)。
[[nodiscard]] bool CaseNarrowScreenKeepsRibbonToolsUsable(V2MainWindow& window)
{
    const QSize before = window.size();
    window.RunCommand("file.new");
    window.resize(1280, 720);
    QApplication::processEvents();

    const UiMode modes[] = {
        UiMode::Drawing, UiMode::Part, UiMode::Fabrication, UiMode::Output,
    };
    auto& ribbon = window.Ribbon();
    bool ok = true;
    for (const UiMode mode : modes) {
        window.SetMode(mode);
        QApplication::processEvents();
        const int categories = ribbon.CategoryCount();
        for (int index = 0; index < categories; ++index) {
            const QString label = ribbon.CategoryLabel(index);
            if (!Explain((std::string("カテゴリ「") + label.toStdString() + "」を押せる").c_str(),
                    ribbon.ClickCategory(label))) {
                ok = false;
                continue;
            }
            QApplication::processEvents();
            if (!Explain((std::string("1280px でも「") + label.toStdString()
                             + "」の道具が右端で切れない").c_str(),
                    ribbon.ToolsFitInWidth())) {
                ok = false;
            }
        }
    }
    if (!Explain("1280px でも 3D 画面が使える幅を保つ(400px 以上)",
            window.Viewport().width() >= 400)) {
        ok = false;
    }

    // 元の大きさへ戻す。後続のケースへ幅を持ち越さない。
    window.resize(before.width(), before.height());
    QApplication::processEvents();
    return ok;
}

} // namespace

//! RG-09。右は「いまの道具の1枚」。製作モードでも数の棚が2枚目として並ばず、
//! 「数の設定」を押したときだけ前に出る(指示書 C-09)。
[[nodiscard]] bool CaseRightPaneShowsOnePageAndNumbersHaveTheirOwnDoor(V2MainWindow& window)
{
    using kachakacha::v2::app::Shelf;
    window.RunCommand("file.new");
    for (const auto mode : {kachakacha::v2::app::UiMode::Drawing,
             kachakacha::v2::app::UiMode::Part, kachakacha::v2::app::UiMode::Fabrication,
             kachakacha::v2::app::UiMode::Output}) {
        window.SetMode(mode);
        if (!Explain((std::string("そのモードで数の棚は並ばない(")
                         + std::string(kachakacha::v2::app::UiModeNameJa(mode)) + ")").c_str(),
                !window.ShelfShown(Shelf::Parameter))) {
            return false;
        }
    }
    window.RunCommand("view.number_settings");
    if (!Explain("「数の設定」で数の棚が前に出る", window.ShelfShown(Shelf::Parameter))) {
        return false;
    }
    if (!Explain((std::string("何をする欄かを言う(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("数の設定")))) {
        return false;
    }
    // 道具を持ち替えれば、また「いまの道具の1枚」に戻る。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    return Explain("道具を持つと数の棚は引っ込む", !window.ShelfShown(Shelf::Parameter));
}

std::vector<SelfTestCase> RegressionCases()
{
    return {
        {"RG-09 右は1枚で、数の棚は「数の設定」でだけ前に出る",
            CaseRightPaneShowsOnePageAndNumbersHaveTheirOwnDoor},
        {"RG-02 道具を先に構えても古い右の棚が残らない",
            CaseOldRightPanelDoesNotLingerAfterToolSwitch},
        {"RG-12 画面の文字に「まとまり」が残っていない", CaseNoGroupingWordInVisibleLabels},
        {"RG-14 狭い画面でも帯の道具が右端で切れない", CaseNarrowScreenKeepsRibbonToolsUsable},
    };
}

} // namespace kachakacha::v2::selftest
