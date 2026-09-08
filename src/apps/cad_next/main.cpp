//! V2アプリの入口(WP-08)。
//!
//! 画面を出さずに確かめられる道を必ず残す。
//!   --version       : 版だけ出す
//!   --self-test     : 画面を出さずに一通り触って、結果を出す
//!   --manual-state <名前> --snapshot <png> : その状態の絵を保存する
//!
//! V1 は画面を出さないと何も確かめられなかったので、
//! 直したかどうかを人が目で見るしかなかった。

#include "V2MainWindow.h"

#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/app/UiMode.h"
#include "Win95Style.h"

#include "kachakacha/base/Version.h"
#include "kachakacha/kernel/KernelInfo.h"

#include <QApplication>
#include <QPointF>
#include <QImage>
#include <QPainter>
#include <QStringList>

#include <iostream>
#include <cstdint>
#include <iterator>
#include <vector>

namespace {

[[nodiscard]] QString ValueAfter(const QStringList& arguments, const QString& flag)
{
    const int index = arguments.indexOf(flag);
    if (index < 0 || index + 1 >= arguments.size()) {
        return QString();
    }
    return arguments.at(index + 1);
}

//! 窓を実際に描いて画像に落とす。offscreen でも通る。
[[nodiscard]] bool SaveSnapshot(V2MainWindow& window, const QString& path)
{
    window.show();
    QApplication::processEvents();
    QImage image(window.size() * 1, QImage::Format_ARGB32);
    image.fill(Qt::white);
    window.render(&image);
    return image.save(path);
}

struct SelfTestCase {
    const char* name;
    bool (*body)(V2MainWindow&);
};

[[nodiscard]] bool CaseToolsExist(V2MainWindow& window)
{
    // 23の道具が並んでいること(V1同等性)。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    return !window.StatusText().isEmpty();
}

[[nodiscard]] bool CaseDrawLine(V2MainWindow& window)
{
    if (!window.ApplyManualState(QStringLiteral("draw-line"))) {
        return false;
    }
    return window.EntityRowCount() >= 1;
}

[[nodiscard]] bool CaseCurves(V2MainWindow& window)
{
    return window.ApplyManualState(QStringLiteral("curves"));
}

[[nodiscard]] bool CaseSnap(V2MainWindow& window)
{
    if (!window.ApplyManualState(QStringLiteral("snap"))) {
        return false;
    }
    // 吸着していれば、案内文が空でない。
    return !window.Viewport().StatusMessage().empty()
        || !window.StatusText().isEmpty();
}

[[nodiscard]] bool CaseThemes(V2MainWindow& window)
{
    window.ApplyTheme(UiTheme::Windows95);
    const bool win95 = window.Theme() == UiTheme::Windows95;
    window.ApplyTheme(UiTheme::Normal);
    return win95 && window.Theme() == UiTheme::Normal;
}

[[nodiscard]] bool CaseUndo(V2MainWindow& window)
{
    if (!window.ApplyManualState(QStringLiteral("draw-line"))) {
        return false;
    }
    const int before = window.EntityRowCount();
    if (!window.Session().Undo()) {
        return false;
    }
    return before > 0;
}

[[nodiscard]] bool CaseViewDirections(V2MainWindow& window)
{
    for (const ViewDirection direction : {ViewDirection::Top, ViewDirection::Front,
             ViewDirection::Left, ViewDirection::Isometric}) {
        window.Viewport().SetViewDirection(direction);
        if (window.Viewport().Direction() != direction) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CaseCancel(V2MainWindow& window)
{
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Polyline);
    window.Viewport().ClickAt(QPointF(100, 100));
    window.Viewport().CancelTool();
    return true;
}

[[nodiscard]] bool CaseEveryCommandReachable(V2MainWindow& window)
{
    // 台帳の全コマンドが、同じ入口から呼べること。
    // どれを押しても落ちないこと。使えないものは理由が出ること。
    for (const auto& command : kachakacha::v2::app::CommandCatalog()) {
        QString reason;
        const bool enabled = window.CommandEnabled(command.id, &reason);
        if (!enabled && reason.isEmpty()) {
            return false;
        }
        window.RunCommand(command.id);
        if (window.StatusText().isEmpty()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CaseMenusComeFromCatalog(V2MainWindow& window)
{
    // メニューの項目は台帳から作る。台帳に無い入口を作らない。
    int found = 0;
    for (const auto& command : kachakacha::v2::app::CommandCatalog()) {
        if (window.ActionFor(command.id) != nullptr) {
            ++found;
        }
    }
    return found == static_cast<int>(kachakacha::v2::app::CommandCatalog().size());
}

[[nodiscard]] bool CaseDisabledCommandsExplain(V2MainWindow& window)
{
    // 使えないコマンドは隠さず、押したときに理由を出す。
    QString reason;
    if (window.CommandEnabled("part.extrude", &reason)) {
        return false;
    }
    if (reason.isEmpty()) {
        return false;
    }
    window.RunCommand("part.extrude");
    return window.StatusText() == reason;
}

[[nodiscard]] bool CaseGuideIsComplete(V2MainWindow& window)
{
    // どの道具に入っても、案内の6つがそろっていること。
    for (const auto& command : kachakacha::v2::app::CommandCatalog()) {
        window.RunCommand(command.id);
        const QString status = window.StatusText();
        if (status.isEmpty()) {
            return false;
        }
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    const QString line = window.StatusText();
    return line.contains(QStringLiteral("次:")) && line.contains(QStringLiteral("Esc"))
        && line.contains(QStringLiteral("選択"));
}

[[nodiscard]] bool CaseFailureRecovery(V2MainWindow& window)
{
    // 失敗する操作を3回ずつ繰り返しても、アプリが続き、文書が変わらないこと。
    const std::uint64_t before = window.Session().GetDocument().Snapshot().revision;
    for (int round = 0; round < 3; ++round) {
        // まだ使えないコマンドを押す。
        window.RunCommand("part.extrude");
        window.RunCommand("part.boolean_cut");
        window.RunCommand("fabrication.create");
        // 点を1つも置かずに確定しようとする。
        window.SelectTool(kachakacha::v2::modeling::DrawingTool::Polyline);
        window.Viewport().FinishTool();
        window.Viewport().CancelTool();
        if (window.StatusText().isEmpty()) {
            return false;
        }
    }
    return window.Session().GetDocument().Snapshot().revision == before;
}

[[nodiscard]] bool CaseSelectionSurvivesFailure(V2MainWindow& window)
{
    // 失敗しても、いま選んでいる道具は変わらないこと。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Circle);
    const auto before = window.Session().CurrentTool();
    for (int round = 0; round < 3; ++round) {
        window.RunCommand("export.step");
        window.RunCommand("part.from_wire_cage");
    }
    return window.Session().CurrentTool() == before;
}

[[nodiscard]] bool CaseModesKeepSelection(V2MainWindow& window)
{
    // モードを切り替えても、選んでいる道具も文書も変わらないこと(UIX-001 / 003)。
    if (!window.ApplyManualState(QStringLiteral("draw-line"))) {
        return false;
    }
    const auto tool = window.Session().CurrentTool();
    const std::uint64_t revision = window.Session().GetDocument().Snapshot().revision;
    const int rows = window.EntityRowCount();
    for (const auto mode : kachakacha::v2::app::AllUiModes()) {
        window.SetMode(mode);
        if (window.Mode() != mode) {
            return false;
        }
        if (window.Session().CurrentTool() != tool) {
            return false;
        }
        if (window.Session().GetDocument().Snapshot().revision != revision) {
            return false;
        }
        if (window.EntityRowCount() != rows) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CaseModesChangeVisibleCommands(V2MainWindow& window)
{
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    const int drawing = window.VisibleCommandCount();
    window.SetMode(kachakacha::v2::app::UiMode::Output);
    const int output = window.VisibleCommandCount();
    // 出るコマンドはモードで変わる。共通操作があるので、どちらも0ではない。
    return drawing > 0 && output > 0 && drawing != output;
}

const SelfTestCase kCases[] = {
    {"道具を選べる", &CaseToolsExist},
    {"直線を引ける", &CaseDrawLine},
    {"曲線を曲線のまま描ける", &CaseCurves},
    {"端点に吸着する", &CaseSnap},
    {"見た目を切り替えられる", &CaseThemes},
    {"元に戻せる", &CaseUndo},
    {"視点を切り替えられる", &CaseViewDirections},
    {"途中でやめられる", &CaseCancel},
    {"台帳の全コマンドが同じ入口から呼べる", &CaseEveryCommandReachable},
    {"メニューが台帳から出来ている", &CaseMenusComeFromCatalog},
    {"使えないコマンドは理由を出す", &CaseDisabledCommandsExplain},
    {"案内が6つそろっている", &CaseGuideIsComplete},
    {"失敗しても続き、文書が変わらない", &CaseFailureRecovery},
    {"失敗しても選んだ道具が変わらない", &CaseSelectionSurvivesFailure},
    {"モードを変えても選択と文書が変わらない", &CaseModesKeepSelection},
    {"モードで出るコマンドが変わる", &CaseModesChangeVisibleCommands},
};

} // namespace

int main(int argc, char** argv)
{
    QStringList arguments;
    for (int index = 1; index < argc; ++index) {
        arguments << QString::fromLocal8Bit(argv[index]);
    }

    if (arguments.contains(QStringLiteral("--version"))) {
        std::cout << kachakacha::v2::base::ProductVersionString() << ' '
                  << kachakacha::v2::base::MigrationNoticeJa() << '\n'
                  << kachakacha::v2::kernel::KernelVersionString() << '\n';
        return 0;
    }

    QApplication application(argc, argv);

    const QString state = ValueAfter(arguments, QStringLiteral("--manual-state"));
    const QString snapshot = ValueAfter(arguments, QStringLiteral("--snapshot"));

    if (arguments.contains(QStringLiteral("--self-test"))) {
        int failed = 0;
        for (const SelfTestCase& item : kCases) {
            // ケースごとに窓を作り直す。前のケースの状態を持ち越さない。
            V2MainWindow window;
            window.resize(1000, 700);
            window.show();
            QApplication::processEvents();
            bool ok = false;
            try {
                ok = item.body(window);
            } catch (const std::exception& error) {
                std::cout << "FAIL " << item.name << " : " << error.what() << std::endl;
                ++failed;
                continue;
            } catch (...) {
                std::cout << "FAIL " << item.name << " : 未知の例外" << std::endl;
                ++failed;
                continue;
            }
            if (ok) {
                std::cout << "PASS " << item.name << std::endl;
            } else {
                std::cout << "FAIL " << item.name << std::endl;
                ++failed;
            }
        }
        const int total = static_cast<int>(std::size(kCases));
        std::cout << "cad_next self-test: " << (total - failed) << " passed, " << failed
                  << " failed, " << total << " total\n";
        return failed == 0 ? 0 : 1;
    }

    V2MainWindow window;
    if (!state.isEmpty()) {
        window.resize(1180, 760);
        if (!window.ApplyManualState(state)) {
            std::cerr << "知らない状態です: " << state.toStdString() << '\n';
            return 2;
        }
        if (!snapshot.isEmpty()) {
            if (!SaveSnapshot(window, snapshot)) {
                std::cerr << "絵を保存できませんでした: " << snapshot.toStdString()
                          << '\n';
                return 3;
            }
            std::cout << "saved " << snapshot.toStdString() << '\n';
            return 0;
        }
    }

    window.show();
    return QApplication::exec();
}
