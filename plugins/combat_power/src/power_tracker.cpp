#include "combat_power/power_tracker.hpp"

#include "game/map_names.hpp"
#include "game/skill_names.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

namespace aion2::combat_power {

namespace {

/// The game's units to a metre.
constexpr float kUnitsPerMetre = 100.0f;

[[nodiscard]] float metres_between(const proto::Point& a, const proto::Point& b) noexcept {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz) / kUnitsPerMetre;
}

/// What a list orders a player by: a row's fields, or a player's worked out once
/// without copying their name.
struct ListKey {
    bool threat{false};
    uint32_t combat_power{0};
    /// Negative for none known.
    float distance_m{-1.0f};
    std::string_view name;
    EntityId id{0};
};

[[nodiscard]] bool before(const ListKey& a, const ListKey& b, ListOrder order) noexcept {
    if (order.threats_first && a.threat != b.threat) return a.threat;
    if (order.by_distance) {
        const bool a_known = a.distance_m >= 0.0f;
        const bool b_known = b.distance_m >= 0.0f;
        if (a_known != b_known) return a_known;
        if (a_known && a.distance_m != b.distance_m) return a.distance_m < b.distance_m;
    }
    if (a.combat_power != b.combat_power) return a.combat_power > b.combat_power;
    if (a.name != b.name) return a.name < b.name;
    return a.id < b.id;
}

/// Every order the view can list in, the first the snapshot's own.
constexpr ListOrder kOrders[] = {{false, true}, {false, false}, {true, true}, {true, false}};

} // namespace

bool listed_before(const PowerRow& a, const PowerRow& b, ListOrder order) noexcept {
    return before(ListKey{a.threat, a.combat_power, a.distance_m, a.name, a.id},
                  ListKey{b.threat, b.combat_power, b.distance_m, b.name, b.id}, order);
}

std::vector<PowerRow> PowerSnapshot::list(bool only_enemies, ListOrder order, size_t count) const {
    std::vector<PowerRow> rows = only_enemies ? enemies : players;
    const size_t shown = std::min({rows.size(), count, PowerTracker::kMaxListed});
    std::partial_sort(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(shown), rows.end(),
                      [&](const PowerRow& a, const PowerRow& b) { return listed_before(a, b, order); });
    rows.resize(shown);
    return rows;
}

bool PowerTracker::follow(uint32_t connection) {
    if (!m_has_connection) {
        m_has_connection = true;
        m_connection = connection;
        return true;
    }
    if (connection < m_connection) return false;
    if (connection > m_connection) {
        // Another server's ids: no one here is who they were. The local player's
        // comes with their own record, or their own values.
        m_connection = connection;
        m_players.clear();
        m_target = 0;
        m_local = 0;
        m_map_id = 0;
        m_has_local_position = false;
    }
    return true;
}

void PowerTracker::on_identity(const proto::Identity& identity, uint32_t connection, Micros ts) {
    // A scan's look-alike pairs a name with some other number.
    if (identity.id == 0 || identity.scanned) return;
    if (!follow(connection)) return;
    if (identity.own) {
        // Another id is another map's: where they stood there says nothing here.
        if (m_local != identity.id) m_has_local_position = false;
        m_local = identity.id;
        if (identity.faction != 0) m_local_faction = identity.faction;
        // The id was someone else's on the map before.
        if (m_players.erase(identity.id) != 0 && m_target == identity.id) m_target = 0;
        if (identity.has_position) place(identity.id, identity.position, ts);
        return;
    }
    if (identity.id == m_local) m_local = 0;
    if (identity.name.empty()) return;

    // The same character under a new id: they left and came back, and what was
    // known of them comes along.
    PowerRow carried;
    bool returned = false;
    bool was_target = false;
    std::erase_if(m_players, [&](const auto& entry) {
        const PowerRow& row = entry.second.row;
        if (entry.first == identity.id || row.name != identity.name || row.server != identity.server) return false;
        carried = row;
        returned = true;
        was_target = was_target || m_target == entry.first;
        return true;
    });

    const auto found = m_players.find(identity.id);
    if (found == m_players.end() && m_players.size() >= kMaxPlayers) return;
    Player& player = found == m_players.end() ? m_players[identity.id] : found->second;
    const bool same = player.row.name == identity.name && player.row.server == identity.server;
    if (!same) {
        // The id has passed to someone else.
        player.row = returned ? carried : PowerRow{};
        // Back in view: whatever killed them before, they were sent again.
        player.row.dead = false;
        if (m_target == identity.id) m_target = 0;
    }
    if (was_target) m_target = identity.id;
    player.row.id = identity.id;
    player.row.name = identity.name;
    player.row.server = identity.server;
    // A record whose end did not read leaves the figure it gave before.
    if (identity.combat_power != 0) player.row.combat_power = identity.combat_power;
    player.seen = ts;
    // Where the last holder of the id stood is not where this one does, nor is
    // their faction this one's.
    if (!same) {
        player.has_position = false;
        player.faction = 0;
    }
    if (identity.faction != 0) player.faction = identity.faction;
    if (identity.has_position) {
        player.has_position = true;
        player.at = identity.position;
    }
}

