#include "protocol/entity_id.hpp"

#include <atomic>

namespace aion2::proto {

namespace {
std::atomic<bool> g_folding{true};
} // namespace

void set_entity_id_folding(bool enabled) noexcept { g_folding.store(enabled, std::memory_order_relaxed); }

bool entity_id_folding() noexcept { return g_folding.load(std::memory_order_relaxed); }

EntityId normalize_entity_id(uint64_t raw) noexcept {
    if (raw > kFoldAbove && entity_id_folding()) return static_cast<EntityId>((raw & 0x3FFF) | 0x4000);
    return static_cast<EntityId>(raw);
}

} // namespace aion2::proto
