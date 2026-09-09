//! V2アプリの入口(WP-08)。
//!
//! 画面を出さずに確かめられる道を必ず残す。
//!   --version       : 版だけ出す
//!   --self-test     : 画面を出さずに一通り触って、結果を出す
//!   --manual-state <名前> --snapshot <png> : その状態の絵を保存する
//!   --size 1366x768 : 窓の大きさを決める(AT-UIX-010 の画面サイズ比べ)
//!   --open <kcd2>   : 文書を開く。--snapshot と組めば絵だけ撮って終わる
//!
//! V1 は画面を出さないと何も確かめられなかったので、
//! 直したかどうかを人が目で見るしかなかった。

#include "V2MainWindow.h"
#include "V2SelfTest.h"

#include "kachakacha/base/Version.h"
#include "kachakacha/kernel/KernelInfo.h"

#include <QApplication>
#include <QImage>
#include <QStringList>

#include <iostream>

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

//! 文書を開く。絵も頼まれていれば撮って終わる。
//! 終わってよいときは終了コードを返し、続けてよいときは -1 を返す。
[[nodiscard]] int OpenAndMaybeShoot(V2MainWindow& window, const QString& path,
    const QString& snapshot)
{
    if (!window.OpenDocumentFile(path)) {
        std::cerr << "開けませんでした: " << path.toStdString() << '\n';
        return 5;
    }
    std::cout << "opened " << path.toStdString() << " entities="
              << window.Session().GetDocument().Snapshot().entities.size() << '\n';
    window.Viewport().FitToDocument();
    if (snapshot.isEmpty()) {
        return -1;
    }
    if (!SaveSnapshot(window, snapshot)) {
        std::cerr << "絵を保存できませんでした: " << snapshot.toStdString() << '\n';
        return 3;
    }
    std::cout << "saved " << snapshot.toStdString() << '\n';
    return 0;
}

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
        return kachakacha::v2::selftest::RunSelfTest();
    }

    // AT-UIX-010。画面の大きさを外から決められるようにする。
    // 1366x768 と 1920x1080 の両方で、同じ状態の絵を撮って見比べる。
    const QString sizeText = ValueAfter(arguments, QStringLiteral("--size"));
    int windowWidth = 1180;
    int windowHeight = 760;
    if (!sizeText.isEmpty()) {
        const QStringList parts = sizeText.split(QStringLiteral("x"));
        if (parts.size() != 2) {
            std::cerr << "--size は 1366x768 の形で渡してください\n";
            return 4;
        }
        windowWidth = parts.at(0).toInt();
        windowHeight = parts.at(1).toInt();
        if (windowWidth < 640 || windowHeight < 400) {
            std::cerr << "--size が小さすぎます\n";
            return 4;
        }
    }

    V2MainWindow window;
    const QString openPath = ValueAfter(arguments, QStringLiteral("--open"));
    if (!openPath.isEmpty()) {
        const int code = OpenAndMaybeShoot(window, openPath, snapshot);
        if (code >= 0) {
            return code;
        }
    }
    if (!state.isEmpty()) {
        window.resize(windowWidth, windowHeight);
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