bool PowerTracker::hostile(const Player& player) const {
    if (m_local_faction == 0 || player.faction == 0 || player.faction == m_local_faction) return false;
    // A dungeon's party mixes both factions, and no one else is in there.
    if (game::is_dungeon(m_map_id)) return false;
    return !m_party.contains(player.row.name);
}

bool PowerTracker::threat(const Player& player, bool measurable) const {
    if (player.row.dead || !hostile(player)) return false;
    if (!measurable || !player.has_position) return true;
    return metres_between(player.at, m_local_at) <= kEnemyRange;
}

bool PowerTracker::local_stale(Micros now) const noexcept {
    if (!m_has_local_position) return false;
    const Micros age = now > m_local_at_ts ? now - m_local_at_ts : 0;
    // Flying, the last position is a hundred metres behind within two seconds.
    return age > (m_local_speed > kFastSpeed ? kFastPositionStaleAfter : kPositionStaleAfter);
}

void PowerTracker::on_party(const proto::PartyRoster& roster) {
    m_party.clear();
    for (const proto::PartyMember& member : roster.members) m_party.insert(member.name);
}

void PowerTracker::place(EntityId id, const proto::Point& at, Micros ts) {
    if (id == 0) return;
    if (id == m_local) {
        if (!m_has_local_position) {
            m_speed_from = at;
            m_speed_from_ts = ts;
            m_local_speed = 0.0f;
        } else if (ts >= m_speed_from_ts + kSpeedWindow) {
            const float seconds = static_cast<float>(ts - m_speed_from_ts) / static_cast<float>(kMicrosPerSecond);
            m_local_speed = metres_between(at, m_speed_from) / seconds;
            m_speed_from = at;
            m_speed_from_ts = ts;
        }
        m_has_local_position = true;
        m_local_at = at;
        m_local_at_ts = ts;
        return;
    }
    if (const auto player = m_players.find(id); player != m_players.end()) {
        player->second.has_position = true;
        player->second.at = at;
    }
}

void PowerTracker::on_position(const proto::Position& position, uint32_t connection, Micros ts) {
    if (current(connection)) place(position.id, position.at, ts);
}

void PowerTracker::on_effect(const proto::BuffApplied& effect, uint32_t connection, Micros ts) {
    // A record's listed effects say where no one stands now.
    if (effect.listed) return;
    if (current(connection)) place(effect.caster, effect.caster_position, ts);
}

void PowerTracker::on_values(const proto::RemainingHp& values, uint32_t connection) {
    if (values.id == 0) return;
    if (!values.self) {
        // Anyone else's HP: 0 is dead, anything above it up again.
        if (!values.has_hp || !current(connection)) return;
        if (const auto player = m_players.find(values.id); player != m_players.end()) {
            player->second.row.dead = values.hp == 0;
        }
        return;
    }
    if (!follow(connection) || m_players.contains(values.id)) return;
    if (m_local != values.id) m_has_local_position = false;
    m_local = values.id;
}

void PowerTracker::on_left_view(const proto::LeftView& left, uint32_t connection) {
    // The local player's own id comes too (once, reason 4): they are never listed.
    if (!current(connection) || left.id == m_local) return;
    if (m_players.erase(left.id) != 0 && m_target == left.id) m_target = 0;
}

void PowerTracker::learn_class(EntityId id, uint32_t skill) {
    const auto player = m_players.find(id);
    if (player == m_players.end()) return;
    const game::AionClass cls = game::class_of_skill(skill);
    if (cls != game::AionClass::Unknown) player->second.row.cls = cls;
}

void PowerTracker::aim(EntityId target, Micros ts) {
    m_target = target;
    m_target_at = ts;
}

void PowerTracker::on_cast(const proto::SkillCast& cast, uint32_t connection, Micros ts) {
    // Another connection's ids are another map's: its caster is someone else here.
    if (!current(connection)) return;
    learn_class(cast.caster, cast.skill);
    if (m_local == 0 || cast.caster != m_local || !cast.pressed) return;
    // A self-cast says nothing of the target.
    if (cast.target == 0 || cast.target == m_local) return;
    if (m_players.contains(cast.target)) aim(cast.target, ts);
    else m_target = 0;
}

void PowerTracker::on_damage(const proto::Damage& damage, uint32_t connection, Micros ts) {
    if (!current(connection)) return;
    learn_class(damage.actor, damage.skill);
    if (m_local == 0 || damage.actor != m_local || damage.target == m_local) return;
    // An area skill hits monsters around a player too: only a press lets go.
    const auto player = m_players.find(damage.target);
    if (player == m_players.end()) return;
    // 04 38 carries heals too, and a passive's or a spirit's heal on an ally is no
    // aim: a hit names an enemy, and on anyone else only renews the target.
    if (hostile(player->second) || damage.target == m_target) aim(damage.target, ts);
}

