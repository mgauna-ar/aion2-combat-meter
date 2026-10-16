#pragma once

#include "protocol/events.hpp"
#include <cstdint>

namespace aion2::proto {

/// Spawn packets carry large ids where combat packets carry small ones for the
/// same entity; above this a raw id is folded into the combat id space.
inline constexpr uint64_t kFoldAbove = 1'000'000;

/// Whether ids are folded. On by default; aion2meter-replay --no-fold turns it off
/// process-wide to compare, should a patch change the scheme.
void set_entity_id_folding(bool enabled) noexcept;
[[nodiscard]] bool entity_id_folding() noexcept;

/// The one place raw ids become EntityIds: above kFoldAbove, (raw & 0x3FFF) | 0x4000.
[[nodiscard]] EntityId normalize_entity_id(uint64_t raw) noexcept;

} // namespace aion2::proto
