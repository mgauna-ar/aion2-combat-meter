#pragma once

#include "common/micros.hpp"
#include "game/classes.hpp"
#include "protocol/events.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aion2::combat_power {

using EntityId = proto::EntityId;

/// One player and their combat power.
struct PowerRow {
    EntityId id{0};
    std::string name;
    uint16_t server{0};
    /// From the skills they were seen using; Unknown until one is.
    game::AionClass cls{game::AionClass::Unknown};
    /// 0 when their record carried none.
    uint32_t combat_power{0};
    /// How far they are from the local player, in metres; negative while either's
    /// position is not known. As fresh as PowerSnapshot::local_position_age says.
    float distance_m{-1.0f};

    /// An enemy (PowerTracker, An enemy): of the other faction, outside a dungeon and
    /// not in the local player's party. False while either's faction is not known.
    bool hostile{false};
    /// Their HP reached 0 (00 8D) and has not risen since: no threat, whoever they
    /// are. They leave view (47 36) a second or two later.
    bool dead{false};

    [[nodiscard]] bool has_distance() const noexcept { return distance_m >= 0.0f; }

    bool operator==(const PowerRow&) const = default;
};

/// What the combat power overlay draws.
struct PowerSnapshot {
    /// The player the local player last used a skill on; `target.id` 0 for none.
    PowerRow target;
    /// The other players on the map, the enemies within reach first, each group
    /// strongest first: all of an arena's opponents or a dungeon's party, and of a
    /// crowd the first PowerTracker::kMaxListed, so no crowd of allies hides an enemy.
    std::vector<PowerRow> players;
    /// The same players, the enemies within reach first, each group nearest first,
    /// those at no known distance last: of a crowd, the first PowerTracker::kMaxListed. Empty until the local player's
    /// position is known.
    std::vector<PowerRow> nearest;
    /// How many there are, listed or not, and how many of them are enemies within
    /// PowerTracker::kEnemyRange and alive: the ones that lead the lists.
    uint32_t in_view{0};
    uint32_t hostile_in_view{0};
    uint32_t map_id{0};
    /// Whether the local player's position is known, and how long ago it was last
    /// said: the game does not send them their own steps, so every distance is from
    /// where they last acted (PowerTracker, Distance).
    bool has_local_position{false};
    Micros local_position_age{0};

    bool operator==(const PowerSnapshot&) const = default;
};

/**
 * @brief Other players' combat power, as the game sends it when they come into view.
 *
 * The plugin's own record of who is on the map: plugins never read each other.
 *
 * - **A player** is an identity read from 45 36, with a name. Its combat power is
 *   the record's (proto::read_combat_power), kept as sent: nothing is looked up
 *   anywhere, and nothing is worked out from gear.
 * - **The local player** is the id of the last own identity (33 36) or own values
 *   (00 8D), as the other plugins take it. Their own combat power is not known:
 *   their record does not end with it.
 * - **The target** is the last other player a skill the local player pressed named
 *   (02 38, mode 2), or one they hit (04 38), for kTargetMemory. A skill pressed on
 *   anything else, a monster, lets go of it. An ally they heal is a target too.
 * - **An enemy** is a player of the other faction than the local player's
 *   (Identity::faction, sent with their record as they come into view, so before
 *   they do anything), but for two cases where the factions play together: no one
 *   is an enemy in a dungeon (game::is_dungeon), whose parties mix both, and a
 *   member of the local player's party (02 97) never is. A player of the local
 *   player's own faction is never one, an arena's opponent included.
 * - **An enemy leads the lists while within kEnemyRange,** or at no known
 *   distance, and alive. The range stays although players now leave the list: the
 *   local player's position can be stale, and an enemy at the edge of view is no
 *   one about to engage. They stay marked either way.
 * - **Who is in view:** a player leaves the list with their 47 36 (they walked out
 *   of range, or died and were taken away), and comes back with their next 45 36.
 *   Dead (their HP, 00 8D, at 0) they stay listed until then, but lead nothing.
 * - **Distance** is between a player's last position and the local player's, in
 *   metres (the game's units are centimetres). Other players' positions come all
 *   the time: their record as they come into view, every step they take. The local
 *   player's own never do, since the server does not send them their own steps: it
 *   is known from their record on entering the map, from each effect they cast
 *   (which carries the caster's position) and from a skill that moves them. So it
 *   is exact while they fight, and where they last acted while they only walk.
 * - **Ids belong to a map.** A newer connection (entering or leaving a dungeon or an
 *   arena opens one) forgets everyone; so does another map over the same connection,
 *   but for those named in the kEarlyArrival before it, since a map's players can
 *   come ahead of its 21 36. The same map over the same connection is a reload, and
 *   keeps them.
 */
