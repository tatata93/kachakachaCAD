#pragma once

//! 「診断情報をコピー」(DIAGNOSTICS_FEATURE_SPEC.md)。
//!
//! 不具合を見つけた瞬間の画面の状態を、短い文にして持ち出せるようにする。
//! 貼り付ける先は人であったり、別の AI であったりする。
//!
//! **わざと別々に出す欄がある。**
//!   activeTool / rightPanelTool / cursorMode / previewOwner / snapOwner
//! これらは本来そろっているべきもので、ずれていること自体が不具合である。
//! 1つにまとめてしまうと、ずれを見つけられない。
//! (USER-UI-001「道具を替えても右の棚が前の道具のまま」がまさにこれ。)
//!
//! **出してはいけないもの** も、ここで機械的に落とす。文書の名前は
//! ファイル名だけにして、フォルダの並びは出さない。貼り付ける先が
//! 他人の目に触れる場所であることを前提にする。

#include "kachakacha/modeling/SnapEngine.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

//! 吸着の種類の名前。
//!
//! 本当は SnapEngine.h にあるべきものである。いまそこには Codex の書きかけが
//! あるので、同じファイルへ同時に入らないよう、ここへ置いている。
//! 書きかけが入ったら、あちらへ移すこと。
[[nodiscard]] std::string_view SnapKindNameJa(modeling::SnapKind kind) noexcept;

//! 診断に載せる状態。画面が詰めて渡す。ここは Qt を知らない。
struct DiagnosticSnapshot {
    //! ISO 8601。画面側が作る。
    std::string timestamp;
    std::string version;
    //! 分かれば。分からなければ空。
    std::string commit;
    std::string branch;
    //! commit が分かるときだけ意味がある。
    bool dirty = false;
    bool dirtyKnown = false;

    //! 文書の名前。**フォルダの並びは入れない。** 空なら未保存。
    std::string documentName;
    std::string activePart;
    std::string activeWorkPlane;

    //! そろっているべき5つ。ずれを見つけるために別々に持つ。
    std::string activeTool;
    std::string rightPanelMode;
    std::string rightPanelTool;
    //! カーソルの **形** の名前(十字・矢印など)。道具の名前ではないので、
    //! そろっているかの照合には使わない。読む人のために出すだけである。
    std::string cursorMode;
    //! カーソルを最後に計算し直したときの道具。
    //! ここが activeTool とずれていたら、道具を替えたのにカーソルを
    //! 作り直していない、ということである。
    std::string cursorOwner;
    std::string previewOwner;
    std::string snapOwner;

    std::string snapType;
    std::string snapTarget;

    std::size_t selectionCount = 0;
    //! 選んでいるものの名前。長すぎる一覧は切り詰める。
    std::vector<std::string> selection;
};

//! 一覧に並べる上限。超えた分は数だけ言う。
//! 全部並べると、貼り付けた先で本文が埋まる。
inline constexpr std::size_t kDiagnosticSelectionLimit = 20;

//! 道の並びを落として、ファイル名だけにする。
//! `C:\Users\誰か\Documents\a.kcd2` → `a.kcd2`。
//! 区切りは `/` と `\` の両方を見る(Windows の文書を Linux で試験するため)。
[[nodiscard]] std::string FileNameOnly(const std::string& path);

//! そろっているべき5つが本当にそろっているか。
//! ずれているなら、その組を返す。そろっていれば空。
[[nodiscard]] std::vector<std::string> DiagnosticMismatches(
    const DiagnosticSnapshot& snapshot);

//! 貼り付けられる形にする。
[[nodiscard]] std::string FormatDiagnosticReport(const DiagnosticSnapshot& snapshot);

} // namespace kachakacha::v2::app
