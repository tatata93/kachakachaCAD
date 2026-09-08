#pragma once

//! 文書から画面の場面を作る(WP-11/WP-12)。
//!
//! ファイルを読んだあと、線を画面へ出すには、文書の Feature から
//! 曲線を取り出して場面へ並べ直さなければならない。
//! その並べ直しをここに置く。画面に置くと、ファイルを開かないと試験できない。
//!
//! ここでやるのは並べ直しだけである。評価はしない。
//! CreateWire は定義がそのまま形なので、評価なしで出せる。
//! それ以外の Feature(押し出し・製作)は立体なので、ここでは出さない。

#include "kachakacha/document/Document.h"
#include "kachakacha/modeling/SnapEngine.h"

namespace kachakacha::v2::app {

//! 文書の線を場面へ並べる。並びは文書の Feature の並びと同じ。
//! 消えている(非表示の)ものは出さない。出すと、消したつもりのものが吸着の相手になる。
[[nodiscard]] modeling::SnapScene BuildSceneFromDocument(
    const document::DocumentSnapshot& snapshot, base::IdGenerator& ids);

//! 文書の線を、いまの場面の作業平面とグリッドを保ったまま並べ直す。
[[nodiscard]] modeling::SnapScene RebuildSceneKeepingView(
    const modeling::SnapScene& current, const document::DocumentSnapshot& snapshot,
    base::IdGenerator& ids);

} // namespace kachakacha::v2::app
