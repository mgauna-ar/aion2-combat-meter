#pragma once

#include "common/micros.hpp"
#include "protocol/events.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace aion2::auras {

using EntityId = proto::EntityId;

/**
 * @brief Who is who, as far as the auras need it: the local player, whose summons
 * are whose, which ids are enemies, and which enemy the local player is fighting.
 *
 * The plugin's own record. Plugins never read each other, so the combat meter's
 * registry is out of reach, and the auras need far less of it: no names, no sides
 * inferred from hits, no party.
 *
 * - The local player is the id of the last own identity (33 36), or of the last own
 *   values (00 8D), unless that id is a spawn: the registry's rule.
 * - An enemy is a spawn that is not a player object (spawn_kind::is_player_object).
 *   A mob already in view when the meter started never spawned for it, so its
 *   debuffs show as no one's until it comes into view again.
 * - Ids belong to the map: a new map forgets every spawn, and the players named on
 *   the map before. A reload within a run (the same map over the same connection)
 *   keeps the spawns, as the combat meter does.
 */
class Entities {
public:
    /// How long a direct hit keeps an enemy a candidate for target().
    static constexpr Micros kTargetMemory = 10 * kMicrosPerSecond;
    /// Most spawns kept; a misread flood stops here.
    static constexpr size_t kMaxSpawns = 4096;

    void on_identity(const proto::Identity& identity);
    void on_spawn(const proto::Spawn& spawn);
    /// 00 8D: the local player's own values, and anyone's HP.
    void on_values(const proto::RemainingHp& values);
    /// A direct hit: the local player's or their summons' on an enemy makes it a
    /// candidate for target().
    void on_damage(const proto::Damage& damage, Micros ts);
    /// HP 0 (00 8D): it stays known, but is no target.
    void on_dead(EntityId id);
    /// 42 36: the corpse is gone, and its id free for someone else.
    void on_gone(EntityId id);
    /// `reload`: the same map over the same connection, a teleport or a revive.
    void on_map_load(bool reload);

    /// The local player's id on this map; 0 before any own identity or own values.
    [[nodiscard]] EntityId local() const noexcept { return m_local; }
    /// The local character, "name@server", from the last own identity; empty before.
    [[nodiscard]] const std::string& local_character() const noexcept { return m_character; }
    /// A summon's owner, a summon's summon's too; any other id is itself.
    [[nodiscard]] EntityId resolve(EntityId id) const;
    /// A spawned monster, or something it owns.
    [[nodiscard]] bool is_enemy(EntityId id) const;
    /// Put there by the local player, or by their summons.
    [[nodiscard]] bool mine(EntityId caster) const { return m_local != 0 && resolve(caster) == m_local; }

    /// The enemy the local player is fighting at `now`: of those they or their
    /// summons hit directly in the last kTargetMemory and that still stand, the one
    /// with the most max HP, the latest hit on a tie; 0 for none. In a boss fight
    /// with adds, the boss.
    [[nodiscard]] EntityId target(Micros now) const;
    /// What an enemy is called: its npc code's name, "NPC #code" for one the table
    /// does not list, "#id" for one with no code.
    [[nodiscard]] std::string label(EntityId id) const;

private:
    struct Spawned {
        uint8_t kind{0};
        uint32_t npc_code{0};
        /// The spawn's max HP, or the most 00 8D HP seen since.
        uint64_t max_hp{0};
        EntityId owner{0};
        /// A boss mechanic aimed at a player (Spawn::aimed_at): never the target.
        bool aimed{false};
        bool dead{false};
        /// The local player's or their summons' last direct hit on it; 0 for none.
        Micros last_hit{0};
    };

    std::unordered_map<EntityId, Spawned> m_spawns;
    /// Ids an identity named on this map: a spawn for one is that player coming
    /// into view, not an NPC.
    std::unordered_set<EntityId> m_players;
    EntityId m_local{0};
    std::string m_character;
};

} // namespace aion2::auras
