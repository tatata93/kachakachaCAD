#pragma once

//! 押し出しの「選んだ面まで」の相手に選べる作業平面。名前と id の組。
//! 棚(V2ExtrudeDock)と詳細の窓(V2ExtrudeDialog)が同じ並びを出す。

#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/Vector3.h"

#include <QString>
#include <string>

struct ExtrudeTargetChoice {
    kachakacha::v2::base::EntityId entityId;
    QString labelJa;
    kachakacha::v2::geometry::Vector3 direction{};
    double parameter = -1.0;
    std::string faceBrep; // snapshot only for an explicitly picked solid face
};
