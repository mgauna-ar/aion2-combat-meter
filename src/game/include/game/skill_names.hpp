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

/// What puts an effect on, as src/game/data/effects.tsv says; for an effect it does
/// not list, Skill when a skill names it (effect_skill), else Unknown.
enum class EffectKind : uint8_t {
    Unknown,
    /// A class skill's.
    Skill,
    /// An item's: a scroll, food, a potion.
    Item,
};

[[nodiscard]] EffectKind effect_kind(uint32_t buff_id, uint32_t source_skill = 0) noexcept;

/// What sort of item an item's effect comes from, as the Combat Meter's Consumables
/// view sorts them.
enum class ItemEffect : uint8_t {
    /// Not an item's, or an item the view leaves out (talismans, candy, seeds).
    None,
    Potion,
    /// Food and drinks.
    Food,
    Scroll,
    /// A weapon's or armour's Theostone going off.
    Theostone,
};

/// By the id's first two digits, for an effect src/game/data/effects.tsv lists as an
/// item's: 20 potions, 21, 27 and 28 food and drinks, 22 scrolls, 30 Theostones.
[[nodiscard]] ItemEffect item_effect(uint32_t buff_id) noexcept;

/// An effect's name (proto::BuffApplied): its own, from src/game/data/effects.tsv
/// (Punishment's 120900001 is Executor); else the skill that applied it, when the
/// record names one the skill table lists; else the skill its id points to (the id
/// over ten: Undefeated Mantra's 181900411 is 18190041). Empty for none of them. In
/// the second capture 78% of effects got a name from the skills alone.
[[nodiscard]] std::string_view effect_name(uint32_t buff_id, uint32_t source_skill = 0) noexcept;

/// effect_name, or "Effect <id>".
[[nodiscard]] std::string effect_label(uint32_t buff_id, uint32_t source_skill = 0);

/// What makes two effects the same one to a player: the skill_key of the skill that
/// names it (so Undefeated Mantra from any Chanter, at any level, is one), else the
/// effect id. A watch list holds these.
[[nodiscard]] uint32_t effect_key(uint32_t buff_id, uint32_t source_skill = 0) noexcept;

/// The key is a skill's, not an effect id: the skill table lists that very code, as
/// every skill effect_key gives does. An effect id read as a skill by its ten
/// thousands names the wrong one: the monsters' Burn, 12000121, is no Dodge.
[[nodiscard]] bool is_skill_key(uint32_t key) noexcept;

/// What an effect_key is called with no effect of it at hand (a watch list's, before
/// the plugin has seen it): its skill's name, or the effect's own as effect_label
/// reads its id (an item's or a monster's: Speed Scroll, Burn).
[[nodiscard]] std::string effect_key_label(uint32_t key);

} // namespace aion2::game
