#include "kachakacha/app/NameFilter.h"

#include <algorithm>
#include <cctype>

namespace kachakacha::v2::app {

namespace {

[[nodiscard]] bool IsSpace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

[[nodiscard]] char Lowered(char value) noexcept
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

//! 大文字小文字を無視して、haystack の中に needle があるか。
//! 日本語は1文字が複数バイトだが、バイト列としてそのまま比べてよい。
//! UTF-8 はある文字の並びが別の文字の途中と一致しないので、誤って当たらない。
[[nodiscard]] bool ContainsFolded(std::string_view haystack, std::string_view needle)
{
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > haystack.size()) {
        return false;
    }
    const auto found = std::search(haystack.begin(), haystack.end(), needle.begin(),
        needle.end(),
        [](char left, char right) { return Lowered(left) == Lowered(right); });
    return found != haystack.end();
}

} // namespace

std::string_view TrimmedFilterTerm(std::string_view term) noexcept
{
    while (!term.empty() && IsSpace(term.front())) {
        term.remove_prefix(1);
    }
    while (!term.empty() && IsSpace(term.back())) {
        term.remove_suffix(1);
    }
    return term;
}

bool NameMatchesFilter(std::string_view name, std::string_view kind, std::string_view term)
{
    const std::string_view trimmed = TrimmedFilterTerm(term);
    if (trimmed.empty()) {
        return true;
    }
    return ContainsFolded(name, trimmed) || ContainsFolded(kind, trimmed);
}

} // namespace kachakacha::v2::app
