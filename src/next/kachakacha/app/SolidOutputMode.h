#pragma once

namespace kachakacha::v2::app {
enum class SolidOutputMode { SolidOnly, WiresOnly, Both };
constexpr bool IncludesSolid(SolidOutputMode mode) { return mode != SolidOutputMode::WiresOnly; }
constexpr bool IncludesWires(SolidOutputMode mode) { return mode != SolidOutputMode::SolidOnly; }
}
