// V2アプリの最小shell。
// WP-01の時点では、ビルドが通り、起動し、版と移行中である旨を出すことだけを保証する。
// 製品機能はここには無い。空ウィンドウにしないこと(WP-01 Gate)。
#include "kachakacha/base/Version.h"
#include "kachakacha/kernel/KernelInfo.h"

#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <iostream>

namespace {

[[nodiscard]] QString BuildSummary()
{
    QStringList lines;
    lines << QStringLiteral("kachakachaCAD %1 (%2)")
                 .arg(QString::fromStdString(kachakacha::v2::base::ProductVersionString()),
                     QString::fromStdString(kachakacha::v2::base::MigrationNoticeJa()));
    lines << QStringLiteral("幾何カーネル: %1")
                 .arg(QString::fromStdString(kachakacha::v2::kernel::KernelVersionString()));
    lines << QString();
    lines << QStringLiteral("この実行ファイルはWire-first V2の作業中の姿です。");
    lines << QStringLiteral("製品機能はまだ入っていません。作図には現行版をお使いください。");
    return lines.join(QLatin1Char('\n'));
}

} // namespace

int main(int argc, char** argv)
{
    const QStringList arguments = [argc, argv] {
        QStringList list;
        for (int index = 1; index < argc; ++index) {
            list << QString::fromLocal8Bit(argv[index]);
        }
        return list;
    }();

    // 画面を開かずに版だけ確かめたい場合(CIと受入試験用)。
    if (arguments.contains(QStringLiteral("--version"))) {
        std::cout << kachakacha::v2::base::ProductVersionString() << ' '
                  << kachakacha::v2::base::MigrationNoticeJa() << '\n'
                  << kachakacha::v2::kernel::KernelVersionString() << '\n';
        return 0;
    }

    QApplication application(argc, argv);
    QMainWindow window;
    window.setWindowTitle(QStringLiteral("kachakachaCAD (V2準備中)"));

    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(24, 24, 24, 24);
    auto* label = new QLabel(BuildSummary());
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(label);
    layout->addStretch(1);
    window.setCentralWidget(central);
    window.resize(640, 320);

    if (arguments.contains(QStringLiteral("--self-test"))) {
        // offscreenで起動できることだけを確かめて終わる。
        window.show();
        QApplication::processEvents();
        const bool ok = !label->text().isEmpty() && window.isVisible();
        std::cout << (ok ? "cad_next self-test passed\n" : "cad_next self-test failed\n");
        return ok ? 0 : 1;
    }

    window.show();
    return QApplication::exec();
}
