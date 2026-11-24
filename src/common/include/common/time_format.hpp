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

/// A calendar day, without the time.
struct LocalDay {
    /// Days since 1970-01-01: two days compare by it, across a DST change too.
    int64_t serial{0};
    int64_t year{1970};
    unsigned month{1};
    unsigned day{1};
    /// 0 for Sunday.
    unsigned weekday{4};
    [[nodiscard]] bool operator==(const LocalDay&) const = default;
};

/// The day `year`-`month`-`day`, its serial and weekday computed (Howard Hinnant's
/// days_from_civil). A month or day out of range is taken as 1.
[[nodiscard]] LocalDay civil_day(int64_t year, unsigned month, unsigned day) noexcept;

/// The machine's local calendar day at `ts`.
[[nodiscard]] LocalDay local_day(Micros ts);

/// "20261016-120000Z": a sortable stamp for file names.
[[nodiscard]] std::string utc_file_stamp(Micros ts);

/// "12:00" in the machine's local time, for the overlay's history list.
[[nodiscard]] std::string local_clock(Micros ts);

/// "1:35" or "1:02:03" for a duration.
[[nodiscard]] std::string clock_duration(double seconds);

} // namespace aion2::common
