#include "V2DesktopStartup.h"
#include <QApplication>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QPixmap>
#include <QScreen>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWindow>
#ifdef Q_OS_WIN
#include <windows.h>
#include <cstdio>
#include <io.h>
#include <fcntl.h>
#include <cstdint>
#endif

#ifdef Q_OS_WIN
namespace {
void RestoreDiagnosticStream(DWORD kind, FILE* output)
{
    const HANDLE handle = GetStdHandle(kind);
    if (!handle || handle == INVALID_HANDLE_VALUE) return;
    HANDLE copy = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &copy,
            0, FALSE, DUPLICATE_SAME_ACCESS)) return;
    FILE* stream = nullptr;
    if (_fileno(output) < 0) freopen_s(&stream, "NUL", "w", output);
    const int fd = _open_osfhandle(reinterpret_cast<std::intptr_t>(copy), _O_WRONLY | _O_TEXT);
    if (fd < 0) { CloseHandle(copy); return; }
    _dup2(fd, _fileno(output));
    _close(fd);
}
}
#endif

void PrepareDesktopLaunch(const QStringList& arguments)
{
    const bool diagnostic = arguments.contains("--version") || arguments.contains("--self-test")
        || arguments.contains("--self-test-list") || arguments.contains("--geometry-job")
        || arguments.contains("--document-job") || arguments.contains("--snapshot");
#ifdef Q_OS_WIN
    // Headless test settings must not make an ordinary desktop launch invisible.
    if (!diagnostic && (qgetenv("QT_QPA_PLATFORM") == "offscreen" || qgetenv("QT_QPA_PLATFORM") == "minimal"))
        qunsetenv("QT_QPA_PLATFORM");
    // GUI subsystem: never create a console for ordinary users. CLI tools can
    // reuse a parent's console, while redirected self-test logs keep their pipes.
    if (diagnostic) {
        AttachConsole(ATTACH_PARENT_PROCESS);
        RestoreDiagnosticStream(STD_OUTPUT_HANDLE, stdout);
        RestoreDiagnosticStream(STD_ERROR_HANDLE, stderr);
    }
#else
    (void)diagnostic;
#endif
}

void ObserveDesktopStartup(QMainWindow& window)
{
    const QString path = qEnvironmentVariable("KACHACAD_STARTUP_REPORT");
    if (path.isEmpty()) return;
    // Runs from the ordinary QApplication::exec(), after a native window has
    // been shown. Self-test process survival is not a desktop startup proof.
    QTimer::singleShot(750, &window, [&window, path] {
        bool onScreen = false;
        for (const auto* screen : QApplication::screens())
            onScreen = onScreen || window.frameGeometry().intersects(screen->availableGeometry());
        const bool exposed = window.windowHandle() && window.windowHandle()->isExposed();
        const QPixmap frame = window.grab();
        const bool image = !frame.isNull() && frame.save(path + ".png");
        const bool ready = window.isVisible() && !window.isMinimized() && exposed
            && onScreen && !window.windowTitle().isEmpty() && image;
        const QJsonObject report{{"schema", 1}, {"ready", ready}, {"event_loop", true},
            {"platform", QApplication::platformName()}, {"title", window.windowTitle()},
            {"visible", window.isVisible()}, {"exposed", exposed}, {"on_screen", onScreen},
            {"rendered", image}, {"width", window.width()}, {"height", window.height()}};
        QFile output(path);
        const bool written = output.open(QIODevice::WriteOnly)
            && output.write(QJsonDocument(report).toJson()) > 0;
        if (qEnvironmentVariableIsSet("KACHACAD_STARTUP_EXIT"))
            QApplication::exit(ready && written ? 0 : 6);
    });
}
