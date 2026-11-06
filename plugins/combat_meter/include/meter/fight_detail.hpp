#pragma once

#include "meter/death_log.hpp"
#include "meter/timeline.hpp"
#include "meter/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aion2::meter {

/// Where an effect was, as the Buffs & Debuffs tab sorts them.
enum class EffectSide : uint8_t {
    /// On a party member, from anyone but an enemy.
    PartyBuff,
    /// On a party member, from an enemy.
    PartyDebuff,
    /// On an enemy the party fought, from a party member or their summon.
    OnEnemy,
};

[[nodiscard]] std::string_view effect_side_name(EffectSide side) noexcept;
[[nodiscard]] EffectSide effect_side_from_name(std::string_view name) noexcept;

/// How long one effect from one caster was on one target during a fight.
struct EffectUptimeRow {
    /// game::effect_key: what makes two effects one to a player.
    uint32_t key{0};
    /// For its label and icon: the first application's.
    uint32_t buff_id{0};
    uint32_t source_skill{0};
    EffectSide side{EffectSide::PartyBuff};
    /// A party member's CombatantTotals::id, or the enemy's id.
    EntityId target{0};
    /// The player's name, or the enemy's label.
    std::string target_name;
    AionClass target_cls{AionClass::Unknown};
    /// Who put it there: a player's name, an NPC's label; empty when unknown.
    std::string caster;
    AionClass caster_cls{AionClass::Unknown};
    /// Instances put on during the fight, and those already running as it began.
    uint32_t applications{0};
    /// Time it was on, within the fight: stacks count once.
    double active_s{0.0};

    /// Of the fight's duration, 0..1.
    [[nodiscard]] double uptime(double duration_s) const noexcept;
    bool operator==(const EffectUptimeRow&) const = default;
};

/**
 * @brief What a fight keeps beyond its totals: its seconds, its deaths, its effects.
 *
 * Shared, never copied: FightSummary holds it through a shared_ptr, so the
 * snapshot that copies the live fight five times a second, and the list of recent
 * fights, copy a pointer. A record saved before the meter kept it has none.
 */
struct FightDetail {
    FightTimeline timeline;
    /// Oldest first.
    std::vector<DeathRecord> deaths;
    /// By side, then most time first.
    std::vector<EffectUptimeRow> effects;

    bool operator==(const FightDetail&) const = default;
};

} // namespace aion2::meter
