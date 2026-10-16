#pragma once

#include "common/micros.hpp"

#include <cstdint>
#include <string>

namespace aion2::common {

struct CivilTime {
    int64_t year{1970};
    unsigned month{1};
    unsigned day{1};
    unsigned hour{0};
    unsigned minute{0};
    unsigned second{0};
};

/// `ts` as a UTC calendar time, computed rather than through gmtime_r/gmtime_s.
[[nodiscard]] CivilTime utc_time(Micros ts) noexcept;

/// "20261016-120000Z": a sortable stamp for file names.
[[nodiscard]] std::string utc_file_stamp(Micros ts);

/// "12:00" in the machine's local time, for the overlay's history list.
[[nodiscard]] std::string local_clock(Micros ts);

/// "1:35" or "1:02:03" for a duration.
[[nodiscard]] std::string clock_duration(double seconds);

} // namespace aion2::common
