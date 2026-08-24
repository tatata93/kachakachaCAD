#include "kachakacha/io/NumericExpression.h"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

bool ExpectValue(std::string_view expression, double expected)
{
    const std::optional<double> value = kachakacha::io::EvaluateNumericExpression(expression);
    if (!value.has_value() || std::abs(*value - expected) > 1.0e-10) {
        std::cerr << "expression failed: " << expression << '\n';
        return false;
    }
    return true;
}

bool ExpectInvalid(std::string_view expression)
{
    if (kachakacha::io::EvaluateNumericExpression(expression).has_value()) {
        std::cerr << "invalid expression was accepted: " << expression << '\n';
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!ExpectValue("(180/2)*3", 270.0)
        || !ExpectValue("10 + 5 * 2", 20.0)
        || !ExpectValue("-(2 + 3) / 2", -2.5)
        || !ExpectValue("1e-3 * 25", 0.025)
        || !ExpectValue("pi * 2", 2.0 * std::acos(-1.0))
        || !ExpectInvalid("")
        || !ExpectInvalid("2 *")
        || !ExpectInvalid("(1 + 2")
        || !ExpectInvalid("1 / 0")
        || !ExpectInvalid("2 apples")) {
        return 1;
    }
    return 0;
}
