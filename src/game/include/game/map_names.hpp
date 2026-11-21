#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace aion2::game {

/// Map ids from here up are instances: the party dungeons, the raids, the Awakening
/// tiers and the daily dungeons (600001 to 910067). Not every instance is: the
/// matched dungeons (156xxx), Ascension, the seal dungeons and the arenas sit below,
/// and the table names them (is_dungeon). The one open-world id a capture has shown
/// is the town's, 1010.
inline constexpr uint32_t kFirstInstanceMapId = 600000;

/// The map's English name, from src/game/data/maps.tsv, by the map id 21 36 carries
/// ("Krao Cave (Conquest)", "Impetusium Arena"). Empty for an id not listed, which
/// today is every open-world zone.
[[nodiscard]] std::string_view map_name(uint32_t map_id) noexcept;

/// The name; for an id not in the table, "Dungeon <id>" or "Map <id>" (is_dungeon);
/// "Unknown map" for 0 (no map load seen yet).
[[nodiscard]] std::string map_label(uint32_t map_id);

/// Whether the map is a dungeon, for the overlays' show_in_dungeons_only and the
/// history's save_only_dungeons: an id of kFirstInstanceMapId or more, named or not
/// (a dungeon a patch adds is one before the meter can name it), or an instance the
/// table marks below it: every one but Chaotic Reshanta, the Abyss's open PvP zones,
/// and the open world's events (the user's call). The arenas and battlefields count:
/// nothing hides in a real dungeon. False for 0.
[[nodiscard]] bool is_dungeon(uint32_t map_id) noexcept;

/// Whether only the group that entered the map is in it, so a stranger in view is
/// one of them: an id of kFirstInstanceMapId or more, the party dungeons and raids.
/// Not the instances below it, the arenas and battlefields among them, where the
/// other side is inside too, nor any open map. The meter counts everyone in view as
/// party here when it has no roster, and the Combat Power plugin marks no one as an
/// enemy, a dungeon's party mixing both factions.
[[nodiscard]] bool is_party_instance(uint32_t map_id) noexcept;

} // namespace aion2::game
