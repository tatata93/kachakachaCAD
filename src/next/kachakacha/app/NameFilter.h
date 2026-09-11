#pragma once

//! 一覧の絞り込み(V1 の `MainWindow::ApplyModelTreeFilter`、走査 §2-7)。
//!
//! V1 の左のツリーには「名前・種類で絞り込み」の欄があった。
//! 物が増えると、一覧は数十行になる。目で探すのは、すぐに無理になる。
//!
//! ここが持つのは **1行が残るかどうか** の判断だけである。
//! 木をたどる仕事は画面側にある(木は Qt の持ち物なので)。
//! 判断をここへ置くのは、画面を出さずに確かめられるようにするためである。
//!
//! 合わせ方は V1 と同じ:
//!   - 絞り込みの語が空なら、全部残る
//!   - 名前か種類のどちらかに、語がそのまま入っていれば残る
//!   - 大文字小文字は区別しない(ASCII のみ。日本語には大小が無い)
//!   - 前後の空白は落とす。空白だけの語は「空」と同じ

#include <string>
#include <string_view>

namespace kachakacha::v2::app {

//! 前後の空白を落とす。落とした結果が空なら空を返す。
[[nodiscard]] std::string_view TrimmedFilterTerm(std::string_view term) noexcept;

//! その行が絞り込みに残るか。name は表示名、kind は種類の日本語。
[[nodiscard]] bool NameMatchesFilter(std::string_view name, std::string_view kind,
    std::string_view term);

} // namespace kachakacha::v2::app
