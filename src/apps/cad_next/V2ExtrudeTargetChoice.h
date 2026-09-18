#pragma once

//! 押し出しの「選んだ面まで」の相手に選べる作業平面。名前と id の組。
//! 棚(V2ExtrudeDock)と詳細の窓(V2ExtrudeDialog)が同じ並びを出す。

#include "kachakacha/base/Ids.h"

#include <QString>

struct ExtrudeTargetChoice {
    kachakacha::v2::base::EntityId entityId;
    QString labelJa;
};
