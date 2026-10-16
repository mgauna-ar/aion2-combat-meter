#pragma once

#include <ctime>

namespace aion2::os {

/// Broken-down local time for a time_t: localtime_s on Windows, localtime_r on
/// the mock. Both are thread-safe, unlike std::localtime.
[[nodiscard]] std::tm local_time(std::time_t t) noexcept;

} // namespace aion2::os
