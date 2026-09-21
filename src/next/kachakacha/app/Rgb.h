#pragma once

//! 画面に出す色。どの色にするかは core が決め、画面は塗るだけにする
//! (面の解析の塗り分け、面を作るときの役割の色分け)。

#include <cstdint>

namespace kachakacha::v2::app {

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    [[nodiscard]] bool operator==(const Rgb& other) const noexcept
    {
        return r == other.r && g == other.g && b == other.b;
    }
};

} // namespace kachakacha::v2::app
