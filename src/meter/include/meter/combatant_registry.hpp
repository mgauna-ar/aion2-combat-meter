#pragma once

#include "common/micros.hpp"
#include "meter/types.hpp"
#include "protocol/events.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace aion2::meter {

/// Everything known about one entity id.
struct Actor {
    EntityId id{0};
    std::string name;
    uint16_t server{0};
    uint32_t class_code{0};
    AionClass cls{AionClass::Unknown};
    /// Named by an identity packet.
    bool is_player{false};
    bool is_local{false};
    /// Seen in a spawn packet.
    bool spawned{false};
    uint8_t spawn_kind{0};
    uint32_t npc_code{0};
    /// The player a summon or effect belongs to; 0 for none.
    EntityId owner{0};
    bool has_hp{false};
    uint64_t spawn_hp{0};
    /// From the spawn; else the most live HP seen, a lower bound.
    uint64_t max_hp{0};
    /// The server's own figure (00 8D): exact, where spawn HP minus damage seen is
    /// an estimate.
    bool has_live_hp{false};
    uint64_t live_hp{0};
    bool dead{false};
    /// Learned only from which side it trades damage with.
    Side inferred_side{Side::Unknown};
};

/**
 * @brief Who every entity id is, and which side it fights on.
 *
 * Sides come from what is known first: identity packets make players, spawn
 * packets make NPCs or player summons. An id the meter has never been told about
 * (it started mid-session, after the loading screen that carried the names) gets
 * its side from the hits it trades: PvE damage only crosses sides, so the other
 * end of a hit with a known side settles it. Hits between two unknowns are held
 * (up to kMaxPendingEdges) and settled when either end becomes known. With no
 * anchor at all, an id in the folded spawn range is taken as an enemy. 04 38 also
 * carries heals, so a hit with a spawned NPC, which only a player trades, overrides
 * an earlier inference.
 *
 * Names outlive map changes, since they arrive only on loading screens; NPCs and
 * summons are per map and are dropped on one.
 */
class CombatantRegistry {
public:
    static constexpr size_t kMaxPendingEdges = 4096;
    /// Spawn ids fold into [kFoldedLow, kFoldedHigh) (protocol/entity_id.hpp).
    static constexpr EntityId kFoldedLow = 0x4000;
    static constexpr EntityId kFoldedHigh = 0x8000;

    void on_identity(const proto::Identity& identity);
    void on_spawn(const proto::Spawn& spawn);
    void on_death(EntityId id);
    void on_revive(EntityId id);
    void on_party(const proto::PartyRoster& roster);
    /// 00 8D for the local player's own values: `id` is the local player, even if
    /// the 33 36 that would have named them came before the meter started.
    void on_self_values(EntityId id);
    /// 00 8D with someone's current HP.
    void on_hp(EntityId id, uint64_t hp);
    /// A hit from `actor` to `target`: settles an unknown side from a known one.
    void observe_hit(EntityId actor, EntityId target);
    /// A new map: NPCs, summons and inferred sides go; players and names stay.
    void on_map_change();

    /// The id a hit by `id` is credited to: the owner of a summon (through chains of
    /// them), the first id of a player seen under several.
    [[nodiscard]] EntityId resolve(EntityId id) const;
    [[nodiscard]] Side side(EntityId id) const;
    /// Side, with the folded-range guess for an id nothing else settled.
    [[nodiscard]] Side side_or_guess(EntityId id) const;

    [[nodiscard]] const Actor* find(EntityId id) const;
    [[nodiscard]] EntityId local_player() const noexcept { return m_local; }
    /// The roster has been seen: party membership is known rather than assumed.
    [[nodiscard]] bool has_roster() const noexcept { return !m_roster.empty(); }
    /// In the party by the roster; the local player always. With no roster, every
    /// player is.
    [[nodiscard]] bool in_party(EntityId id) const;
    /// The name; "You" for the local player whose name has not been seen; "#id"
    /// for anyone else whose name is not known.
    [[nodiscard]] std::string display_name(EntityId id) const;
    [[nodiscard]] size_t actor_count() const noexcept { return m_actors.size(); }
    [[nodiscard]] size_t pending_edges() const noexcept { return m_pending.size(); }

private:
    Actor& get_or_create(EntityId id);
    /// Makes `canonical` (and `id`, which an alias may map to it) the local player.
    void set_local(EntityId id, EntityId canonical);
    void set_inferred(EntityId id, Side side);
    /// Spawned, and on the enemy side: an NPC for certain, not by inference.
    [[nodiscard]] bool known_npc(EntityId id) const;
    /// Settles `id` as a player over any earlier inference; identity and spawn
    /// packets still win.
    void make_player(EntityId id);
    /// Settles held hits that touch a newly known id, and what that settles in turn.
    void settle_pending(EntityId known);

    std::unordered_map<EntityId, Actor> m_actors;
    /// Second and later ids of a player, to the first.
    std::unordered_map<EntityId, EntityId> m_alias;
    /// "name@server" to the first id seen with it.
    std::unordered_map<std::string, EntityId> m_by_name;
    std::unordered_set<std::string> m_roster;
    EntityId m_local{0};
    std::deque<std::pair<EntityId, EntityId>> m_pending;
};

} // namespace aion2::meter
