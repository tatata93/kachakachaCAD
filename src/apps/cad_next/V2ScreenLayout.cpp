#include "V2ScreenLayout.h"
#include "V2Ribbon.h"

#include <QEvent>
#include <QGuiApplication>
#include <QLayout>
#include <QMainWindow>
#include <QMetaObject>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QScreen>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QWindow>

#include <algorithm>

namespace {

QScreen* ScreenForFrame(const QMainWindow& window)
{
    auto* screen = window.screen();
    const auto frame = window.frameGeometry();
    qsizetype bestArea = 0;
    for (auto* candidate : QGuiApplication::screens()) {
        const auto overlap = frame.intersected(candidate->geometry());
        const auto area = qsizetype(overlap.width()) * overlap.height();
        if (area > bestArea) {
            bestArea = area;
            screen = candidate;
        }
    }
    return screen;
}

class ScreenLayout final : public QObject {
public:
    ScreenLayout(QMainWindow& window, V2Ribbon& ribbon)
        : QObject(&ribbon), window_(window), ribbon_(ribbon)
    {
        refresh_.setSingleShot(true);
        fitDelay_.setSingleShot(true);
        fitDelay_.setInterval(120);
        connect(&refresh_, &QTimer::timeout, this, [this] { UpdatePresentation(); });
        connect(&fitDelay_, &QTimer::timeout, this, [this] { FitCurrentScreen(); });
        window_.installEventFilter(this);
    }

private:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::Show:
        case QEvent::WinIdChange:
        case QEvent::WindowStateChange:
            ScheduleFit();
            break;
        case QEvent::Move:
            if (fitDelay_.isActive() || (hasDesktop_ && screen_ && screen_ != ScreenForFrame(window_)))
                ScheduleFit();
            break;
        case QEvent::Resize:
            refresh_.start();
            break;
        default:
            break;
        }
        return QObject::eventFilter(object, event);
    }

    void ScheduleFit()
    {
        refresh_.start();
        if (hasDesktop_) fitDelay_.start();
    }

    void TrackScreen()
    {
        auto* handle = window_.windowHandle();
        if (handle_ != handle) {
            disconnect(screenChange_);
            handle_ = handle;
            if (handle) {
                screenChange_ = connect(handle, &QWindow::screenChanged, this, [this] { ScheduleFit(); });
            }
        }
        auto* screen = hasDesktop_ ? ScreenForFrame(window_) : window_.screen();
        if (screen_ == screen) return;
        disconnect(workAreaChange_);
        disconnect(dpiChange_);
        screen_ = screen;
        if (screen) {
            workAreaChange_ = connect(screen, &QScreen::availableGeometryChanged, this, [this] { ScheduleFit(); });
            dpiChange_ = connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this] { ScheduleFit(); });
        }
    }

    void Fit(const QRect& workArea)
    {
        if (workArea.contains(window_.frameGeometry())
            || window_.isMaximized() || window_.isMinimized() || window_.isFullScreen()) return;
        const auto frameSize = window_.frameGeometry().size() - window_.size();
        const auto size = window_.size().boundedTo(workArea.size() - frameSize)
            .expandedTo(window_.minimumSizeHint());
        if (window_.size() != size) window_.resize(size);

        const auto frame = window_.frameGeometry();
        const int x = std::clamp(frame.x(), workArea.x(), std::max(workArea.x(), workArea.right() - frame.width() + 1));
        const int y = std::clamp(frame.y(), workArea.y(), std::max(workArea.y(), workArea.bottom() - frame.height() + 1));
        if (frame.topLeft() != QPoint(x, y)) handle_->setFramePosition(QPoint(x, y));
    }

    void UpdatePresentation()
    {
        TrackScreen();
        const auto workArea = screen_ ? screen_->availableGeometry() : QRect();
        const QSize space = workArea.isValid() && fitDelay_.isActive()
            ? window_.size().boundedTo(workArea.size()) : window_.size();
        ribbon_.SetCompact(space.width() < 1100 || space.height() < 710);
    }

    void FitCurrentScreen()
    {
        TrackScreen();
        if (!handle_ || !screen_ || !window_.isVisible()) return;
        const auto workArea = screen_->availableGeometry();
        if (!workArea.isValid()) return;
        window_.layout()->activate();
        Fit(workArea);
        UpdatePresentation();
    }

    QMainWindow& window_;
    V2Ribbon& ribbon_;
    QPointer<QWindow> handle_;
    QPointer<QScreen> screen_;
    QMetaObject::Connection screenChange_;
    QMetaObject::Connection workAreaChange_;
    QMetaObject::Connection dpiChange_;
    QTimer refresh_;
    QTimer fitDelay_;
    // The headless Qt backends have no desktop work area.
    const bool hasDesktop_ = QGuiApplication::platformName() != QStringLiteral("offscreen")
        && QGuiApplication::platformName() != QStringLiteral("minimal");
};

} // namespace

void InstallScreenLayout(QMainWindow& window, V2Ribbon& ribbon)
{
    new ScreenLayout(window, ribbon);
}
