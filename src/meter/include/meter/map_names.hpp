#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace aion2::meter {

/// The map's English name, from src/meter/data/maps.tsv: a dungeon tier's id as
/// aion2.app lists it ("Krao Cave (Conquest)"), or a row added from a capture.
/// Empty for an id not listed, which today is every open-world zone.
[[nodiscard]] std::string_view map_name(uint32_t map_id) noexcept;

/// The name, or "Map <id>" for an id not in the table, or "Unknown map" for 0 (no map
/// load seen yet).
[[nodiscard]] std::string map_label(uint32_t map_id);

} // namespace aion2::meter
