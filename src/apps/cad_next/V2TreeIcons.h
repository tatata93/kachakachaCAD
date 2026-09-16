#pragma once

#include "kachakacha/domain/Entity.h"

#include <QIcon>

//! モデルツリーで、名前を読まなくても種類を見分けるための小さな記号。
[[nodiscard]] QIcon V2EntityTreeIcon(kachakacha::v2::domain::EntityKind kind);
[[nodiscard]] QIcon V2GroupTreeIcon();
[[nodiscard]] QIcon V2OriginTreeIcon();
[[nodiscard]] QIcon V2AxisTreeIcon(int axis);