void PowerTracker::on_map_load(const proto::MapLoad& map, uint32_t connection, Micros ts) {
    if (!follow(connection)) return;
    if (m_map_id != 0 && m_map_id != map.map_id) {
        std::erase_if(m_players, [&](const auto& entry) { return entry.second.seen + kEarlyArrival < ts; });
        if (!m_players.contains(m_target)) m_target = 0;
        // Theirs too can come ahead of the load: their own record does.
        if (m_local_at_ts + kEarlyArrival < ts) m_has_local_position = false;
    }
    m_map_id = map.map_id;
}

void PowerTracker::clear() { *this = PowerTracker{}; }

PowerSnapshot PowerTracker::snapshot(Micros now) const {
    PowerSnapshot s;
    s.map_id = m_map_id;
    s.in_view = static_cast<uint32_t>(m_players.size());
    s.has_local_position = m_local != 0 && m_has_local_position;
    if (s.has_local_position) s.local_position_age = now > m_local_at_ts ? now - m_local_at_ts : 0;
    s.local_position_stale = s.has_local_position && local_stale(now);
    // Distances, the range and the nearest only from a position worth measuring from.
    const bool measurable = s.has_local_position && !s.local_position_stale;

    // A row with its distance worked out: only for the few a snapshot carries.
    const auto row_of = [&](const Player& player) {
        PowerRow row = player.row;
        row.hostile = hostile(player);
        row.threat = threat(player, measurable);
        if (measurable && player.has_position) row.distance_m = metres_between(player.at, m_local_at);
        return row;
    };
    if (m_target != 0 && now <= m_target_at + kTargetMemory) {
        if (const auto target = m_players.find(m_target); target != m_players.end()) s.target = row_of(target->second);
    }

    // Each player as a list sees them, worked out once: a town's crowd is sorted by
    // these, its names not copied.
    struct Ranked {
        const Player* player;
        ListKey key;
        bool hostile;
    };
    std::vector<Ranked> everyone;
    everyone.reserve(m_players.size());
    for (const auto& entry : m_players) {
        const Player& player = entry.second;
        const float distance = measurable && player.has_position ? metres_between(player.at, m_local_at) : -1.0f;
        everyone.push_back(Ranked{&player,
                                  ListKey{threat(player, measurable), player.row.combat_power, distance,
                                          player.row.name, player.row.id},
                                  hostile(player)});
    }
    s.hostile_in_view = static_cast<uint32_t>(
        std::count_if(everyone.begin(), everyone.end(), [](const Ranked& r) { return r.key.threat; }));

    // Of a crowd, the first kMaxListed in each order the view can pick: cut in one
    // order and sorted in another, a list loses the rows that lead the second (the
    // nearest enemy, cut for being the weakest). Enemies lead the orders that put the
    // threats first, so no crowd of allies hides one.
    const auto listable = [](std::vector<Ranked>& rows) {
        const auto cut = static_cast<std::ptrdiff_t>(std::min(rows.size(), kMaxListed));
        std::vector<Ranked> kept;
        kept.reserve(static_cast<size_t>(cut) * std::size(kOrders));
        for (const ListOrder order : kOrders) {
            std::partial_sort(rows.begin(), rows.begin() + cut, rows.end(),
                              [&](const Ranked& a, const Ranked& b) { return before(a.key, b.key, order); });
            kept.insert(kept.end(), rows.begin(), rows.begin() + cut);
        }
        std::sort(kept.begin(), kept.end(),
                  [](const Ranked& a, const Ranked& b) { return before(a.key, b.key, kOrders[0]); });
        kept.erase(std::unique(kept.begin(), kept.end(),
                               [](const Ranked& a, const Ranked& b) { return a.player == b.player; }),
                   kept.end());
        std::vector<PowerRow> out;
        out.reserve(kept.size());
        for (const Ranked& r : kept) {
            PowerRow row = r.player->row;
            row.hostile = r.hostile;
            row.threat = r.key.threat;
            row.distance_m = r.key.distance_m;
            out.push_back(std::move(row));
        }
        return out;
    };
    s.players = listable(everyone);

    // The enemies on their own: a crowd's cut keeps those that lead, not those out
    // of range or dead, who still have their red name.
    std::vector<Ranked> enemies;
    std::copy_if(everyone.begin(), everyone.end(), std::back_inserter(enemies), [](const Ranked& r) { return r.hostile; });
    s.enemies_in_view = static_cast<uint32_t>(enemies.size());
    s.enemies = listable(enemies);
    return s;
}

} // namespace aion2::combat_power
