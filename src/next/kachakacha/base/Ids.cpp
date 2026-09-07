#include "kachakacha/base/Ids.h"

#include <chrono>
#include <random>

namespace kachakacha::v2::base {

namespace {

//! UUID v4 の版と変種のビットを立てる。
void StampVersion4(std::array<std::uint8_t, 16>& bytes) noexcept
{
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);
}

[[nodiscard]] std::uint64_t SplitMix64(std::uint64_t& state) noexcept
{
    state += 0x9e3779b97f4a7c15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

void WriteBigEndian(std::array<std::uint8_t, 16>& bytes, std::size_t offset,
    std::uint64_t value) noexcept
{
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[offset + index] =
            static_cast<std::uint8_t>((value >> (56 - 8 * index)) & 0xFF);
    }
}

} // namespace

RandomIdGenerator::RandomIdGenerator(std::uint64_t seed)
{
    if (seed == 0) {
        std::random_device device;
        seed = (static_cast<std::uint64_t>(device()) << 32) ^ device()
            ^ static_cast<std::uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count());
    }
    std::uint64_t state = seed;
    state0_ = SplitMix64(state);
    state1_ = SplitMix64(state);
}

Uuid RandomIdGenerator::Next()
{
    std::array<std::uint8_t, 16> bytes{};
    WriteBigEndian(bytes, 0, SplitMix64(state0_));
    WriteBigEndian(bytes, 8, SplitMix64(state1_));
    StampVersion4(bytes);
    return Uuid(bytes);
}

Uuid DeterministicIdGenerator::Next()
{
    std::array<std::uint8_t, 16> bytes{};
    // 前半は固定の目印。試験の出力を読んだときに、決定的IDだと一目で分かるようにする。
    WriteBigEndian(bytes, 0, 0x0000000000000000ULL);
    WriteBigEndian(bytes, 8, counter_);
    ++counter_;
    StampVersion4(bytes);
    return Uuid(bytes);
}

} // namespace kachakacha::v2::base
