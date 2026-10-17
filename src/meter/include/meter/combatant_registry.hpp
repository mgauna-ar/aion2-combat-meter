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

/// A character: a name on a server. 1-based; 0 is none. The server gives a player
/// a new id on each map and hands old ids out again, so fights are kept by
/// character, not by id.
using CharacterId = uint32_t;

struct Character {
    std::string name;
    uint16_t server{0};
    uint32_t class_code{0};
    AionClass cls{AionClass::Unknown};
    /// The id it was last named on.
    EntityId id{0};
};

/// Everything known about one entity id.
struct Actor {
    EntityId id{0};
    /// The character this id is now; 0 while no identity has named it.
    CharacterId character{0};
    /// From the skills it used: the class until the id is named, and the
    /// character's when its identity has none.
    AionClass cls{AionClass::Unknown};
    /// Named by an identity packet.
    bool is_player{false};
    bool is_local{false};
    /// A player from before the last map load. The server may have handed the id
    /// to someone else since, so a spawn or another name for it wins.
    bool stale{false};
    /// Seen in a spawn packet.
    bool spawned{false};
    uint8_t spawn_kind{0};
    uint32_t npc_code{0};
    /// A boss mechanic aimed at a player (Spawn::aimed_at), such as a trap the party
    /// breaks. Hits on it are no damage: the game's own window leaves them out.
    bool aimed_mechanic{false};
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
 * an earlier inference. A summon's spawn names its owner, who is then a player too.
 *
 * Names belong to characters, and an id is one character at a time: the last
 * identity for it says which. Ids are per map, and the server hands them out
 * again, so a map load leaves players stale rather than dropping them (their
 * names may have come before it). A stale id gives way to a spawn, and a
 * character named on a new id leaves its stale one. Ids are never joined to each
 * other: two players can never share a row.
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
    /// `player` used a skill of class `cls` (class_of_skill): their class, when no
    /// identity has named it. Summons use their owner's class digits.
    void on_skill_class(EntityId player, AionClass cls);
    /// A map load, the same map again included (a new run of a dungeon): NPCs,
    /// summons and inferred sides go; players stay, stale, since a new instance has
    /// ids of its own. `instance` (proto::MapLoad::is_instance) says who counts with
    /// no roster.
    void on_map_change(bool instance);

    /// The id a hit by `id` is credited to: the owner of a summon, through chains
    /// of them.
    [[nodiscard]] EntityId resolve(EntityId id) const;
    [[nodiscard]] Side side(EntityId id) const;
    /// Side, with the folded-range guess for an id nothing else settled.
    [[nodiscard]] Side side_or_guess(EntityId id) const;

    [[nodiscard]] const Actor* find(EntityId id) const;
    /// The character `id` is now; 0 for none.
    [[nodiscard]] CharacterId character_of(EntityId id) const;
    [[nodiscard]] const Character* character(CharacterId who) const;
    [[nodiscard]] EntityId local_player() const noexcept { return m_local; }
    /// The local player's character, kept while their id changes.
    [[nodiscard]] CharacterId local_character() const noexcept { return m_local_character; }
    /// The roster has been seen: party membership is known rather than assumed.
    [[nodiscard]] bool has_roster() const noexcept { return !m_roster.empty(); }
    /// In the party by the roster; the local player always. With no roster, every
    /// player in an instance is, since only the group gets in; in the open world,
    /// or on a map not seen loading, only the local player is.
    [[nodiscard]] bool in_party(EntityId id) const;
    /// The same for a character, whatever id it is on now.
    [[nodiscard]] bool character_in_party(CharacterId who) const;
    /// The class `id` has shown: its character's, else its skills'.
    [[nodiscard]] AionClass class_of(EntityId id) const;
    /// The name; "You" for the local player whose name has not been seen; "#id"
    /// for anyone else whose name is not known.
    [[nodiscard]] std::string display_name(EntityId id) const;
    [[nodiscard]] size_t actor_count() const noexcept { return m_actors.size(); }
    [[nodiscard]] size_t pending_edges() const noexcept { return m_pending.size(); }

private:
    Actor& get_or_create(EntityId id);
    [[nodiscard]] Actor* find_mut(EntityId id);
    /// The character with this name and server, made on first sight.
    CharacterId character_for(const proto::Identity& identity);
    /// Names `a` as `who`. The character's stale id from an earlier map goes.
    void bind(Actor& a, CharacterId who);
    /// Forgets everything about `id`: the server has given it to someone else.
    void forget(EntityId id);
    /// Whether a scanned identity, which may have misread its id, can name it.
    [[nodiscard]] bool scan_can_name(const proto::Identity& identity) const;
    /// Makes `id` the local player. Their name moves with them from their last id.
    void set_local(EntityId id);
    void set_inferred(EntityId id, Side side);
    /// Spawned, and on the enemy side: an NPC for certain, not by inference.
    [[nodiscard]] bool known_npc(EntityId id) const;
    /// Settles `id` as a player over any earlier inference; identity and spawn
    /// packets still win.
    void make_player(EntityId id);
    /// Settles held hits that touch a newly known id, and what that settles in turn.
    void settle_pending(EntityId known);
    void mark_players_stale();

    std::unordered_map<EntityId, Actor> m_actors;
    /// Index + 1 is the CharacterId. Never shrinks: names outlive maps.
    std::vector<Character> m_characters;
    /// "name@server" to its character.
    std::unordered_map<std::string, CharacterId> m_by_name;
    std::unordered_set<std::string> m_roster;
    EntityId m_local{0};
    CharacterId m_local_character{0};
    bool m_in_instance{false};
    std::deque<std::pair<EntityId, EntityId>> m_pending;
};

} // namespace aion2::meter
