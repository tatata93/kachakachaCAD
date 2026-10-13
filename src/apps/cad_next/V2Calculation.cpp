#include "V2Calculation.h"
#include "V2GeometryJob.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QLabel>
#include <QObject>
#include <QProgressBar>
#include <QPushButton>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <atomic>
#include <exception>
#include <stdexcept>
#include <thread>

namespace V2Calculation {
namespace {
class CalculationDialog final : public QDialog {
public:
    using QDialog::QDialog;
    std::function<void()> cancel;
    void reject() override { if (cancel) cancel(); }
};
class InputGate final : public QObject {
public:
    explicit InputGate(QWidget& dialog) : dialog_(dialog) { qApp->installEventFilter(this); }
    ~InputGate() override { qApp->removeEventFilter(this); }
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease
            && event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease) return false;
        const auto* widget = qobject_cast<QWidget*>(object);
        return widget && widget->window() != &dialog_;
    }
private:
    QWidget& dialog_;
};
}
bool BackgroundAllowed() {
    return qobject_cast<QApplication*>(QCoreApplication::instance())
        && QThread::currentThread() == QCoreApplication::instance()->thread();
}
bool Execute(const QWidget* parent, const char* title, const std::function<void()>& work) {
    if (V2GeometryJob::Active()) throw std::runtime_error("別の計算が終了してから再操作してください。");
    struct Busy {
        Busy() { qApp->setProperty("geometryJobActive", true); }
        ~Busy() { qApp->setProperty("geometryJobActive", false); }
    } busy;
    CalculationDialog dialog(parent ? const_cast<QWidget*>(parent) : QApplication::activeWindow());
    InputGate gate(dialog);
    dialog.setObjectName("calculationDialog");
    dialog.setWindowTitle(QStringLiteral("計算中"));
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setMinimumWidth(420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* phase = new QLabel(QString::fromUtf8(title), &dialog);
    phase->setWordWrap(true); layout->addWidget(phase);
    auto* progress = new QProgressBar(&dialog);
    progress->setObjectName("calculationProgress"); progress->setRange(0, 0); layout->addWidget(progress);
    auto* elapsed = new QLabel(&dialog); layout->addWidget(elapsed);
    auto* cancel = new QPushButton(QStringLiteral("計算を取り消す"), &dialog);
    cancel->setObjectName("calculationCancel"); layout->addWidget(cancel);
    bool cancelled = false;
    dialog.cancel = [&] {
        cancelled = true; cancel->setEnabled(false);
        phase->setText(QStringLiteral("取消中 — 実行中の処理が終わり次第、結果を破棄します…"));
    };
    QObject::connect(cancel, &QPushButton::clicked, &dialog, [&] { dialog.reject(); });
    std::atomic<bool> finished = false;
    std::exception_ptr exception;
    std::thread worker([&] {
        try { work(); } catch (...) { exception = std::current_exception(); }
        finished.store(true);
    });
    QElapsedTimer clock; clock.start(); QTimer timer; timer.setInterval(25);
    QEventLoop loop;
    QObject::connect(&timer, &QTimer::timeout, &dialog, [&] {
        if (finished.load()) { dialog.done(QDialog::Accepted); loop.quit(); return; }
        // Avoid flashing dialogs for an ordinary instantaneous calculation.
        if (clock.elapsed() >= 200 && !dialog.isVisible()) dialog.show();
        elapsed->setText(QStringLiteral("経過 %1 秒 — 完了するまで文書は変更しません").arg(clock.elapsed() / 1000));
    });
    timer.start();
    if (!finished.load()) loop.exec();
    // Shutdown also joins the private worker before any captured input is destroyed.
    worker.join();
    if (exception) std::rethrow_exception(exception);
    return cancelled;
}
}
