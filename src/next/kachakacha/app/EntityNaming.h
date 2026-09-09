#pragma once

//! 一覧で名前を変える(v1-input-parity.md §2-5 の F2)。
//!
//! 名前は幾何ではないので、間違えても形は壊れない。
//! だからこそ、黙って捨てたり黙って直したりしない。
//! 空にした、前後に空白だけ足した、というのはどれも「変えていない」と同じなので、
//! 変えていないことを伝えて、文書へは入れない。履歴が意味なく伸びる。

#include "kachakacha/base/Diagnostic.h"

#include <string>

namespace kachakacha::v2::app {

//! 入れた名前を、文書へ入れられる形にそろえる。
//!
//! 前後の空白は落とす。落とさないと「線」と「線 」が別の名前に見える。
//! 中身が無くなったら断る。名前の無いものを一覧で見分けられなくなる。
[[nodiscard]] base::Result<std::string> NormalizeEntityName(const std::string& input);

//! その付け替えが、実際に何かを変えるか。変えないなら文書へ入れない。
[[nodiscard]] bool NameActuallyChanges(const std::string& current,
    const std::string& normalized) noexcept;

} // namespace kachakacha::v2::app
