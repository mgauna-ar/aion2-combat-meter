#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace aion2::game {

/// The monster's or NPC's English name, from src/game/data/npcs.tsv (aion2.app's
/// lists), by the npc code a spawn carries. Empty for a code not listed, such as a
/// summon's.
[[nodiscard]] std::string_view npc_name(uint32_t npc_code) noexcept;

/// The name, or "NPC #<code>" for a code not in the table.
[[nodiscard]] std::string npc_label(uint32_t npc_code);

} // namespace aion2::game
