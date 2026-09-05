#pragma once

#include "kachakacha/model/Surface.h"

#include <string>
#include <vector>

namespace kachakacha::model {

//! おまかせ面(オーナー指示): 選んだ線分群から前提なしに「いい感じ」の面を組み立てる。
//! 選択順・線の向き・端点の完全一致は要求しない。
//! - 線分は端点の近さで自動連結・自動反転する(小さな隙間は直線で自動的に閉じる)。
//! - 全体が1本の閉ループ → 平面なら平面、そうでなければパッチ面(Coons穴埋め)。
//! - 連結後に2本 → ルールド面。3本以上 → 中心位置で自動整列してロフト。
//! - どうしても作れないときは、何が足りないかを日本語の std::invalid_argument で返す。
struct AutoSurfaceResult {
    Surface surface;
    std::string description; //!< 何をどう組み立てたかの日本語説明(ステータス表示用)
};

[[nodiscard]] AutoSurfaceResult BuildAutoSurface(const std::vector<Wire>& wires);

} // namespace kachakacha::model
