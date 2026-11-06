#pragma once

#include "common/micros.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace aion2::meter {

/// A skill's row in src/meter/data/cooldowns.tsv (tools/gen_cooldown_table.py).
struct SkillCooldown {
    uint32_t code{0};
    /// A stigma keeps every specialty it unlocked; an active skill's are picked.
    bool stigma{false};
    /// What aion2.app lists, the same at every level.
    uint32_t cooldown_ms{0};
    /// What each specialty (1 to 5, in the page's order) takes off its own cooldown.
    std::array<uint32_t, 5> cut_ms{};
    /// The uses each specialty adds before the cooldown ("+1 consecutive use").
    std::array<uint32_t, 5> extra_uses{};
};

/// The row of the skill `code` is a variant of, or null for a skill with no cooldown
/// listed.
[[nodiscard]] const SkillCooldown* find_cooldown(uint32_t code) noexcept;

/// The cooldown a cast of `code` starts, by the table: listed, less what its
/// specialties cut. The cast code's variant digits say which are in play: an active
/// skill's name its picks (12350130: 1 and 3), a stigma's tens digit counts the ones
/// it unlocked (18190041: 1 to 4). 0 for a skill with no cooldown listed. The game's
/// figure is shorter still by the Illusion stat's reduction, which the meter cannot
/// read yet; CooldownTracker learns it.
[[nodiscard]] uint32_t listed_cooldown_ms(uint32_t code) noexcept;

/// How many times a cast of `code` can be used before all of it is cooling: 1, more
/// with a "+1 consecutive use" specialty in play (Shield of Protection's).
[[nodiscard]] uint32_t skill_charges(uint32_t code) noexcept;

/// One of the local player's skills cooling down, as the aura overlay shows it.
struct CooldownEntry {
    /// skill_base of the code.
    uint32_t skill{0};
    std::string label;
    /// When it is ready again, on the capture clock.
    Micros ready_at{0};
    /// The whole cooldown, for the length of a bar.
    uint64_t duration_ms{0};
    /// The server said what is left (47 38). Otherwise it is an estimate.
    bool exact{false};

    bool operator==(const CooldownEntry&) const = default;
};

/**
 * @brief The local player's cooldowns, estimated: the server sends none as a skill is
 * used (01 38), only when something cuts or resets one (47 38).
 *
 * A use starts the table's figure (listed_cooldown_ms), or the shortest gap seen
 * between two uses of the skill this session when that is shorter: the game's
 * cooldown reduction, which the table does not know, shows there. A gap with a 47 38
 * for the skill in it says nothing (the cooldown was cut or reset), nor does one
 * under kMinLearnShare of the figure (a skill another one triggered, such as
 * Judgment). A 47 38 sets what is left exactly.
 *
 * A skill with more than one use (skill_charges) recharges each on its own: a use
 * takes the one ready soonest, and it shows as cooling only once none is ready. Its
 * gaps say nothing of the cooldown, so it learns none.
 */
class CooldownTracker {
public:
    /// A gap shorter than this share of the table's figure is a trigger or a reset,
    /// not the cooldown. The Illusion stat's reduction is capped at 60%; the second
    /// capture's character's cooldowns ran 8 to 12% under the table's.
    static constexpr double kMinLearnShare = 0.5;

    void on_used(uint32_t code, Micros ts);
    void on_set(uint32_t skill, uint32_t remaining_ms, Micros ts);
    /// Another character: what was learned was theirs.
    void clear() noexcept { m_skills.clear(); }

    /// The skills cooling at `now`, the soonest ready first.
    [[nodiscard]] std::vector<CooldownEntry> snapshot(Micros now) const;
    /// The cooldown the next use of `code` would start, for tests.
    [[nodiscard]] uint64_t estimate_ms(uint32_t code) const;

private:
    struct State {
        Micros used_at{0};
        /// When each use is ready again; one for most skills.
        std::vector<Micros> ready_at;
        uint64_t duration_ms{0};
        bool exact{false};
        /// A 47 38 came since the last use.
        bool set_since_use{false};
        /// The table's figure for the code last used, and the shortest gap seen
        /// under it; 0 for none.
        uint64_t listed_ms{0};
        uint64_t learned_ms{0};
    };

    std::unordered_map<uint32_t, State> m_skills;
};

} // namespace aion2::meter
