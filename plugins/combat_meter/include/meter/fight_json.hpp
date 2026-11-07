#pragma once

#include "common/config/json.hpp"
#include "meter/types.hpp"

#include <optional>

namespace aion2::meter {

/// The version written in every fight record ("v"). A reader skips records of a
/// version it does not know rather than misreading them. 2 added the skill,
/// healing and damage-taken breakdowns and the map visit; a 1 reads without them.
///
/// Fields a reader can do without come with no new version: a missing field reads
/// as empty, and older builds ignore what they do not know, where a new version
/// would make them skip the whole record. The timeline, the deaths, the effects and
/// active time came that way, and a record without them reads with no FightDetail.
inline constexpr int kFightRecordVersion = 2;
inline constexpr int kOldestFightRecordVersion = 1;

/// `json["v"]` names a version this build reads.
[[nodiscard]] bool readable_version(const config::JsonValue& json);
/// A count from a record: 0 for a negative, missing or NaN value, and capped
/// rather than undefined past 2^64 (1e999 in a file reads as infinity).
[[nodiscard]] uint64_t json_u64(const config::JsonValue& v);

[[nodiscard]] config::JsonValue fight_to_json(const FightSummary& fight);
/// nullopt when `json` is not a fight record of a version this build reads.
[[nodiscard]] std::optional<FightSummary> fight_from_json(const config::JsonValue& json);

} // namespace aion2::meter
