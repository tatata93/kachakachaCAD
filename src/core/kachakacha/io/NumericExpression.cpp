#include "kachakacha/io/NumericExpression.h"

#include <charconv>
#include <cmath>
#include <numbers>

namespace kachakacha::io {
namespace {

class Parser {
public:
    explicit Parser(std::string_view text)
        : text_(text)
    {
    }

    std::optional<double> Parse()
    {
        if (text_.empty() || text_.size() > 256) {
            return std::nullopt;
        }
        const std::optional<double> value = ParseExpression();
        SkipWhitespace();
        if (!value.has_value() || position_ != text_.size() || !std::isfinite(*value)) {
            return std::nullopt;
        }
        return value;
    }

private:
    std::optional<double> ParseExpression()
    {
        std::optional<double> value = ParseTerm();
        while (value.has_value()) {
            SkipWhitespace();
            if (!Consume('+') && !Consume('-')) {
                return value;
            }
            const char operation = text_[position_ - 1];
            const std::optional<double> right = ParseTerm();
            if (!right.has_value()) {
                return std::nullopt;
            }
            *value = operation == '+' ? *value + *right : *value - *right;
            if (!std::isfinite(*value)) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<double> ParseTerm()
    {
        std::optional<double> value = ParseUnary();
        while (value.has_value()) {
            SkipWhitespace();
            if (!Consume('*') && !Consume('/')) {
                return value;
            }
            const char operation = text_[position_ - 1];
            const std::optional<double> right = ParseUnary();
            if (!right.has_value() || (operation == '/' && *right == 0.0)) {
                return std::nullopt;
            }
            *value = operation == '*' ? *value * *right : *value / *right;
            if (!std::isfinite(*value)) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<double> ParseUnary()
    {
        SkipWhitespace();
        if (Consume('+')) {
            return ParseUnary();
        }
        if (Consume('-')) {
            const std::optional<double> value = ParseUnary();
            return value.has_value() ? std::optional<double>(-*value) : std::nullopt;
        }
        return ParsePrimary();
    }

    std::optional<double> ParsePrimary()
    {
        SkipWhitespace();
        if (Consume('(')) {
            if (++parenthesisDepth_ > 32) {
                return std::nullopt;
            }
            const std::optional<double> value = ParseExpression();
            SkipWhitespace();
            --parenthesisDepth_;
            return value.has_value() && Consume(')') ? value : std::nullopt;
        }

        if (position_ + 2 <= text_.size()
            && (text_[position_] == 'p' || text_[position_] == 'P')
            && (text_[position_ + 1] == 'i' || text_[position_ + 1] == 'I')) {
            position_ += 2;
            return std::numbers::pi;
        }

        const char* first = text_.data() + position_;
        const char* last = text_.data() + text_.size();
        double value = 0.0;
        const auto result = std::from_chars(first, last, value, std::chars_format::general);
        if (result.ec != std::errc{} || result.ptr == first || !std::isfinite(value)) {
            return std::nullopt;
        }
        position_ = static_cast<std::size_t>(result.ptr - text_.data());
        return value;
    }

    void SkipWhitespace()
    {
        while (position_ < text_.size()) {
            const char character = text_[position_];
            if (character != ' ' && character != '\t' && character != '\r' && character != '\n') {
                break;
            }
            ++position_;
        }
    }

    bool Consume(char expected)
    {
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    std::string_view text_;
    std::size_t position_ = 0;
    int parenthesisDepth_ = 0;
};

} // namespace

std::optional<double> EvaluateNumericExpression(std::string_view expression)
{
    return Parser(expression).Parse();
}

} // namespace kachakacha::io
