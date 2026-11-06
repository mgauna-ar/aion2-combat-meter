#include "meter/types.hpp"

#include <array>
#include <utility>

namespace aion2::meter {

namespace {

constexpr std::array<std::pair<EndReason, std::string_view>, 7> kEndReasons{{
    {EndReason::None, "none"},
    {EndReason::BossKilled, "boss_killed"},
    {EndReason::MapChange, "map_change"},
    {EndReason::Wipe, "wipe"},
    {EndReason::Idle, "idle"},
    {EndReason::Manual, "manual"},
    {EndReason::CaptureLost, "capture_lost"},
}};

} // namespace

std::string_view end_reason_name(EndReason r) noexcept {
    for (const auto& [reason, name] : kEndReasons) {
        if (reason == r) return name;
    }
    return "none";
}

EndReason end_reason_from_name(std::string_view name) noexcept {
    for (const auto& [reason, n] : kEndReasons) {
        if (n == name) return reason;
    }
    return EndReason::None;
}

} // namespace aion2::meter
