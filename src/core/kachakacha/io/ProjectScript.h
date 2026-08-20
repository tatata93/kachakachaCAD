#pragma once

#include "kachakacha/model/Project.h"

#include <istream>
#include <string_view>

namespace kachakacha::io {

[[nodiscard]] model::Project LoadProjectScript(std::istream& input, std::string_view sourceName);

} // namespace kachakacha::io

