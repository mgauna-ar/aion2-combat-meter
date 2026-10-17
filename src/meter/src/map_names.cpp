#include "meter/map_names.hpp"

#include <algorithm>
#include <iterator>

namespace aion2::meter {

namespace {

struct MapRow {
    uint32_t id;
    const char* name;
    bool dungeon;
};

constexpr MapRow kMaps[] = {
#include "map_table.inc"
};

const MapRow* find(uint32_t map_id) noexcept {
    const auto* it = std::lower_bound(std::begin(kMaps), std::end(kMaps), map_id,
                                      [](const MapRow& row, uint32_t id) { return row.id < id; });
    return it != std::end(kMaps) && it->id == map_id ? it : nullptr;
}

} // namespace

std::string_view map_name(uint32_t map_id) noexcept {
    const MapRow* row = find(map_id);
    return row != nullptr ? std::string_view(row->name) : std::string_view();
}

bool is_dungeon(uint32_t map_id) noexcept {
    const MapRow* row = find(map_id);
    return row != nullptr && row->dungeon;
}

std::string map_label(uint32_t map_id) {
    if (map_id == 0) return "Unknown map";
    const std::string_view name = map_name(map_id);
    if (!name.empty()) return std::string(name);
    return "Map " + std::to_string(map_id);
}

} // namespace aion2::meter
