#pragma once

#include "kachakacha/base/Ids.h"

class V2MainWindow;

namespace kachakacha::v2::selftest {
// Draw, pick and extrude through the normal UI; no native shape injection.
[[nodiscard]] base::EntityId MakeBoxByHand(V2MainWindow&, double x0, double y0,
    double x1, double y1);
[[nodiscard]] bool PressPart(V2MainWindow&, const base::EntityId&);
}
