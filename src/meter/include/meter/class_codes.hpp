#pragma once

#include "meter/types.hpp"
#include <cstdint>

namespace aion2::meter {

/// The class a wire class code (Identity::class_code) stands for.
///
/// The codes are not in the community notes, so the table starts empty and every
/// player shows as Unknown with the raw code beside it in aion2meter-replay
/// --events. Fill it from a capture where each class's name is known
/// (.claude/skills/after-game-patch).
[[nodiscard]] AionClass class_from_code(uint32_t code) noexcept;

} // namespace aion2::meter
