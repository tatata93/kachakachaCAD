#pragma once

//! 上位モード(ui-workflows.md §2、AT-UIX-001)。
//!
//! モードは4つだけにする。V1 の「面 / 板材」のような、
//! 内部の作りがそのまま出た区分は置かない(PRD §3)。
//!
//! 決まりが3つある(UIX-001 / 003)。
//!   1. モードを切り替えても選択は消えない。
//!   2. モードを変えただけで Feature を作ったり消したり計算し直したりしない。
//!   3. 共通操作はどのモードでも常に出す。

#include "kachakacha/app/CommandCatalog.h"

#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

enum class UiMode {
    Drawing,      //!< 作図
    Part,         //!< 部品
    Fabrication,  //!< 製作
    Output,       //!< 出力
};

[[nodiscard]] std::string_view UiModeNameJa(UiMode value) noexcept;
[[nodiscard]] std::string_view UiModeName(UiMode value) noexcept;

//! 4つのモード。並びは仕様の順。
[[nodiscard]] const std::vector<UiMode>& AllUiModes();

//! どのモードでも常に出す共通操作(ui-workflows.md §2)。
[[nodiscard]] const std::vector<std::string_view>& CommonCommandIds();

//! そのモードで出すコマンド。共通操作は含めない。
[[nodiscard]] const std::vector<std::string_view>& CommandIdsForMode(UiMode mode);

//! 上の帯(2段目)に並べる命令。モードの命令のうち **入口になるものだけ**。
//!
//! 部品モードの2段目には 19 個が横一列に並んでいた。名前だけの札が並び、
//! 何から押すのか読めない(オーナー指摘 2026-09-11「上部のuiは不親切すぎる」)。
//! 表の行を動かす・板厚を当てる、といったものは **その欄の隣** にあるべきで、
//! 右の棚へ移した。ここに残すのは「そこから始める」ものだけである。
//!
//! ここに並ぶ id は必ず CommandIdsForMode にも入っている(試験が照合する)。
[[nodiscard]] const std::vector<std::string_view>& TopBarCommandIdsForMode(UiMode mode);

//! そのコマンドが、そのモードで出るか(共通操作を含めて判断する)。
[[nodiscard]] bool CommandVisibleInMode(std::string_view commandId, UiMode mode);

} // namespace kachakacha::v2::app
