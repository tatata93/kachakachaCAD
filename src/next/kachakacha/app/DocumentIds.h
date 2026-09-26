#pragma once

//! 開いた文書の ID と、これから振る ID を重ねない。
//!
//! 画面は DeterministicIdGenerator(通し番号)で ID を振る。文書を開き直すと番号が 1 から
//! 始まるので、前の回に振った ID とぶつかり、面を作る・固定するときに DOC-C003
//! 「同じ ID のオブジェクトが既にあります」で断られていた(オーナー報告 2026-09-27:
//! 面生成に失敗する)。文書を採用したら、その中のどの決定的 ID よりも先へ進める。
//!
//! 文書の全部の ID(要素・操作履歴・線の片・寸法・まとまり…)を漏れなく拾うため、
//! 構造をたどらず、書き出した JSON の文字列から ID の形を拾う。

#include "kachakacha/base/Ids.h"
#include "kachakacha/document/Document.h"

#include <cstdint>
#include <string_view>

namespace kachakacha::v2::app {

//! JSON(や任意の文字列)の中の決定的 ID の通し番号の最大。無ければ 0。
[[nodiscard]] std::uint64_t MaxDeterministicCounterIn(std::string_view text);

//! 文書の中のどの決定的 ID よりも先へ、生成器を進める。進めた先の番号を返す。
//! 決定的でない生成器(乱数)なら何もせず 0 を返す。
std::uint64_t AdvanceIdsPastDocument(base::IdGenerator& ids,
    const document::DocumentSnapshot& snapshot);

} // namespace kachakacha::v2::app
