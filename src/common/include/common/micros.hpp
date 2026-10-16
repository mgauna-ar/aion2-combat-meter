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

[[nodiscard]] constexpr Micros from_seconds(double seconds) noexcept {
    return seconds <= 0.0 ? 0 : static_cast<Micros>(seconds * static_cast<double>(kMicrosPerSecond));
}

} // namespace aion2
