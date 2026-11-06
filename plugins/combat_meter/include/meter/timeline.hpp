#pragma once

#include "common/micros.hpp"
#include "meter/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aion2::meter {

/// Longest a timeline runs: a fight past an hour keeps its first hour. Its totals
/// still count the rest.
inline constexpr size_t kTimelineMaxSeconds = 3600;

/// What one player did in one second of a fight.
struct TimelineBin {
    /// Direct hits and ticks, theirs and their summons'.
    uint32_t damage{0};
    uint32_t healing{0};
    uint32_t taken{0};

    bool operator==(const TimelineBin&) const = default;
};
static_assert(sizeof(TimelineBin) == 12, "A timeline's memory is sized by this");

/// One player's fight: bins[i] is second i from the fight's start.
struct TimelineRow {
    /// The row's CombatantTotals::id.
    EntityId id{0};
    std::vector<TimelineBin> bins;

    bool operator==(const TimelineRow&) const = default;
};

/// The boss's HP over the fight, from the server's own figures (00 8D and its spawn):
/// never damage, which is in other units.
struct BossHpSeries {
    /// The second of permille[0].
    uint32_t first_s{0};
    /// 0..1000 of its max HP, each second carrying the last figure forward.
    std::vector<uint16_t> permille;

    [[nodiscard]] bool empty() const noexcept { return permille.empty(); }
    /// Permille at `second`, or -1 before the first figure.
    [[nodiscard]] int at(size_t second) const noexcept;
    bool operator==(const BossHpSeries&) const = default;
};

/// A party-wide buff one player kept up: one effect from one caster on two or more
/// party members, merged over everyone it was on.
struct BuffWindow {
    /// game::effect_key.
    uint32_t key{0};
    /// For its label and icon.
    uint32_t buff_id{0};
    uint32_t source_skill{0};
    std::string caster;
    AionClass caster_cls{AionClass::Unknown};
    /// Since the fight's start.
    double begin_s{0.0};
    double end_s{0.0};

    bool operator==(const BuffWindow&) const = default;
};

/// A fight second by second, for the Timeline tab.
struct FightTimeline {
    /// How many seconds the rows and the boss line cover. It can run past the
    /// fight's duration, which ends at the party's last hit: hits taken and deaths
    /// can come after it.
    uint32_t seconds{0};
    /// By id.
    std::vector<TimelineRow> rows;
    BossHpSeries boss_hp;
    /// By start.
    std::vector<BuffWindow> buffs;

    [[nodiscard]] const TimelineRow* row(EntityId id) const noexcept;
    bool operator==(const FightTimeline&) const = default;
};

/// What a timeline line shows.
enum class TimelineMetric : uint8_t { Damage, Healing, Taken };

[[nodiscard]] uint32_t timeline_value(const TimelineBin& bin, TimelineMetric metric) noexcept;

/// `amount` added to `into`, which stops at its largest value.
void add_capped(uint32_t& into, uint64_t amount) noexcept;

/// The bin for capture time `ts` in a fight that started at `start`, `bins` grown to
/// reach it; nullptr at or past kTimelineMaxSeconds. A time before the start is
/// second 0.
[[nodiscard]] TimelineBin* timeline_bin(std::vector<TimelineBin>& bins, Micros start, Micros ts);

/// `metric` for each of `length` seconds, each an average of the `window_s` seconds
/// centred on it that fall inside the fight, weighted towards the middle (1, 2, 3, 2,
/// 1 for five). Seconds past the last bin count as 0.
[[nodiscard]] std::vector<float> smoothed_series(const std::vector<TimelineBin>& bins, size_t length,
                                                 TimelineMetric metric, size_t window_s);

/// `values` averaged down to at most `points` points, one per equal span.
[[nodiscard]] std::vector<float> downsample(const std::vector<float>& values, size_t points);

} // namespace aion2::meter
