#pragma once

#include "common/micros.hpp"
#include "meter/combatant_registry.hpp"
#include "meter/meter_settings.hpp"
#include "meter/types.hpp"
#include "protocol/events.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

namespace aion2::meter {

/**
 * @brief Events in, fights out.
 *
 * A fight starts on a direct hit that lands from the player side on an enemy; a
 * DoT tick never starts one. It ends on the boss's death (after kill_grace_s, so
 * the last ticks count), a map change, an idle stretch with no party hit, a
 * capture gone quiet, a wipe (if enabled) or by hand. Its duration runs from the
 * first hit to the last, so time spent idle before it is called never dilutes DPS.
 *
 * Runs on one thread (the engine thread, or the replay tool's only one) on the
 * capture clock: every time it sees comes from an event or a tick, never the
 * system clock, so a replay computes what the live meter did.
 */
class EncounterEngine {
public:
    explicit EncounterEngine(MeterSettings settings = {});

    void set_settings(const MeterSettings& settings) noexcept { m_settings = settings; }
    [[nodiscard]] const MeterSettings& settings() const noexcept { return m_settings; }

    void on_event(const proto::ParsedEvent& event);
    /// The capture's running totals (gaps, resyncs, bundle errors, drops). A fight
    /// records how much they grew while it ran.
    void set_capture_counters(const CaptureQuality& totals) noexcept { m_counters = totals; }
    /// Ends a fight whose time is up. Call it as capture time passes, quiet or not.
    void tick(Micros now);
    /// Ends the live fight now, if there is one.
    void end_fight(EndReason reason, Micros now);

    [[nodiscard]] bool in_fight() const noexcept { return m_fight.has_value(); }
    /// The live fight, or else the newest finished one; id 0 before any.
    [[nodiscard]] FightSummary current() const;
    /// Fights finished since the last call, oldest first: what to save.
    [[nodiscard]] std::vector<FightSummary> take_finished();
    /// The last memory_history finished fights, oldest first.
    [[nodiscard]] const std::deque<FightSummary>& recent() const noexcept { return m_recent; }
    /// Moves whenever recent() changes.
    [[nodiscard]] uint64_t recent_revision() const noexcept { return m_recent_revision; }

    [[nodiscard]] const CombatantRegistry& registry() const noexcept { return m_registry; }
    [[nodiscard]] uint32_t map_id() const noexcept { return m_map_id; }
    [[nodiscard]] Micros last_event_ts() const noexcept { return m_last_event_ts; }
    [[nodiscard]] uint64_t last_server_ms() const noexcept { return m_last_server_ms; }

private:
    struct Accum {
        uint64_t damage{0};
        uint64_t dot_damage{0};
        uint64_t max_hit{0};
        HitCounts hits;
        /// By skill_key().
        std::unordered_map<uint32_t, SkillTotals> skills;
        uint64_t healing{0};
        uint64_t heal_hits{0};
        std::unordered_map<uint32_t, SkillAmount> heals;
        uint64_t taken{0};
        HitCounts taken_hits;
        /// By skill_key() << 32 | the attacker's NPC code.
        std::unordered_map<uint64_t, SkillAmount> taken_by;

        void merge(const Accum& other);
    };

    struct Fight {
        uint64_t id{0};
        uint32_t map_id{0};
        Micros visit_ts{0};
        Micros start_ts{0};
        Micros last_hit_ts{0};
        /// By the player a hit resolves to.
        std::unordered_map<EntityId, Accum> players;
        /// Damage each enemy took from the player side.
        std::unordered_map<EntityId, uint64_t> enemies;
        CaptureQuality counters_at_start;
        bool boss_killed{false};
        Micros kill_ts{0};
    };

    void on_spawn(const proto::Spawn& s);
    void on_damage(const proto::Damage& d, Micros ts);
    void on_tick(const proto::Tick& t, Micros ts);
    /// 00 8D: the local player's id, and HP, whose 0 is a death.
    void on_values(const proto::RemainingHp& v, Micros ts);
    void on_death(EntityId id, Micros ts);
    void on_map(const proto::MapLoad& m, Micros ts);
    /// A hit from the player side on an enemy, credited to its resolved player.
    void credit(EntityId actor, EntityId target, uint64_t amount, uint32_t skill, const proto::Damage* direct,
                Micros ts);
    /// A heal tick, or a 04 38 `record` from a player to the player side, during a
    /// fight: healing for the resolved healer. Never starts a fight.
    void book_heal(EntityId actor, EntityId target, uint64_t amount, uint32_t skill, bool record);
    /// An enemy's hit or tick on a player during a fight: damage that player took.
    void book_taken(EntityId actor, EntityId target, uint64_t amount, uint32_t skill, const proto::Damage* direct);
    /// A player whose healing and damage taken a live fight books: a party member by
    /// the roster, the local player, or with no roster one already in the fight,
    /// so strangers healing each other nearby stay out.
    [[nodiscard]] bool counts_in_fight(EntityId player) const;
    /// Ends the fight if its time is up at `now`.
    void check_end(Micros now);
    void finish(EndReason reason, Micros now);
    [[nodiscard]] BossSummary boss_of(const Fight& fight) const;
    [[nodiscard]] FightSummary summarize(const Fight& fight, bool live, EndReason reason) const;
    [[nodiscard]] bool party_wiped(const Fight& fight) const;

    MeterSettings m_settings;
    CombatantRegistry m_registry;
    std::optional<Fight> m_fight;
    uint64_t m_next_fight_id{1};
    uint32_t m_map_id{0};
    /// The current map visit's start; 0 until a map load or the first fight.
    Micros m_visit_ts{0};
    Micros m_last_event_ts{0};
    uint64_t m_last_server_ms{0};
    CaptureQuality m_counters;
    /// Damage seen landing on each entity since its spawn, from anyone: the boss's
    /// HP estimate subtracts it from the spawn HP.
    std::unordered_map<EntityId, uint64_t> m_damage_since_spawn;
    /// What the server's HP figures (00 8D) say an entity lost, and how much of it
    /// came with no hit seen since the figure before. Those amounts are in HP, which
    /// falls by a steady multiple of the damage records' numbers, so the share that
    /// came with hits is the one comparison that holds without knowing the multiple.
    struct HpLoss {
        bool known{false};
        uint64_t last{0};
        bool hit_since{false};
        uint64_t lost{0};
        uint64_t lost_unseen{0};

        /// The server's figure is now `hp`.
        void observe(uint64_t hp) noexcept {
            if (known && hp < last) {
                lost += last - hp;
                if (!hit_since) lost_unseen += last - hp;
            }
            known = true;
            last = hp;
            hit_since = false;
        }
    };
    std::unordered_map<EntityId, HpLoss> m_hp_loss;
    /// A hit (or heal) on `target` was seen: the next HP change is accounted for.
    void note_hit_on(EntityId target);
    std::vector<FightSummary> m_finished;
    std::deque<FightSummary> m_recent;
    uint64_t m_recent_revision{0};
};

} // namespace aion2::meter
