#pragma once
#include "kachakacha/base/Ids.h"
class V2MainWindow;
namespace kachakacha::v2::selftest::facefixture {
base::EntityId MakeGuide(V2MainWindow&,double height=12,bool tilted=false,bool holes=false);
void Setup(V2MainWindow&);
}
