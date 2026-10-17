#ifndef AION2_VERSION_HPP
#define AION2_VERSION_HPP

// The one place the release version is written. CMakeLists.txt parses the three
// numbers below for project(VERSION), and app.rc includes this file for the
// executable's version resource, so everything above the RC_INVOKED guard must stay
// plain preprocessor that rc.exe understands: no #pragma once, no C++.
#define AION2_VERSION_MAJOR 0
#define AION2_VERSION_MINOR 1
#define AION2_VERSION_PATCH 2
#define AION2_VERSION_STRING "0.1.2"

#ifndef RC_INVOKED

#include <cstdint>
#include <string_view>

namespace aion2 {

constexpr uint16_t VERSION_MAJOR = AION2_VERSION_MAJOR;
constexpr uint16_t VERSION_MINOR = AION2_VERSION_MINOR;
constexpr uint16_t VERSION_PATCH = AION2_VERSION_PATCH;
constexpr const char* VERSION_STRING = AION2_VERSION_STRING;

namespace detail {
/// `n` in decimal followed by `sep`, consumed from the front of `s`; false when `s`
/// does not start that way.
constexpr bool consume_version_part(std::string_view& s, unsigned n, char sep) {
    char digits[8]{};
    int len = 0;
    do {
        digits[len++] = static_cast<char>('0' + n % 10);
        n /= 10;
    } while (n != 0 && len < 8);
    for (int i = len - 1; i >= 0; --i) {
        if (s.empty() || s.front() != digits[i]) return false;
        s.remove_prefix(1);
    }
    if (sep == '\0') return s.empty();
    if (s.empty() || s.front() != sep) return false;
    s.remove_prefix(1);
    return true;
}

constexpr bool version_string_matches_numbers() {
    std::string_view s = AION2_VERSION_STRING;
    return consume_version_part(s, AION2_VERSION_MAJOR, '.') &&
           consume_version_part(s, AION2_VERSION_MINOR, '.') &&
           consume_version_part(s, AION2_VERSION_PATCH, '\0');
}
} // namespace detail

static_assert(detail::version_string_matches_numbers(),
              "AION2_VERSION_STRING must spell AION2_VERSION_MAJOR.MINOR.PATCH");

} // namespace aion2

#endif // RC_INVOKED

#endif // AION2_VERSION_HPP
