#pragma once

#include "meter/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace aion2::meter {

/// The skill a code is a variant of: a class skill's code less its low four digits
/// (12240347 → 12240000). Other codes are their own skill.
[[nodiscard]] uint32_t skill_base(uint32_t code) noexcept;

/// The skill's English name, from src/meter/data/skills.tsv: the code's own row,
/// else its base skill's. Empty when neither is listed, such as a spirit's attacks.
[[nodiscard]] std::string_view skill_name(uint32_t code) noexcept;

/// The class a skill code belongs to: its first two digits, 11 Gladiator to 19
/// Brawler, for 8-digit codes. Unknown for every other code (items, system skills).
[[nodiscard]] AionClass class_of_skill(uint32_t code) noexcept;

/// The name, or "Skill <code>" for a code not in the table.
[[nodiscard]] std::string skill_label(uint32_t code);

} // namespace aion2::meter
