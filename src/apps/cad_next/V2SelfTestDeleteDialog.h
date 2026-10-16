#pragma once
#include "V2MainWindow.h"
#include <QApplication>
#include <QDialog>
#include <QTimer>
#include <functional>
namespace kachakacha::v2::selftest {
inline bool DeleteWithConfirmation(V2MainWindow& window,bool accept,
    std::function<void(QDialog*)> inspect={}) {
    bool seen=false;QTimer timer;timer.setInterval(5);
    QObject::connect(&timer,&QTimer::timeout,&window,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog||dialog->objectName()!=QStringLiteral("confirmDependentDelete"))return;
        seen=true;if(inspect)inspect(dialog);if(accept)dialog->accept();else dialog->reject();
    });timer.start();window.RunCommand("edit.delete");timer.stop();return seen;
}
}
