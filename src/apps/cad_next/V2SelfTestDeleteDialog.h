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
        // Preview changes can deliver nested GUI events. Inspect this modal
        // once, before a second timeout can alter its chosen checkbox state.
        timer.stop();seen=true;if(inspect)inspect(dialog);if(accept)dialog->accept();else dialog->reject();
    });timer.start();window.RunCommand("edit.delete");timer.stop();return seen;
}
}
