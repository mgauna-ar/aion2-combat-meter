#include "common/time_format.hpp"

#include "common/os/local_time.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace aion2::common {

CivilTime utc_time(Micros ts) noexcept {
    // Howard Hinnant's civil_from_days.
    const auto secs = static_cast<int64_t>(ts / kMicrosPerSecond);
    int64_t z = secs / 86400;
    const int64_t rem = secs % 86400;
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    CivilTime c;
    c.day = doy - (153 * mp + 2) / 5 + 1;
    c.month = mp < 10 ? mp + 3 : mp - 9;
    c.year = static_cast<int64_t>(yoe) + era * 400 + (c.month <= 2 ? 1 : 0);
    c.hour = static_cast<unsigned>(rem / 3600);
    c.minute = static_cast<unsigned>((rem / 60) % 60);
    c.second = static_cast<unsigned>(rem % 60);
    return c;
}

LocalDay civil_day(int64_t year, unsigned month, unsigned day) noexcept {
    if (month < 1 || month > 12) month = 1;
    if (day < 1 || day > 31) day = 1;
    // Howard Hinnant's days_from_civil, the inverse of utc_time's.
    const int64_t y = static_cast<int64_t>(year) - (month <= 2 ? 1 : 0);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    LocalDay d;
    d.serial = era * 146097 + static_cast<int64_t>(doe) - 719468;
    d.year = year;
    d.month = month;
    d.day = day;
    // 1970-01-01 was a Thursday.
    const int64_t w = (d.serial + 4) % 7;
    d.weekday = static_cast<unsigned>(w < 0 ? w + 7 : w);
    return d;
}

LocalDay local_day(Micros ts) {
    const std::tm t = os::local_time(static_cast<std::time_t>(ts / kMicrosPerSecond));
    return civil_day(static_cast<int64_t>(t.tm_year) + 1900, static_cast<unsigned>(t.tm_mon + 1),
                     static_cast<unsigned>(t.tm_mday));
}

std::string utc_file_stamp(Micros ts) {
    const CivilTime c = utc_time(ts);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04lld%02u%02u-%02u%02u%02uZ", static_cast<long long>(c.year), c.month, c.day,
                  c.hour, c.minute, c.second);
    return buf;
}

std::string local_clock(Micros ts) {
    const std::tm t = os::local_time(static_cast<std::time_t>(ts / kMicrosPerSecond));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    return buf;
}

std::string clock_duration(double seconds) {
    // NaN and negatives read as 0; the cap keeps the cast defined.
    if (!(seconds > 0.0)) seconds = 0.0;
    const auto total = static_cast<unsigned long long>(std::min(seconds, 1e12));
    char buf[32];
    if (total >= 3600) {
        std::snprintf(buf, sizeof(buf), "%llu:%02llu:%02llu", total / 3600, (total / 60) % 60, total % 60);
    } else {
        std::snprintf(buf, sizeof(buf), "%llu:%02llu", total / 60, total % 60);
    }
    return buf;
}

} // namespace aion2::common
