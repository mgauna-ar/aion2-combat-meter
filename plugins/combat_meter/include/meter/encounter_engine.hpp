#pragma once

#include "common/micros.hpp"
#include "game/plugin.hpp"
#include "meter/combatant_registry.hpp"
#include "meter/death_log.hpp"
#include "meter/effect_uptime.hpp"
#include "meter/fight_detail.hpp"
#include "meter/meter_settings.hpp"
#include "meter/timeline.hpp"
#include "meter/types.hpp"
#include "protocol/events.hpp"

#include <cstdint>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
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
class EncounterEngine final : public game::Plugin {
public:
    /// The longest gap between a player's own direct hits that still counts as time
    /// spent hitting (CombatantTotals::active_s). In the second capture's last boss
    /// fight, 99% of every player's gaps were 2.7 s or less.
    static constexpr Micros kActiveGap = 3 * kMicrosPerSecond;
    /// Most enemies whose HP one fight follows, for the boss's line.
    static constexpr size_t kMaxHpSeries = 256;
    /// Most players one fight keeps a recap ring for.
    static constexpr size_t kMaxRecapRings = 256;

    explicit EncounterEngine(MeterSettings settings = {});

    void set_settings(const MeterSettings& settings) noexcept { m_settings = settings; }
    [[nodiscard]] const MeterSettings& settings() const noexcept { return m_settings; }

    void on_event(const proto::ParsedEvent& event) override;
    /// The capture's running totals (gaps, resyncs, bundle errors, drops). A fight
    /// records how much they grew while it ran.
    void set_capture_counters(const CaptureQuality& totals) noexcept { m_counters = totals; }
    /// Ends a fight whose time is up. Call it as capture time passes, quiet or not.
    void tick(Micros now) override;
    /// Ends the live fight now, if there is one.
    void end_fight(EndReason reason, Micros now);
    /// Forgets every effect it knew of: switched off, it will miss what happens
    /// meanwhile.
    void forget_effects() noexcept { m_effects.clear(); }

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
        /// By game::skill_key().
        std::unordered_map<uint32_t, SkillTotals> skills;
        uint64_t healing{0};
        uint64_t heal_hits{0};
        std::unordered_map<uint32_t, SkillAmount> heals;
        uint64_t taken{0};
        HitCounts taken_hits;
        /// By game::skill_key() << 32 | the attacker's NPC code.
        std::unordered_map<uint64_t, SkillAmount> taken_by;
        /// Second by second, from the fight's start.
        std::vector<TimelineBin> bins;
        /// The gaps of kActiveGap or less between their own direct hits.
        Micros active_us{0};

