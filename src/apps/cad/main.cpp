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
    QApplication::setOrganizationName("kachakachaCAD");
    QApplication::setStyle("Fusion");
    const int japaneseFontId = QFontDatabase::addApplicationFont("C:/Windows/Fonts/YuGothM.ttc");
    const QStringList japaneseFamilies = QFontDatabase::applicationFontFamilies(japaneseFontId);
    QApplication::setFont(QFont(japaneseFamilies.isEmpty() ? QStringLiteral("Meiryo UI") : japaneseFamilies.front(), 9));

    QString projectPath;
    QString snapshotPath;
    bool selfTest = false;
    const QStringList arguments = application.arguments();
    for (int index = 1; index < arguments.size(); ++index) {
        if (arguments[index] == "--project" && index + 1 < arguments.size()) {
            projectPath = arguments[++index];
        } else if (arguments[index] == "--snapshot" && index + 1 < arguments.size()) {
            snapshotPath = arguments[++index];
        } else if (arguments[index] == "--self-test") {
            selfTest = true;
        }
    }

    MainWindow window;
    if (!projectPath.isEmpty() && !window.LoadProjectFile(projectPath)) {
        return 2;
    }
    window.show();
    application.processEvents();
    if (selfTest && !window.RunCreationSelfTest()) {
        return 4;
    }

    if (!snapshotPath.isEmpty() || selfTest) {
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
