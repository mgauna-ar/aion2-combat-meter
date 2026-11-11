#include "combat_power/power_tracker.hpp"

#include "game/skill_names.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <vector>

namespace aion2::combat_power {

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
    }
    return true;
}

void PowerTracker::on_identity(const proto::Identity& identity, uint32_t connection, Micros ts) {
    // A scan's look-alike pairs a name with some other number.
    if (identity.id == 0 || identity.scanned) return;
    if (!follow(connection)) return;
    if (identity.own) {
        m_local = identity.id;
        // The id was someone else's on the map before.
        if (m_players.erase(identity.id) != 0 && m_target == identity.id) m_target = 0;
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
        if (m_target == identity.id) m_target = 0;
    }
    if (was_target) m_target = identity.id;
    player.row.id = identity.id;
    player.row.name = identity.name;
    player.row.server = identity.server;
    // A record whose end did not read leaves the figure it gave before.
    if (identity.combat_power != 0) player.row.combat_power = identity.combat_power;
    player.seen = ts;
}

void PowerTracker::on_values(const proto::RemainingHp& values, uint32_t connection) {
    if (values.id == 0 || !values.self) return;
    if (!follow(connection) || m_players.contains(values.id)) return;
    m_local = values.id;
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

void PowerTracker::on_cast(const proto::SkillCast& cast, Micros ts) {
    learn_class(cast.caster, cast.skill);
    if (m_local == 0 || cast.caster != m_local || !cast.pressed) return;
    // A self-cast says nothing of the target.
    if (cast.target == 0 || cast.target == m_local) return;
    if (m_players.contains(cast.target)) aim(cast.target, ts);
    else m_target = 0;
}

void PowerTracker::on_damage(const proto::Damage& damage, Micros ts) {
    learn_class(damage.actor, damage.skill);
    if (m_local == 0 || damage.actor != m_local || damage.target == m_local) return;
    // An area skill hits monsters around a player too: only a press lets go.
    if (m_players.contains(damage.target)) aim(damage.target, ts);
}

void PowerTracker::on_map_load(const proto::MapLoad& map, uint32_t connection, Micros ts) {
    if (!follow(connection)) return;
    if (m_map_id != 0 && m_map_id != map.map_id) {
        std::erase_if(m_players, [&](const auto& entry) { return entry.second.seen + kEarlyArrival < ts; });
        if (!m_players.contains(m_target)) m_target = 0;
    }
    m_map_id = map.map_id;
}

void PowerTracker::clear() { *this = PowerTracker{}; }

PowerSnapshot PowerTracker::snapshot(Micros now) const {
    PowerSnapshot s;
    s.map_id = m_map_id;
    s.in_view = static_cast<uint32_t>(m_players.size());
    if (m_target != 0 && now <= m_target_at + kTargetMemory) {
        if (const auto target = m_players.find(m_target); target != m_players.end()) s.target = target->second.row;
    }
    // Pointers first: a town's crowd is sorted without copying its names.
    std::vector<const PowerRow*> rows;
    rows.reserve(m_players.size());
    std::transform(m_players.begin(), m_players.end(), std::back_inserter(rows),
                   [](const auto& entry) { return &entry.second.row; });
    const size_t listed = std::min(rows.size(), kMaxListed);
    std::partial_sort(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(listed), rows.end(),
                      [](const PowerRow* a, const PowerRow* b) {
                          if (a->combat_power != b->combat_power) return a->combat_power > b->combat_power;
                          if (a->name != b->name) return a->name < b->name;
                          return a->id < b->id;
                      });
    s.players.reserve(listed);
    for (size_t i = 0; i < listed; ++i) s.players.push_back(*rows[i]);
    return s;
}

} // namespace aion2::combat_power
