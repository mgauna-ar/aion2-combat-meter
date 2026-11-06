#include "game/classes.hpp"

#include <array>

namespace aion2::game {

namespace {

struct ClassNames {
    AionClass cls;
    std::string_view name;
    std::string_view shorthand;
};

/// Spiritmaster's display name is the one the skill data uses for codes 16xxxxxx.
constexpr std::array<ClassNames, 10> kClassNames{{
    {AionClass::Unknown, "Unknown", "?"},
    {AionClass::Gladiator, "Gladiator", "GLA"},
    {AionClass::Templar, "Templar", "TEM"},
    {AionClass::Assassin, "Assassin", "ASN"},
    {AionClass::Ranger, "Ranger", "RAN"},
    {AionClass::Sorcerer, "Sorcerer", "SOR"},
    {AionClass::Spiritmaster, "Elementalist", "ELE"},
    {AionClass::Cleric, "Cleric", "CLR"},
    {AionClass::Chanter, "Chanter", "CHN"},
    {AionClass::Brawler, "Brawler", "BRW"},
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
    if (name == "Spiritmaster") return AionClass::Spiritmaster;
    return AionClass::Unknown;
}

} // namespace aion2::game
