#pragma once

//! 配る見本の文書(WP-12)。
//!
//! マニュアルの手順をそのままなぞれるものを1つだけ持つ。
//! 中身は「1/87 の車体の側面を、外形と6枚の窓で描いたもの」である。
//! 立体は入れない。立体を入れると、読む側に OCCT が要るためである。
//!
//! IDは決まった並びで振る。だから同じ版からは同じバイト列の `.kcd2` が出る。
//! 出ないと、見本が更新されたのか、ただ作り直しただけなのかが版管理で分からない。

#include "kachakacha/io/DocumentFile.h"

#include <string>

namespace kachakacha::v2::app {

//! 見本の文書。1/87 の側面。
[[nodiscard]] io::DocumentFile BuildSampleDocument();

//! 見本の `.kcd2` の中身。同じ版からは毎回同じバイト列。
[[nodiscard]] base::Result<std::string> BuildSampleArchive();

//! 見本の版。中身を変えたら上げる。マニュアルの図と合わせるため。
[[nodiscard]] std::string_view SampleDocumentVersion() noexcept;

} // namespace kachakacha::v2::app
