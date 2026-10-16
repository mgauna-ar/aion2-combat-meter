#pragma once

#include "meter/types.hpp"

#include <cstdint>
#include <string>

namespace aion2::app {

/// A colour as float channels, without alpha: the overlay applies alpha per use.
struct Rgb {
    float r;
    float g;
    float b;
};

/// The overlay's colours.
namespace colors {
inline constexpr Rgb Background{0.06f, 0.07f, 0.10f};
inline constexpr Rgb Border{0.20f, 0.23f, 0.30f};
inline constexpr Rgb Text{0.92f, 0.93f, 0.95f};
inline constexpr Rgb TextDim{0.55f, 0.60f, 0.70f};
inline constexpr Rgb Accent{0.30f, 0.58f, 1.00f};
inline constexpr Rgb Live{0.95f, 0.30f, 0.35f};
inline constexpr Rgb Dps{0.96f, 0.55f, 0.20f};
inline constexpr Rgb Warn{0.95f, 0.70f, 0.20f};
inline constexpr Rgb Good{0.25f, 0.80f, 0.45f};
inline constexpr Rgb BossHp{0.80f, 0.20f, 0.25f};
inline constexpr Rgb Unknown{0.42f, 0.48f, 0.60f};
inline constexpr Rgb Local{0.30f, 0.58f, 1.00f};
} // namespace colors

/// The class's colour for bars and labels.
[[nodiscard]] Rgb class_color(meter::AionClass cls) noexcept;
/// A row's bar: the class colour, the accent for the local player whose class is
/// not known, and grey for anyone else's.
[[nodiscard]] Rgb row_color(const meter::CombatantTotals& row) noexcept;

/// "999", "45.6k", "1.23M", "4.56B".
[[nodiscard]] std::string format_amount(uint64_t value);
/// The same for a rate, with one decimal below a thousand.
[[nodiscard]] std::string format_rate(double value);
/// "62.3%".
[[nodiscard]] std::string format_percent(double fraction);

} // namespace aion2::app
