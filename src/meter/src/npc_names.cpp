#include "meter/npc_names.hpp"

#include <algorithm>
#include <iterator>

namespace aion2::meter {

namespace {

struct NpcRow {
    uint32_t code;
    const char* name;
};

constexpr NpcRow kNpcs[] = {
#include "npc_table.inc"
};

} // namespace

std::string_view npc_name(uint32_t npc_code) noexcept {
    const auto* it = std::lower_bound(std::begin(kNpcs), std::end(kNpcs), npc_code,
                                      [](const NpcRow& row, uint32_t code) { return row.code < code; });
    return it != std::end(kNpcs) && it->code == npc_code ? std::string_view(it->name) : std::string_view();
}

std::string npc_label(uint32_t npc_code) {
    const std::string_view name = npc_name(npc_code);
    if (!name.empty()) return std::string(name);
    return "NPC #" + std::to_string(npc_code);
}

} // namespace aion2::meter
