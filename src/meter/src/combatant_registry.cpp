#include "meter/combatant_registry.hpp"

#include "meter/class_codes.hpp"

#include <vector>

namespace aion2::meter {

namespace {

/// Owner links followed before giving up: a summon's summon is plausible, a loop is not.
constexpr int kMaxOwnerHops = 8;

[[nodiscard]] Side opposite(Side s) noexcept {
    switch (s) {
        case Side::Player: return Side::Enemy;
        case Side::Enemy: return Side::Player;
        default: return Side::Unknown;
    }
}

[[nodiscard]] std::string name_key(const std::string& name, uint16_t server) {
    return name + "@" + std::to_string(server);
}

} // namespace

Actor& CombatantRegistry::get_or_create(EntityId id) {
    auto [it, inserted] = m_actors.try_emplace(id);
    if (inserted) it->second.id = id;
    return it->second;
}

const Actor* CombatantRegistry::find(EntityId id) const {
    const auto it = m_actors.find(id);
    return it == m_actors.end() ? nullptr : &it->second;
}

void CombatantRegistry::on_identity(const proto::Identity& identity) {
    if (identity.id == 0) return;
    EntityId canonical = identity.id;
    if (!identity.name.empty()) {
        const std::string key = name_key(identity.name, identity.server);
        const auto [it, inserted] = m_by_name.try_emplace(key, identity.id);
        if (!inserted && it->second != identity.id) {
            canonical = it->second;
            m_alias[identity.id] = canonical;
        }
    }

    for (const EntityId id : {identity.id, canonical}) {
        Actor& a = get_or_create(id);
        a.is_player = true;
        if (!identity.name.empty()) {
            a.name = identity.name;
            a.server = identity.server;
            a.class_code = identity.class_code;
            a.cls = class_from_code(identity.class_code);
        }
        a.spawned = false;
        a.owner = 0;
        a.inferred_side = Side::Unknown;
    }
    if (identity.own) {
        if (m_local != 0 && m_local != canonical) get_or_create(m_local).is_local = false;
        m_local = canonical;
        get_or_create(canonical).is_local = true;
        get_or_create(identity.id).is_local = true;
    }
    settle_pending(identity.id);
}

void CombatantRegistry::on_spawn(const proto::Spawn& spawn) {
    if (spawn.id == 0) return;
    Actor& a = get_or_create(spawn.id);
    // A spawn for an id an identity packet named is a player appearing in view;
    // it says nothing about their side.
    if (a.is_player) return;
    a.spawned = true;
    a.spawn_kind = spawn.kind;
    a.npc_code = spawn.npc_code;
    a.dead = false;
    a.inferred_side = Side::Unknown;
    if (spawn.has_hp) {
        a.has_hp = true;
        a.spawn_hp = spawn.cur_hp;
        a.max_hp = spawn.max_hp;
    }
    if (proto::spawn_kind::is_player_object(spawn.kind) && spawn.owner != 0 && spawn.owner != spawn.id) {
        a.owner = spawn.owner;
    }
    settle_pending(spawn.id);
}

void CombatantRegistry::on_death(EntityId id) {
    if (id != 0) get_or_create(resolve(id)).dead = true;
}

void CombatantRegistry::on_revive(EntityId id) {
    const auto it = m_actors.find(resolve(id));
    if (it != m_actors.end()) it->second.dead = false;
}

void CombatantRegistry::on_party(const proto::PartyRoster& roster) {
    m_roster.clear();
    for (const auto& member : roster.members) m_roster.insert(member.name);
}

void CombatantRegistry::on_map_change() {
    for (auto it = m_actors.begin(); it != m_actors.end();) {
        if (it->second.is_player) {
            it->second.dead = false;
            ++it;
        } else {
            it = m_actors.erase(it);
        }
    }
    m_pending.clear();
}

EntityId CombatantRegistry::resolve(EntityId id) const {
    for (int hop = 0; hop < kMaxOwnerHops; ++hop) {
        if (const auto alias = m_alias.find(id); alias != m_alias.end()) id = alias->second;
        const Actor* a = find(id);
        if (a == nullptr || a->owner == 0) return id;
        id = a->owner;
    }
    return id;
}

Side CombatantRegistry::side(EntityId id) const {
    if (id == 0) return Side::Unknown;
    for (int hop = 0; hop < kMaxOwnerHops; ++hop) {
        if (const auto alias = m_alias.find(id); alias != m_alias.end()) id = alias->second;
        const Actor* a = find(id);
        if (a == nullptr) return Side::Unknown;
        if (a->is_player) return Side::Player;
        // A summon or effect fights for the players whoever owns it, known or not.
        if (a->spawned && proto::spawn_kind::is_player_object(a->spawn_kind)) return Side::Player;
        if (a->owner != 0) {
            id = a->owner;
            continue;
        }
        if (a->spawned) return Side::Enemy;
        return a->inferred_side;
    }
    return Side::Unknown;
}

Side CombatantRegistry::side_or_guess(EntityId id) const {
    const Side s = side(id);
    if (s != Side::Unknown) return s;
    return id >= kFoldedLow && id < kFoldedHigh ? Side::Enemy : Side::Unknown;
}

void CombatantRegistry::set_inferred(EntityId id, Side s) {
    if (s == Side::Unknown || side(id) != Side::Unknown) return;
    get_or_create(id).inferred_side = s;
}

void CombatantRegistry::observe_hit(EntityId actor, EntityId target) {
    const EntityId a = resolve(actor);
    const EntityId t = resolve(target);
    if (a == 0 || t == 0 || a == t) return;
    const Side sa = side_or_guess(a);
    const Side st = side_or_guess(t);
    if (sa != Side::Unknown && st == Side::Unknown) {
        set_inferred(t, opposite(sa));
        settle_pending(t);
    } else if (st != Side::Unknown && sa == Side::Unknown) {
        set_inferred(a, opposite(st));
        settle_pending(a);
    } else if (sa == Side::Unknown && st == Side::Unknown) {
        m_pending.emplace_back(a, t);
        if (m_pending.size() > kMaxPendingEdges) m_pending.pop_front();
    }
}

void CombatantRegistry::settle_pending(EntityId known) {
    std::vector<EntityId> work{known};
    while (!work.empty() && !m_pending.empty()) {
        const EntityId id = resolve(work.back());
        work.pop_back();
        const Side s = side(id);
        if (s == Side::Unknown) continue;
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            EntityId other = 0;
            if (resolve(it->first) == id) other = resolve(it->second);
            else if (resolve(it->second) == id) other = resolve(it->first);
            if (other == 0) {
                ++it;
                continue;
            }
            if (side(other) == Side::Unknown) {
                set_inferred(other, opposite(s));
                work.push_back(other);
            }
            it = m_pending.erase(it);
        }
    }
}

bool CombatantRegistry::in_party(EntityId id) const {
    id = resolve(id);
    if (id == 0) return false;
    if (id == m_local) return true;
    if (side(id) != Side::Player) return false;
    if (!has_roster()) return true;
    const Actor* a = find(id);
    return a != nullptr && !a->name.empty() && m_roster.count(a->name) != 0;
}

std::string CombatantRegistry::display_name(EntityId id) const {
    const EntityId r = resolve(id);
    const Actor* a = find(r);
    if (a != nullptr && !a->name.empty()) return a->name;
    return "#" + std::to_string(r);
}

} // namespace aion2::meter
