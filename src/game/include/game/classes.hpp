#pragma once

#include <cstdint>
#include <string_view>

namespace aion2::game {

enum class AionClass : uint8_t {
    Unknown,
    Gladiator,
    Templar,
    Assassin,
    Ranger,
    Sorcerer,
    Spiritmaster,
    Cleric,
    Chanter,
    /// Skill codes 19xxxxxx: listed by the skill database, not yet seen on Global.
    Brawler,
};

[[nodiscard]] std::string_view class_name(AionClass c) noexcept;
/// Three letters for a narrow column.
[[nodiscard]] std::string_view class_short(AionClass c) noexcept;
[[nodiscard]] AionClass class_from_name(std::string_view name) noexcept;

} // namespace aion2::game
