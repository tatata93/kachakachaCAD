#pragma once
#include <QStringList>
class QWidget;
namespace v2ui {
// -1: cancel, 0: delete listed objects, 1: ungroup while keeping contents.
int ConfirmDeletion(QWidget* parent, const QStringList& names, bool hasFolders = false);
}
