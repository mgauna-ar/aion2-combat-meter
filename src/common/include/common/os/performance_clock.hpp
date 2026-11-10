#pragma once

#include "common/micros.hpp"

namespace aion2::os {

/// The machine's performance counter, in microseconds since it started counting (the
/// last boot): QueryPerformanceCounter on Windows, the steady clock on the mock.
///
/// One counter for every process, read in our own. The game stamps its pings with
/// it (Unreal's FPlatformTime), which is how the Ping plugin tells an echo's age
/// without sending anything (AGENTS.md, Protocol, Pong).
[[nodiscard]] Micros performance_counter_now() noexcept;

} // namespace aion2::os
