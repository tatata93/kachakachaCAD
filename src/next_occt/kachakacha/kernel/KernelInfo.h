#pragma once

#include <string>

namespace kachakacha::v2::kernel {

//! OCCTアダプタ層が実際にリンクできているかを、UIと試験から確かめるための最小API。
//! ここに幾何の本体は置かない(WP-06以降)。
[[nodiscard]] std::string KernelVersionString();

//! この層がOCCT付きでビルドされたかどうか。
[[nodiscard]] bool IsKernelAvailable();

} // namespace kachakacha::v2::kernel
