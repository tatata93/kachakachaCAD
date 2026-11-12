#pragma once
#include <QStringList>
#include <functional>
class QWidget;
namespace v2ui {
// -1: cancel, 0: delete, 1: keep folder contents, 2: also delete generated boundaries.
int ConfirmDeletion(QWidget* parent, const QStringList& names, bool hasFolders = false,
    const QStringList* withBoundaries = nullptr, const std::function<void(bool)>& preview = {});
}
