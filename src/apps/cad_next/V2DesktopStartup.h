#pragma once
#include <QStringList>
class QMainWindow;
void PrepareDesktopLaunch(const QStringList& arguments);
void ObserveDesktopStartup(QMainWindow& window);
