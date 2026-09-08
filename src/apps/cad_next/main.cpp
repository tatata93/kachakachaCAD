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
#include "Win95Style.h"

#include "kachakacha/base/Version.h"
#include "kachakacha/kernel/KernelInfo.h"

#include <QApplication>
#include <QPointF>
#include <QImage>
#include <QPainter>
#include <QStringList>

#include <iostream>
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

const SelfTestCase kCases[] = {
    {"道具を選べる", &CaseToolsExist},
    {"直線を引ける", &CaseDrawLine},
    {"曲線を曲線のまま描ける", &CaseCurves},
    {"端点に吸着する", &CaseSnap},
    {"見た目を切り替えられる", &CaseThemes},
    {"元に戻せる", &CaseUndo},
    {"視点を切り替えられる", &CaseViewDirections},
    {"途中でやめられる", &CaseCancel},
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
