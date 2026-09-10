#pragma once

//! コマンドが押せるかどうかの判断(command-catalog.md §1)。
//!
//! 「押せるのに何も起きない」「押せないが理由が分からない」の両方を避ける。
//! 使えないコマンドは **隠さず、押せない形で見せ、理由を出す。**
//! 隠すと、そもそも在ることに気づけない。
//!
//! 判断をここへ置いたのは、画面を出さずに確かめるためである。
//! 画面側に書くと、Qt を組み立てられる機械でしか確かめられなくなり、
//! 実際にそれで8件の取りこぼしが PC のビルドまで気づかれなかった。

#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/app/Selection.h"

#include <string_view>

namespace kachakacha::v2::app {

//! いま何がどれだけ選ばれているか。数え方は画面ではなく core が決める。
struct SelectionFacts {
    bool hasDocument = true;
    bool canUndo = false;
    bool canRedo = false;
    bool hasVisibleGeometry = false;

    int workPlanes = 0;
    //! 平らな面。面そのものはまだ選べないので、作業平面だけが数に入る。
    int planarFaces = 0;
    int groups = 0;
    int wires = 0;
    //! 選んだ線が、いくつの鎖になっているか。1本ずつ離れていれば本数と同じ。
    int wireChains = 0;
    //! 閉じた輪郭になっているワイヤーの数。
    int closedProfiles = 0;
    int parts = 0;
    //! 選んだ形状ガイド(曲がった面)の数。
    int guideSurfaces = 0;
    int derivedEntities = 0;
    int fabricationModels = 0;
    int fabricationPanels = 0;
    int patterns = 0;
    //! 選んだ曲線の本数(鎖にまとめる前)。
    int curves = 0;
    //! 形状ガイドの役割表で選んでいる行の数(0か1)と、表にある行の数。
    int selectedGuideRows = 0;
    int guideRows = 0;
};

//! その条件を、いまの選択が満たしているか。
[[nodiscard]] bool SelectionSatisfies(SelectionPredicate predicate,
    const SelectionFacts& facts) noexcept;

//! 満たしていないときに出す一言。台帳の文言をそのまま使う。
[[nodiscard]] std::string_view SelectionBlockReasonJa(SelectionPredicate predicate) noexcept;

//! 画面が持っている数。文書からも場面からも分からないものだけを渡す。
struct ExternalCounts {
    int fabricationModels = 0;
    int fabricationPanels = 0;
    int patterns = 0;
    //! 形状ガイドの役割表。表は画面が持つので、行の数と選んでいる行の数をここで渡す。
    int selectedGuideRows = 0;
    int guideRows = 0;
};

//! 選択と文書と場面から、押せるかどうかの材料を作る。
//!
//! 鎖の数と閉じた輪郭の数は、ここで幾何に問い合わせて数える。
//! 画面側で数えると、画面を出さないと確かめられなくなる。
[[nodiscard]] SelectionFacts BuildSelectionFacts(const SelectionSet& selection,
    const document::DocumentSnapshot& snapshot, const modeling::SnapScene& scene,
    const geometry::GeometryTolerance& tolerance, const ExternalCounts& external,
    bool canUndo, bool canRedo);

} // namespace kachakacha::v2::app
