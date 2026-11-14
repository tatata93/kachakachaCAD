#pragma once

#include <functional>

class QString;
class QWidget;
class V2Ribbon;

namespace kachakacha::v2::selftest {

// Click the visible control or open its overflow menu and click the same item.
bool ClickRibbonItem(V2Ribbon& ribbon, const char* objectName, const QString& label,
    const std::function<bool(QWidget*)>& press);

}
