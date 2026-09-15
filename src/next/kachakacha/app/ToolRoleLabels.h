#pragma once

//! 3D の中に出す、一時的な役割の札(オーナー指示 2026-09-15 §7、UI の正本)。
//!
//! 正本の 3D 図には、押し出しのとき `TARGET` と `PROFILE`、
//! 面を作るとき `Section 1` `Section 2` `Section 3` の札が出ている。
//!
//! これが無いと、右の棚に「断面 3本」と出ていても、
//! **画面のどの線がその3本なのかが分からない。**
//! 選び直すときに、いま何が入っているのかを見て確かめられない。
//!
//! ここは「どの番号に、どの札を出すか」だけを決める。位置も描画も画面の仕事。

#include "kachakacha/app/ExtrudeInputState.h"
#include "kachakacha/app/SurfaceInputState.h"
#include "kachakacha/base/Ids.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace kachakacha::v2::app {

//! 1枚の札。どの番号に、どの言葉を出すか。
struct ToolRoleLabel {
    base::EntityId entityId;
    std::string text;
};

//! 押し出しの札。対象は `TARGET`、輪郭は `PROFILE`(複数なら番号つき)。
[[nodiscard]] std::vector<ToolRoleLabel> ExtrudeRoleLabels(const ExtrudeInputState& state);

//! 面を作るときの札。
//!
//!   断面 … `Section 1` から順に。**画面の「3. 断面順」と同じ番号。**
//!   ガイド … 2本なら `Guide L` `Guide R`、それ以外は `Guide 1` から順に
//!   境界 … 1本なら `Boundary`、複数なら番号つき
//!
//! 曲線網のときは断面が U、ガイドが V なので、そう出す(正本の曖昧点の扱い)。
[[nodiscard]] std::vector<ToolRoleLabel> SurfaceRoleLabels(const SurfaceInputState& state);

} // namespace kachakacha::v2::app
