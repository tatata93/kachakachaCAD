#pragma once

#include "kachakacha/model/Project.h"

#include <istream>
#include <ostream>
#include <string_view>

namespace kachakacha::io {

[[nodiscard]] model::Project LoadProjectScript(std::istream& input, std::string_view sourceName);
void WriteProjectScript(std::ostream& output, const model::Project& project);

} // namespace kachakacha::io
