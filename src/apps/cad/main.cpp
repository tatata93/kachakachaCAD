#include "MainWindow.h"

#include <QApplication>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QImage>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName("kachakachaCAD");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setOrganizationName("kachakachaCAD");
    QApplication::setStyle("Fusion");
    const int japaneseFontId = QFontDatabase::addApplicationFont("C:/Windows/Fonts/YuGothM.ttc");
    const QStringList japaneseFamilies = QFontDatabase::applicationFontFamilies(japaneseFontId);
    QApplication::setFont(QFont(japaneseFamilies.isEmpty() ? QStringLiteral("Meiryo UI") : japaneseFamilies.front(), 9));

    QString projectPath;
    QString snapshotPath;
    QString exportStlPath;
    QString exportStepPath;
    QString manualState;
    bool selfTest = false;
    const QStringList arguments = application.arguments();
    for (int index = 1; index < arguments.size(); ++index) {
        if (arguments[index] == "--project" && index + 1 < arguments.size()) {
            projectPath = arguments[++index];
        } else if (arguments[index] == "--snapshot" && index + 1 < arguments.size()) {
            snapshotPath = arguments[++index];
        } else if (arguments[index] == "--export-first-body-stl" && index + 1 < arguments.size()) {
            exportStlPath = arguments[++index];
        } else if (arguments[index] == "--export-first-body-step" && index + 1 < arguments.size()) {
            exportStepPath = arguments[++index];
        } else if (arguments[index] == "--self-test") {
            selfTest = true;
        } else if (arguments[index] == "--manual-state" && index + 1 < arguments.size()) {
            manualState = arguments[++index];
        }
    }

    MainWindow window;
    if (!projectPath.isEmpty() && !window.LoadProjectFile(projectPath)) {
        return 2;
    }
    if (manualState == QStringLiteral("split")) {
        window.resize(1380, 1080);
    }
    window.show();
    application.processEvents();
    if (!manualState.isEmpty() && !window.PrepareManualScreenshot(manualState)) {
        return 6;
    }
    application.processEvents();
    if (selfTest && !window.RunCreationSelfTest()) {
        return 4;
    }
    const bool exportRequested = !exportStlPath.isEmpty() || !exportStepPath.isEmpty();
    if (exportRequested
        && !window.ExportFirstBodyForAutomation(exportStlPath, exportStepPath)) {
        return 5;
    }

    if (!snapshotPath.isEmpty() || selfTest || exportRequested) {
        QTimer::singleShot(250, &application, [&] {
            if (!snapshotPath.isEmpty() && !window.grab().save(snapshotPath)) {
                application.exit(3);
                return;
            }
            application.exit(0);
        });
    }

    return application.exec();
}
