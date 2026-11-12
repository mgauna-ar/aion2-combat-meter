#pragma once

#include "common/micros.hpp"
#include "game/classes.hpp"
#include "protocol/events.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
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

    [[nodiscard]] bool has_distance() const noexcept { return distance_m >= 0.0f; }

    bool operator==(const PowerRow&) const = default;
};

/// What the combat power overlay draws.
struct PowerSnapshot {
    /// The player the local player last used a skill on; `target.id` 0 for none.
    PowerRow target;
    /// The other players on the map, strongest first: all of an arena's opponents or
    /// a dungeon's party, and of a crowd the PowerTracker::kMaxListed strongest.
    std::vector<PowerRow> players;
    /// The same players, nearest first, those at no known distance last: of a
    /// crowd, the PowerTracker::kMaxListed nearest. Empty until the local player's
    /// position is known.
    std::vector<PowerRow> nearest;
    /// How many there are, listed or not.
    uint32_t in_view{0};
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
    /// Most players remembered; a misread flood stops here.
    static constexpr size_t kMaxPlayers = 2048;

    void on_identity(const proto::Identity& identity, uint32_t connection, Micros ts);
    /// 00 8D: the local player's own values name them when 33 36 was missed.
    void on_values(const proto::RemainingHp& values, uint32_t connection);
    void on_cast(const proto::SkillCast& cast, Micros ts);
    void on_damage(const proto::Damage& damage, Micros ts);
    void on_map_load(const proto::MapLoad& map, uint32_t connection, Micros ts);
    /// A step, or a skill that moved someone: a listed player's, or the local player's.
    void on_position(const proto::Position& position, uint32_t connection, Micros ts);
    /// An effect put on or renewed: it says where its caster stands.
    void on_effect(const proto::BuffApplied& effect, uint32_t connection, Micros ts);
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
    };

    /// A newer connection forgets the map before; false for an event from an older
    /// one, which is ignored.
    [[nodiscard]] bool follow(uint32_t connection);
    void learn_class(EntityId id, uint32_t skill);
    void aim(EntityId target, Micros ts);
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
    /// The local player's position on this map, and when it was last said.
    bool m_has_local_position{false};
    proto::Point m_local_at;
    Micros m_local_at_ts{0};
};

} // namespace aion2::combat_power
