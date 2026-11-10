#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace aion2::game {

/// Map ids from here up are instances. Every dungeon tier known sits there
/// (600001 to 690046, and the Awakening tiers at 910011 and up), and the one
/// open-world id a capture has shown is the town's, 1010.
inline constexpr uint32_t kFirstInstanceMapId = 600000;

/// The map's English name, from src/game/data/maps.tsv: a dungeon tier's id as
/// aion2.app lists it ("Krao Cave (Conquest)"), or a row added from a capture.
/// Empty for an id not listed, which today is every open-world zone.
[[nodiscard]] std::string_view map_name(uint32_t map_id) noexcept;

/// The name; for an id not in the table, "Dungeon <id>" or "Map <id>" (is_dungeon);
/// "Unknown map" for 0 (no map load seen yet).
[[nodiscard]] std::string map_label(uint32_t map_id);

/// Whether the map is a dungeon, by its id alone (kFirstInstanceMapId), never by the
/// table: a dungeon a patch adds is one before the meter can name it. An arena or
/// battlefield up there would count too, which is the side to err on: nothing hides
/// in a real dungeon. False for 0.
[[nodiscard]] bool is_dungeon(uint32_t map_id) noexcept;

} // namespace aion2::game
