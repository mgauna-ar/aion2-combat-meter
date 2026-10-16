#include "meter/types.hpp"

#include <array>
#include <utility>

namespace aion2::meter {

namespace {

struct ClassNames {
    AionClass cls;
    std::string_view name;
    std::string_view shorthand;
};

constexpr std::array<ClassNames, 9> kClassNames{{
    {AionClass::Unknown, "Unknown", "?"},
    {AionClass::Gladiator, "Gladiator", "GLA"},
    {AionClass::Templar, "Templar", "TEM"},
    {AionClass::Assassin, "Assassin", "ASN"},
    {AionClass::Ranger, "Ranger", "RAN"},
    {AionClass::Sorcerer, "Sorcerer", "SOR"},
    {AionClass::Spiritmaster, "Spiritmaster", "SPM"},
    {AionClass::Cleric, "Cleric", "CLR"},
    {AionClass::Chanter, "Chanter", "CHN"},
}};

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

std::string_view class_name(AionClass c) noexcept {
    for (const auto& n : kClassNames) {
        if (n.cls == c) return n.name;
    }
    return "Unknown";
}

std::string_view class_short(AionClass c) noexcept {
    for (const auto& n : kClassNames) {
        if (n.cls == c) return n.shorthand;
    }
    return "?";
}

AionClass class_from_name(std::string_view name) noexcept {
    for (const auto& n : kClassNames) {
        if (n.name == name) return n.cls;
    }
    return AionClass::Unknown;
}

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
