#pragma once

//! 原点の基準平面(V1 の一覧の「原点」ノード)。
//!
//! V1 は新しいプロジェクトに top_XY / front_XZ / side_YZ の3平面を置き、
//! 一覧の最上部に「原点」として固定表示していた。消せず、名前も変えられず、
//! グループへも移せない。V2 は既定の作業平面が文書のもの(Entity)ではなく画面の枠だけ
//! だったので、「平面から離す」「2面の中間」の相手が最初は無く、作業平面が作れないように
//! 見えた。ここで、文書に無ければ3平面を足す。
//!
//! 名前は V1 と同じにする(.kcd の読み込みで同じ名前が来る)。

#include "kachakacha/base/Ids.h"
#include "kachakacha/document/Document.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

//! 原点の平面1つぶん。並びは XY → XZ → YZ(V1 の一覧と同じ)。
struct OriginPlaneSpec {
    modeling::StandardPlaneKind kind = modeling::StandardPlaneKind::XY;
    std::string_view name;
};

[[nodiscard]] const std::array<OriginPlaneSpec, 3>& OriginPlaneSpecs();

//! 文書に無い原点平面を足す。あるものはそのまま。戻りは3つの id(XY, XZ, YZ の順)。
//! 文書を変えたら true。
bool EnsureOriginPlanes(document::Document& document, base::IdGenerator& ids,
    std::array<base::EntityId, 3>* idsOut = nullptr);

//! その Entity が原点の平面か。
[[nodiscard]] bool IsOriginPlane(const document::DocumentSnapshot& snapshot,
    const base::EntityId& id);

//! その向きの原点平面の id。無ければ空。
[[nodiscard]] std::optional<base::EntityId> OriginPlaneId(
    const document::DocumentSnapshot& snapshot, modeling::StandardPlaneKind kind);

} // namespace kachakacha::v2::app
