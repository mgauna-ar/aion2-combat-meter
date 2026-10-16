#include "meter/class_codes.hpp"

#include <array>
#include <utility>

namespace aion2::meter {

namespace {

/// {wire code, class}. Empty until a capture confirms the codes; see the header.
constexpr std::array<std::pair<uint32_t, AionClass>, 0> kClassCodes{};

} // namespace

AionClass class_from_code(uint32_t code) noexcept {
    for (const auto& [wire, cls] : kClassCodes) {
        if (wire == code) return cls;
    }
    return AionClass::Unknown;
}

} // namespace aion2::meter
