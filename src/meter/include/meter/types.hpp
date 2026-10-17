#pragma once

#include "common/micros.hpp"
#include "protocol/events.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aion2::meter {

using EntityId = proto::EntityId;

enum class AionClass : uint8_t {
    Unknown,
    Gladiator,
    Templar,
    Assassin,
    Ranger,
    Sorcerer,
    Spiritmaster,
    Cleric,
    Chanter,
    /// Skill codes 19xxxxxx: listed by the skill database, not yet seen on Global.
    Brawler,
};

[[nodiscard]] std::string_view class_name(AionClass c) noexcept;
/// Three letters for a narrow column.
[[nodiscard]] std::string_view class_short(AionClass c) noexcept;
[[nodiscard]] AionClass class_from_name(std::string_view name) noexcept;

/// Which side of a PvE fight an entity is on. Damage only crosses sides, so one
/// side known for either end of a hit gives the other.
enum class Side : uint8_t {
    Unknown,
    /// Players and the summons and effects they own.
    Player,
    Enemy,
};

enum class EndReason : uint8_t {
    None,
    BossKilled,
    MapChange,
    Wipe,
    Idle,
    Manual,
    CaptureLost,
};

[[nodiscard]] std::string_view end_reason_name(EndReason r) noexcept;
[[nodiscard]] EndReason end_reason_from_name(std::string_view name) noexcept;

struct HitCounts {
    uint64_t hits{0};
    uint64_t crits{0};
    uint64_t back{0};
    uint64_t front{0};
    /// The target blocked it (the game's BLOC).
    uint64_t blocks{0};
    /// A multi-hit (the game's MULT): one hit, however many strikes.
    uint64_t multi{0};
    uint64_t perfect{0};
    uint64_t doubles{0};
    uint64_t heavy{0};

    [[nodiscard]] bool operator==(const HitCounts&) const = default;

    /// Of the hits, 0..1.
    [[nodiscard]] double rate(uint64_t HitCounts::*count) const noexcept {
        return hits == 0 ? 0.0 : static_cast<double>(this->*count) / static_cast<double>(hits);
    }
};

/// One skill's damage in a player's breakdown: its direct hits and its ticks.
struct SkillTotals {
    /// skill_key(): the code's own row in the skill table, else its base skill.
    uint32_t code{0};
    /// Direct hits and ticks.
    uint64_t damage{0};
    uint64_t tick_damage{0};
    uint64_t ticks{0};
    /// Of the direct hits.
    uint64_t min_hit{0};
    uint64_t max_hit{0};
    /// Direct hits only: ticks carry no crit, modifiers or direction.
    HitCounts hits;

    [[nodiscard]] bool operator==(const SkillTotals&) const = default;
};

/// Healing, or damage taken, from one skill.
struct SkillAmount {
    uint32_t code{0};
    /// Damage taken only: the attacker's NPC code, 0 when its spawn was not seen.
    uint32_t source_npc{0};
    uint64_t amount{0};
    uint64_t hits{0};
    uint64_t max{0};

    [[nodiscard]] bool operator==(const SkillAmount&) const = default;
};

/// One player's line in a fight: their damage and their summons'.
struct CombatantTotals {
    EntityId id{0};
    std::string name;
    AionClass cls{AionClass::Unknown};
    uint32_t class_code{0};
    bool is_local{false};
    bool in_party{false};
    /// Direct hits and DoT ticks.
    uint64_t damage{0};
    uint64_t dot_damage{0};
    uint64_t max_hit{0};
    HitCounts hits;
    double dps{0.0};
    /// Of the fight's total, 0..1.
    double share{0.0};

    /// Most damage first.
    std::vector<SkillTotals> skills;

    /// Heal ticks, and 04 38 records from this player to one on the player side.
    /// The total, as the game's Combat Analysis counts it: no overheal split.
    uint64_t healing{0};
    uint64_t heal_hits{0};
    double hps{0.0};
    /// Most healing first.
    std::vector<SkillAmount> heal_skills;

    /// Hits and ticks from enemies landing on this player.
    uint64_t damage_taken{0};
    /// The direct hits among them, with their crits, modifiers and directions.
    HitCounts taken;
    /// By attacking skill and NPC, most damage first.
    std::vector<SkillAmount> taken_by;

    [[nodiscard]] double crit_rate() const noexcept { return hits.rate(&HitCounts::crits); }
    [[nodiscard]] double back_rate() const noexcept { return hits.rate(&HitCounts::back); }
};

/// The enemy a fight is fought against: the damaged one with the most max HP.
struct BossSummary {
    EntityId id{0};
    uint32_t npc_code{0};
    std::string label;
    uint64_t max_hp{0};
    /// Meaningful when hp_known: the server's own figure when hp_exact, else spawn
    /// HP less the damage seen since.
    uint64_t hp{0};
    bool hp_known{false};
    bool hp_exact{false};
    /// Max HP reaches boss_min_max_hp: the fight counts as a boss fight.
    bool is_boss{false};
    bool killed{false};
    /// Damage seen landing on it in the fight, from anyone.
    uint64_t damage_taken{0};

    [[nodiscard]] bool present() const noexcept { return id != 0; }
    [[nodiscard]] double hp_fraction() const noexcept {
        return !hp_known || max_hp == 0 ? 0.0 : static_cast<double>(hp) / static_cast<double>(max_hp);
    }
};

/// What the capture lost during a fight. A world boss or a lossy capture makes the
/// numbers partial, and the record says so rather than looking complete.
struct CaptureQuality {
    uint64_t gap_bytes{0};
    uint64_t resyncs{0};
    uint64_t bundle_errors{0};
    uint64_t dropped_packets{0};
    /// The boss died having lost much of its HP with no hits seen: the server did
    /// not send everyone's hits (crowded world bosses do this).
    bool partial{false};

    [[nodiscard]] bool clean() const noexcept {
        return gap_bytes == 0 && resyncs == 0 && bundle_errors == 0 && dropped_packets == 0 && !partial;
    }
};

/// A fight, live or finished. The overlay draws one; the history stores them.
struct FightSummary {
    uint64_t id{0};
    uint32_t map_id{0};
    /// When the map visit this fight belongs to began: its map load, or the first
    /// fight since the meter started. The history groups and numbers fights by it.
    Micros visit_ts{0};
    Micros start_ts{0};
    /// The last landed hit: idle time after it never dilutes DPS.
    Micros end_ts{0};
    double duration_s{0.0};
    bool live{false};
    EndReason end_reason{EndReason::None};
    BossSummary boss;
    /// Most damage first.
    std::vector<CombatantTotals> combatants;
    uint64_t total_damage{0};
    double total_dps{0.0};
    uint64_t total_healing{0};
    CaptureQuality quality;
};

} // namespace aion2::meter
