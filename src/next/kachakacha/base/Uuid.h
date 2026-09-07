#pragma once

//! UUID v4 の値型と、その文字列表現。
//! 保存表現は小文字ハイフン付き(8-4-4-4-12)。文字列と暗黙変換できない強い型として使う。

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace kachakacha::v2::base {

class Uuid {
public:
    //! 全ゼロ(=無効)を作る。
    constexpr Uuid() noexcept = default;

    explicit constexpr Uuid(std::array<std::uint8_t, 16> bytes) noexcept
        : bytes_(bytes)
    {
    }

    //! 小文字ハイフン付きの36文字だけを受け付ける。大文字・波括弧・ハイフン無しは拒否する。
    //! 受け付けない形を黙って直さない(保存表現を1つに保つため)。
    [[nodiscard]] static std::optional<Uuid> Parse(std::string_view text);

    [[nodiscard]] std::string ToString() const;

    [[nodiscard]] constexpr bool IsNil() const noexcept
    {
        for (const std::uint8_t value : bytes_) {
            if (value != 0) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr const std::array<std::uint8_t, 16>& Bytes() const noexcept
    {
        return bytes_;
    }

    friend constexpr bool operator==(const Uuid& left, const Uuid& right) noexcept
    {
        return left.bytes_ == right.bytes_;
    }
    friend constexpr bool operator!=(const Uuid& left, const Uuid& right) noexcept
    {
        return !(left == right);
    }
    //! 並び順はバイト列の辞書順。保存や表示の順序を決定的にするために使う。
    friend constexpr bool operator<(const Uuid& left, const Uuid& right) noexcept
    {
        return left.bytes_ < right.bytes_;
    }
    friend constexpr bool operator>(const Uuid& left, const Uuid& right) noexcept
    {
        return right < left;
    }
    friend constexpr bool operator<=(const Uuid& left, const Uuid& right) noexcept
    {
        return !(right < left);
    }
    friend constexpr bool operator>=(const Uuid& left, const Uuid& right) noexcept
    {
        return !(left < right);
    }

private:
    std::array<std::uint8_t, 16> bytes_{};
};

} // namespace kachakacha::v2::base

template<>
struct std::hash<kachakacha::v2::base::Uuid> {
    [[nodiscard]] std::size_t operator()(
        const kachakacha::v2::base::Uuid& value) const noexcept
    {
        // 16バイトを8バイト2つへ畳んで混ぜる。
        std::size_t high = 0;
        std::size_t low = 0;
        const auto& bytes = value.Bytes();
        for (std::size_t index = 0; index < 8; ++index) {
            high = (high << 8) | bytes[index];
            low = (low << 8) | bytes[index + 8];
        }
        return high ^ (low + 0x9e3779b97f4a7c15ULL + (high << 6) + (high >> 2));
    }
};
