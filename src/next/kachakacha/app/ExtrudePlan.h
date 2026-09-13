#pragma once

//! 選んだものから「押し出しが何を意味するか」を決める。
//!
//! オーナー指示 2026-09-14。CAD に詳しくない人が、説明書なしで使えるようにする。
//! そのために、**利用者に役割を宣言させない** 。
//! 立体と輪郭を選んだなら、立体が加工される側で、輪郭が形を決める側である。
//! どちらを先に選んだかは関係ない。型を見れば決まる。
//!
//! 受け付ける組み合わせ(指示の A〜E)。
//!   A 輪郭だけ      → 輪郭を押し出して新しい立体を作る
//!   B 面だけ        → その面を押し出す
//!   C 立体だけ      → 加工する相手は決まった。「押し出す面か輪郭を選んでください」
//!   D 立体 + 輪郭   → 立体が相手、輪郭が形
//!   E 立体 + 面     → その面を押し引きする
//!
//! ここは判断だけを持つ。形は作らない。画面も知らない。
//! 画面へ出す言葉もここで作る ── 「CADがいまの選択をどう読んだか」は
//! 必ず日本語で見せると決めたので、その文が2か所にあると食い違う。

#include "kachakacha/base/Ids.h"
#include "kachakacha/modeling/ExtrudeInput.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kachakacha::v2::app {

//! 選んだものの内訳。文書を見て画面側が数える。
struct ExtrudeSelectionFacts {
    //! 閉じた輪郭。押し出すと立体になる。
    std::size_t closedWires = 0;
    //! 開いた輪郭。押し出しても立体にならない。
    std::size_t openWires = 0;
    //! 立体の面。押し引きの相手。
    std::size_t faces = 0;
    //! 立体(部品)。
    std::size_t solids = 0;
    //! 曲面(形状ガイド)。押し出しの相手ではない。
    std::size_t surfaces = 0;
};

//! いまの選択が、どの組み合わせに当たるか。
enum class ExtrudeInputKind {
    //! 何も選んでいない。
    Nothing,
    //! 輪郭だけ(A)。
    ProfileOnly,
    //! 面だけ(B)。
    FaceOnly,
    //! 立体だけ(C)。相手は決まったが、形がまだ決まっていない。
    SolidOnly,
    //! 立体と輪郭(D)。
    SolidAndProfile,
    //! 立体と面(E)。
    SolidAndFace,
    //! 押し出せない組み合わせ。
    Unusable,
};

//! 読み取った結果。
struct ExtrudePlan {
    ExtrudeInputKind kind = ExtrudeInputKind::Nothing;
    //! 加工される立体。無ければ空。
    base::EntityId targetSolid;
    //! 形を決めるもの(輪郭または面)。
    std::vector<base::EntityId> profiles;
    //! profiles が面かどうか。輪郭なら false。
    bool profileIsFace = false;

    //! いま選べる操作。並びはそのまま画面へ出す。
    std::vector<modeling::ExtrudeBooleanMode> operations;
    //! 最初に選ばれている操作。
    modeling::ExtrudeBooleanMode defaultOperation = modeling::ExtrudeBooleanMode::NewPart;

    //! 押し出しの下見をもう出せるか。false なら、まだ足りないものがある。
    bool readyToPreview = false;
    //! 足りないときに、**何を選べばよいか** を言う。
    //! 「不正な入力です」だけで終わらせない。
    std::string needsJa;
};

//! 選んだものから読み取る。
[[nodiscard]] ExtrudePlan PlanExtrude(const ExtrudeSelectionFacts& facts,
    const std::vector<base::EntityId>& solidIds,
    const std::vector<base::EntityId>& profileIds, bool profilesAreFaces);

//! 「CADがいまの選択をどう読んだか」の日本語。
//!
//! 名前は呼ぶ側が渡す(文書の表示名)。ここは並べ方だけを決める。
//! 空の行は出さない ── 意味のない欄を並べない、という決まりのため。
[[nodiscard]] std::string ExplainExtrudePlanJa(const ExtrudePlan& plan,
    const std::string& targetNameJa, const std::vector<std::string>& profileNamesJa,
    modeling::ExtrudeBooleanMode operation);

//! 用語。画面では内部の言葉を出さない(Wire=輪郭、Face=面、Solid=立体)。
[[nodiscard]] std::string_view ExtrudeInputKindNameJa(ExtrudeInputKind kind) noexcept;

} // namespace kachakacha::v2::app
