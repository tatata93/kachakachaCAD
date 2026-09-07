#pragma once

#include <string>

namespace kachakacha::v2::base {

//! V2アプリの表示用バージョン。切替までは「V2準備中」であることを必ず添える。
[[nodiscard]] std::string ProductVersionString();

//! 移行中であることの表示文。空文字を返してはならない。
[[nodiscard]] std::string MigrationNoticeJa();

} // namespace kachakacha::v2::base
