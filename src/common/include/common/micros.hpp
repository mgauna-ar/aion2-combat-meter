#pragma once

#include <cstdint>

namespace aion2 {

/// A capture timestamp: microseconds since the Unix epoch, as the capture driver
/// stamped the packet. Everything downstream of capture runs on this clock and
/// never reads the system's, so replaying a file gives exactly what the live meter
/// computed from the same packets.
using Micros = uint64_t;

inline constexpr Micros kMicrosPerMilli = 1'000;
inline constexpr Micros kMicrosPerSecond = 1'000'000;

[[nodiscard]] constexpr double to_seconds(Micros us) noexcept {
    return static_cast<double>(us) / static_cast<double>(kMicrosPerSecond);
}

/// Longer than anything the meter times: 2^62 µs, about 146,000 years, which a
/// timestamp can still be added to without wrapping.
inline constexpr Micros kForever = Micros{1} << 62;

/// NaN and negatives are 0, and anything longer than kForever is kForever.
[[nodiscard]] constexpr Micros from_seconds(double seconds) noexcept {
    if (!(seconds > 0.0)) return 0;
    const double us = seconds * static_cast<double>(kMicrosPerSecond);
    return us >= static_cast<double>(kForever) ? kForever : static_cast<Micros>(us);
}

} // namespace aion2
