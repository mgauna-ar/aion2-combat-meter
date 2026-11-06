#include "auras/entities.hpp"

#include "game/npc_names.hpp"

#include <tuple>

namespace aion2::auras {

namespace {

/// A summon's summon at most this deep; a loop in misread owners stops here.
constexpr int kMaxOwnerHops = 4;

} // namespace

void Entities::on_identity(const proto::Identity& identity) {
    // A scan's look-alike pairs a name with some other number (the combat meter's
    // registry ignores them for the same reason).
    if (identity.id == 0 || identity.scanned) return;
    // The server has handed a spawn's id to a player.
    m_spawns.erase(identity.id);
    if (m_players.size() < kMaxSpawns) m_players.insert(identity.id);
    if (identity.own) {
        m_local = identity.id;
        if (!identity.name.empty()) m_character = identity.name + "@" + std::to_string(identity.server);
    } else if (identity.id == m_local) {
        m_local = 0;
    }
}

void Entities::on_spawn(const proto::Spawn& spawn) {
    if (spawn.id == 0 || m_players.contains(spawn.id)) return;
    if (spawn.id == m_local) m_local = 0;
    if (m_spawns.size() >= kMaxSpawns && !m_spawns.contains(spawn.id)) return;
    Spawned& s = m_spawns[spawn.id];
    s = Spawned{};
    s.kind = spawn.kind;
    s.npc_code = spawn.npc_code;
    s.aimed = !spawn.aimed_at.empty();
    if (spawn.has_hp) s.max_hp = spawn.max_hp;
    if (proto::spawn_kind::is_player_object(spawn.kind) && spawn.owner != 0 && spawn.owner != spawn.id) {
        s.owner = spawn.owner;
    }
}

void Entities::on_values(const proto::RemainingHp& values) {
    if (values.id == 0) return;
    const auto s = m_spawns.find(values.id);
    // An NPC or a summon is never the local player.
    if (values.self && s == m_spawns.end()) m_local = values.id;
    if (!values.has_hp || s == m_spawns.end()) return;
    // A mob in view before its spawn was seen: the most HP seen stands in for its max.
    if (values.hp > s->second.max_hp) s->second.max_hp = values.hp;
    if (values.hp > 0) s->second.dead = false;
}

void Entities::on_damage(const proto::Damage& damage, Micros ts) {
    if (damage.amount == 0 || !mine(damage.actor)) return;
    const auto s = m_spawns.find(damage.target);
    if (s == m_spawns.end() || s->second.aimed || !is_enemy(damage.target)) return;
    s->second.last_hit = ts;
}

void Entities::on_dead(EntityId id) {
    if (const auto s = m_spawns.find(id); s != m_spawns.end()) s->second.dead = true;
}

void Entities::on_gone(EntityId id) { m_spawns.erase(id); }

void Entities::on_map_load(bool reload) {
    // Players may have new ids even on a reload; the spawns stay with the run.
    m_players.clear();
    if (!reload) m_spawns.clear();
}

EntityId Entities::resolve(EntityId id) const {
    for (int hop = 0; hop < kMaxOwnerHops; ++hop) {
        const auto s = m_spawns.find(id);
        if (s == m_spawns.end() || s->second.owner == 0) return id;
        id = s->second.owner;
    }
    return id;
}

bool Entities::is_enemy(EntityId id) const {
    const auto s = m_spawns.find(resolve(id));
    return s != m_spawns.end() && !proto::spawn_kind::is_player_object(s->second.kind);
}

EntityId Entities::target(Micros now) const {
    EntityId best = 0;
    std::tuple<uint64_t, Micros> best_rank{0, 0};
    for (const auto& [id, s] : m_spawns) {
        if (s.last_hit == 0 || s.dead || s.aimed || now > s.last_hit + kTargetMemory) continue;
        const std::tuple<uint64_t, Micros> rank{s.max_hp, s.last_hit};
        if (best == 0 || rank > best_rank) {
            best = id;
            best_rank = rank;
        }
    }
    return best;
}

std::string Entities::label(EntityId id) const {
    const auto s = m_spawns.find(id);
    if (s != m_spawns.end() && s->second.npc_code != 0) return game::npc_label(s->second.npc_code);
    return "#" + std::to_string(id);
}

} // namespace aion2::auras
