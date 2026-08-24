#pragma once

#include <optional>
#include <string_view>

namespace kachakacha::io {

// Evaluates a small, deterministic arithmetic expression used by CAD dimension fields.
// Supported syntax: decimal/scientific numbers, pi, +, -, *, /, unary signs and parentheses.
[[nodiscard]] std::optional<double> EvaluateNumericExpression(std::string_view expression);

} // namespace kachakacha::io
