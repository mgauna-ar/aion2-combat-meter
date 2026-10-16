#pragma once

#include "common/config/json.hpp"
#include "meter/types.hpp"

#include <optional>

namespace aion2::meter {

/// The version written in every fight record ("v"). A reader skips records of a
/// version it does not know rather than misreading them.
inline constexpr int kFightRecordVersion = 1;

[[nodiscard]] config::JsonValue fight_to_json(const FightSummary& fight);
/// nullopt when `json` is not a fight record of a version this build reads.
[[nodiscard]] std::optional<FightSummary> fight_from_json(const config::JsonValue& json);

} // namespace aion2::meter
