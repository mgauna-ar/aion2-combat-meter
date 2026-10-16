#include "common/os/local_time.hpp"

namespace aion2::os {

std::tm local_time(std::time_t t) noexcept {
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

} // namespace aion2::os
