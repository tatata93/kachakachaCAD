#include "kachakacha/base/Uuid.h"

namespace kachakacha::v2::base {

namespace {

[[nodiscard]] std::optional<std::uint8_t> HexValue(char character) noexcept
{
    if (character >= '0' && character <= '9') {
        return static_cast<std::uint8_t>(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
        return static_cast<std::uint8_t>(character - 'a' + 10);
    }
    // 大文字は受け付けない。保存表現を小文字1つに固定するため。
    return std::nullopt;
}

constexpr std::array<std::size_t, 4> kHyphenPositions{8, 13, 18, 23};

} // namespace

std::optional<Uuid> Uuid::Parse(std::string_view text)
{
    if (text.size() != 36) {
        return std::nullopt;
    }
    for (const std::size_t position : kHyphenPositions) {
        if (text[position] != '-') {
            return std::nullopt;
        }
    }
    std::array<std::uint8_t, 16> bytes{};
    std::size_t byteIndex = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            continue;
        }
        const std::optional<std::uint8_t> high = HexValue(text[index]);
        if (!high.has_value()) {
            return std::nullopt;
        }
        ++index;
        if (index >= text.size()) {
            return std::nullopt;
        }
        const std::optional<std::uint8_t> low = HexValue(text[index]);
        if (!low.has_value()) {
            return std::nullopt;
        }
        bytes[byteIndex] = static_cast<std::uint8_t>((*high << 4) | *low);
        ++byteIndex;
    }
    if (byteIndex != 16) {
        return std::nullopt;
    }
    return Uuid(bytes);
}

std::string Uuid::ToString() const
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    text.reserve(36);
    for (std::size_t index = 0; index < bytes_.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            text.push_back('-');
        }
        text.push_back(kDigits[bytes_[index] >> 4]);
        text.push_back(kDigits[bytes_[index] & 0x0F]);
    }
    return text;
}

} // namespace kachakacha::v2::base