class PowerTracker {
public:
    /// How long the last skill used on a player keeps them the target.
    static constexpr Micros kTargetMemory = 30 * kMicrosPerSecond;
    /// How long before a map's 21 36 its players may be named.
    static constexpr Micros kEarlyArrival = 2 * kMicrosPerSecond;
    /// Most players a snapshot lists: of a crowd, the strongest.
    static constexpr size_t kMaxListed = 12;
    /// How far an enemy may be and still lead the lists, in metres: about twice as
    /// far as the farthest a player's record first came in the fourth capture.
    static constexpr float kEnemyRange = 200.0f;
    /// Most players remembered; a misread flood stops here.
    static constexpr size_t kMaxPlayers = 2048;

    void on_identity(const proto::Identity& identity, uint32_t connection, Micros ts);
    /// 00 8D: the local player's own values name them when 33 36 was missed; anyone
    /// else's HP says whether they are dead.
    void on_values(const proto::RemainingHp& values, uint32_t connection);
    void on_cast(const proto::SkillCast& cast, Micros ts);
    void on_damage(const proto::Damage& damage, Micros ts);
    void on_map_load(const proto::MapLoad& map, uint32_t connection, Micros ts);
    /// A step, or a skill that moved someone: a listed player's, or the local player's.
    void on_position(const proto::Position& position, uint32_t connection, Micros ts);
    /// An effect put on or renewed: it says where its caster stands.
    void on_effect(const proto::BuffApplied& effect, uint32_t connection, Micros ts);
    /// 47 36: a listed player left view, and the list.
    void on_left_view(const proto::LeftView& left, uint32_t connection);
    /// 02 97: who the local player's party is now.
    void on_party(const proto::PartyRoster& roster);
    /// Forgets everything: the plugin was switched off.
    void clear();

    [[nodiscard]] EntityId local() const noexcept { return m_local; }
    [[nodiscard]] size_t players() const noexcept { return m_players.size(); }
    [[nodiscard]] PowerSnapshot snapshot(Micros now) const;

private:
    struct Player {
        PowerRow row;
        /// Capture time of the identity that named them last.
        Micros seen{0};
        bool has_position{false};
        proto::Point at;
        /// Identity::faction; 0 for unknown.
        uint8_t faction{0};
    };

    /// A newer connection forgets the map before; false for an event from an older
    /// one, which is ignored.
    [[nodiscard]] bool follow(uint32_t connection);
    void learn_class(EntityId id, uint32_t skill);
    void aim(EntityId target, Micros ts);
    [[nodiscard]] bool hostile(const Player& player) const;
    /// A hostile player near enough to lead the lists.
    [[nodiscard]] bool threat(const Player& player) const;
    /// `id` stands at `at`: the local player, a listed player, or no one kept.
    void place(EntityId id, const proto::Point& at, Micros ts);
    /// Whether `connection` is the one being followed: ids from any other are
    /// someone else's.
    [[nodiscard]] bool current(uint32_t connection) const noexcept {
        return m_has_connection && connection == m_connection;
    }

    std::unordered_map<EntityId, Player> m_players;
    EntityId m_local{0};
    EntityId m_target{0};
    Micros m_target_at{0};
    bool m_has_connection{false};
    uint32_t m_connection{0};
    uint32_t m_map_id{0};
    /// The names of the local player's party, as the last roster gave them.
    std::unordered_set<std::string> m_party;
    /// The local player's faction, from their own record. It is the character's, so
    /// it outlasts the map; 0 until a record of theirs is seen.
    uint8_t m_local_faction{0};
    /// The local player's position on this map, and when it was last said.
    bool m_has_local_position{false};
    proto::Point m_local_at;
    Micros m_local_at_ts{0};
};

} // namespace aion2::combat_power