        void merge(const Accum& other);
    };

    /// A row: an id, and the character it was when it hit. The server gives a
    /// player a new id on each map and hands old ones on, so an id can be two
    /// players in turn; the summary joins a character's rows.
    struct RowKey {
        EntityId id{0};
        CharacterId who{0};
        bool operator==(const RowKey&) const = default;
    };
    struct RowKeyHash {
        size_t operator()(const RowKey& k) const noexcept {
            return std::hash<uint64_t>{}((static_cast<uint64_t>(k.who) << 32) | k.id);
        }
    };

    struct Fight {
        uint64_t id{0};
        uint32_t map_id{0};
        Micros visit_ts{0};
        Micros start_ts{0};
        Micros last_hit_ts{0};
        /// By the player a hit resolves to, and their character then.
        std::unordered_map<RowKey, Accum, RowKeyHash> players;
        /// Damage each enemy took from the player side.
        std::unordered_map<EntityId, uint64_t> enemies;
        CaptureQuality counters_at_start;
        bool boss_killed{false};
        Micros kill_ts{0};
        /// Each player's last own direct hit, by id: active time bridges the gap.
        std::unordered_map<EntityId, Micros> last_direct;
        /// The server's HP figures for each enemy, the last one of each second.
        struct HpSample {
            uint32_t second{0};
            uint64_t hp{0};
        };
        std::unordered_map<EntityId, std::vector<HpSample>> enemy_hp;
        /// What landed on each party member, for the recap of a death.
        std::unordered_map<EntityId, RecapRing> recaps;
        struct Death {
            RowKey key;
            Micros ts{0};
            /// When HP above 0, or their own hit, said they were up; 0 while down.
            Micros up_ts{0};
            std::vector<RecapEvent> recap;
        };
        std::vector<Death> deaths;
    };

    void on_spawn(const proto::Spawn& s, Micros ts);
    void on_damage(const proto::Damage& d, Micros ts);
    void on_tick(const proto::Tick& t, Micros ts);
    /// 00 8D: the local player's id, and HP, whose 0 is a death.
    void on_values(const proto::RemainingHp& v, Micros ts);
    void on_death(EntityId id, Micros ts);
    void on_map(const proto::MapLoad& m, uint32_t connection, Micros ts);
    /// A hit from the player side on an enemy, credited to its resolved player.
    void credit(EntityId actor, EntityId target, uint64_t amount, uint32_t skill, const proto::Damage* direct,
                Micros ts);
    /// A heal tick, or a 04 38 `record` from a player to the player side, during a
    /// fight: healing for the resolved healer. Never starts a fight.
    void book_heal(EntityId actor, EntityId target, uint64_t amount, uint32_t skill, bool record, Micros ts);
    /// An enemy's hit or tick on a player during a fight: damage that player took.
    void book_taken(EntityId actor, EntityId target, uint64_t amount, uint32_t skill, const proto::Damage* direct,
                    Micros ts);
    /// An enemy's HP figure `hp` at `ts`, `before` the one the meter had: the boss's
    /// line. The first in a fight also stands at its start.
    void sample_hp(EntityId id, uint64_t hp, std::optional<uint64_t> before, Micros ts);
    /// The exact HP figure the meter has for `id`: its last 00 8D, else its spawn's.
    [[nodiscard]] std::optional<uint64_t> exact_hp(EntityId id) const;
    /// A party member's HP reached 0 during a fight: a death, with its recap.
    void record_death(EntityId id, Micros ts);
    /// HP above 0 or their own hit: a death still open for `id` is over.
    void note_alive(EntityId id, Micros ts);
    void push_recap(EntityId target, const RecapSample& sample);
    /// What an effect on `target` from `caster` is to the fight, or nothing for one
    /// it does not track: on a party member (not a summon), or a party member's on an
    /// enemy.
    [[nodiscard]] std::optional<EffectTag> effect_tag(EntityId target, EntityId caster) const;
    /// The fight's detail (timeline, deaths, effects) for its summary `s`, whose rows
    /// are `row_ids` (by group_key) with their seconds in `rows`.
    [[nodiscard]] std::shared_ptr<const FightDetail> build_detail(const Fight& fight, const FightSummary& s,
                                                                  const std::unordered_map<uint64_t, EntityId>& row_ids,
                                                                  std::vector<TimelineRow> rows) const;
    /// A player's name and class as a summary shows them.
    [[nodiscard]] std::pair<std::string, AionClass> player_label(EntityId id, CharacterId who) const;
    /// The key summarize groups a player's rows by: their character, else the id.
    [[nodiscard]] uint64_t group_key(EntityId id, CharacterId who) const;
    /// A player whose healing and damage taken a live fight books: a party member by
    /// the roster, the local player, or with no roster one already in the fight,
    /// so strangers healing each other nearby stay out.
    [[nodiscard]] bool counts_in_fight(EntityId player) const;
    [[nodiscard]] RowKey row_key(EntityId player) const { return RowKey{player, m_registry.character_of(player)}; }
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
    /// The connection the current map loaded on (Frame::connection); 0 for none.
    uint32_t m_map_connection{0};
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
    /// Every effect in view, and the live fight's spans of them.
    EffectUptime m_effects;
    std::vector<FightSummary> m_finished;
    std::deque<FightSummary> m_recent;
    uint64_t m_recent_revision{0};
};

} // namespace aion2::meter
