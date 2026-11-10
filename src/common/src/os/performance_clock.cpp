#include "common/os/performance_clock.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <chrono>
#endif

namespace aion2::os {

Micros performance_counter_now() noexcept {
#ifdef _WIN32
    // The frequency is fixed at boot.
    static const uint64_t per_second = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? static_cast<uint64_t>(f.QuadPart) : uint64_t{1};
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    const auto ticks = static_cast<uint64_t>(c.QuadPart);
    // Whole seconds and the rest apart: ticks times a million would wrap in days.
    return (ticks / per_second) * kMicrosPerSecond + (ticks % per_second) * kMicrosPerSecond / per_second;
#else
    const auto since = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<Micros>(std::chrono::duration_cast<std::chrono::microseconds>(since).count());
#endif
}

} // namespace aion2::os
