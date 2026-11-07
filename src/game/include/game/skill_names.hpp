#pragma once

#include "game/classes.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace aion2::game {

/// The skill a code is a variant of: a class skill's code less its low four digits
/// (12240347 → 12240000), or less its low two in the class's common block, where
/// the weapon and Dodge are a hundred apart (12000101 → 12000100, Dodge). Other
/// codes are their own skill.
[[nodiscard]] uint32_t skill_base(uint32_t code) noexcept;

/// The code a breakdown files a hit under: the code itself when the table names it,
/// else its base skill. Variants of one skill share a row; one with its own name
/// (Dismiss: Earth Spirit) keeps it.
[[nodiscard]] uint32_t skill_key(uint32_t code) noexcept;

/// The skill's English name, from src/game/data/skills.tsv: the code's own row,
/// else its base skill's. Empty when neither is listed, such as a spirit's attacks.
[[nodiscard]] std::string_view skill_name(uint32_t code) noexcept;

/// The class a skill code belongs to: its first two digits, 11 Gladiator to 19
/// Brawler, for 8-digit codes. Unknown for every other code (items, system skills).
[[nodiscard]] AionClass class_of_skill(uint32_t code) noexcept;

/// The name, or "Skill <code>" for a code not in the table.
[[nodiscard]] std::string skill_label(uint32_t code);

/// The skill that names an effect (proto::BuffApplied): the skill that applied it,
/// when the record names one the table lists, else the skill its id points to (the
/// id over ten). 0 for neither: monster, item and system effects.
[[nodiscard]] uint32_t effect_skill(uint32_t buff_id, uint32_t source_skill = 0) noexcept;

/// An effect's name (proto::BuffApplied): the skill that applied it, when the record
/// names one the table lists, else the skill its id points to (the id over ten:
/// Undefeated Mantra's 181900411 is 18190041). Empty for neither: monster, item and
/// system effects. In the second capture 78% of effects got a name.
[[nodiscard]] std::string_view effect_name(uint32_t buff_id, uint32_t source_skill = 0) noexcept;

/// effect_name, or "Effect <id>".
[[nodiscard]] std::string effect_label(uint32_t buff_id, uint32_t source_skill = 0);

/// What makes two effects the same one to a player: the skill_key of the skill that
/// names it (so Undefeated Mantra from any Chanter, at any level, is one), else the
/// effect id. A watch list holds these.
[[nodiscard]] uint32_t effect_key(uint32_t buff_id, uint32_t source_skill = 0) noexcept;

} // namespace aion2::game
