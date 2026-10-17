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

Actor* CombatantRegistry::find_mut(EntityId id) {
    const auto it = m_actors.find(id);
    return it == m_actors.end() ? nullptr : &it->second;
}

CharacterId CombatantRegistry::character_of(EntityId id) const {
    const Actor* a = find(id);
    return a == nullptr ? 0 : a->character;
}

const Character* CombatantRegistry::character(CharacterId who) const {
    return who == 0 || who > m_characters.size() ? nullptr : &m_characters[who - 1];
}

CharacterId CombatantRegistry::character_for(const proto::Identity& identity) {
    const auto [it, inserted] = m_by_name.try_emplace(name_key(identity.name, identity.server), 0);
    if (inserted) {
        m_characters.push_back(Character{identity.name, identity.server});
        it->second = static_cast<CharacterId>(m_characters.size());
    }
    return it->second;
}

void CombatantRegistry::forget(EntityId id) {
    if (id == m_local) m_local = 0;
    m_actors.erase(id);
}

void CombatantRegistry::bind(Actor& a, CharacterId who) {
    Character& c = m_characters[who - 1];
    // Its id from an earlier map may be anyone's by now.
    if (c.id != 0 && c.id != a.id) {
        if (const Actor* old = find(c.id); old != nullptr && old->character == who && old->stale) {
            const bool was_local = c.id == m_local;
            forget(c.id);
            if (was_local) {
                m_local = a.id;
                a.is_local = true;
            }
        }
    }
    a.character = who;
    c.id = a.id;
    if (a.id == m_local) m_local_character = who;
    // Skills seen before the name.
    if (c.cls == AionClass::Unknown) c.cls = a.cls;
}

bool CombatantRegistry::scan_can_name(const proto::Identity& identity) const {
    if (identity.name.empty()) return false;
    const auto named = m_by_name.find(name_key(identity.name, identity.server));
    const CharacterId who = named == m_by_name.end() ? 0 : named->second;
    // Not a spawned NPC or summon, nor a player an identity named on this map.
    if (const Actor* a = find(identity.id); a != nullptr) {
        if (a->spawned) return false;
        if (a->character != 0 && a->character != who && !a->stale) return false;
    }
    // Nor a name an identity gave another id on this map.
    if (const Character* c = character(who); c != nullptr && c->id != 0 && c->id != identity.id) {
        const Actor* holder = find(c->id);
        if (holder != nullptr && holder->character == who && !holder->stale) return false;
    }
    return true;
}

void CombatantRegistry::on_identity(const proto::Identity& identity) {
    if (identity.id == 0) return;
    // A scan's look-alike may have misread the id in front of the name.
    if (identity.scanned && !scan_can_name(identity)) return;
    const CharacterId who = identity.name.empty() ? 0 : character_for(identity);
    Actor& a = get_or_create(identity.id);
    // A spawn's id, or another character's: the server has handed it on, and
    // nothing known about it holds any more.
    if (a.spawned || (who != 0 && a.character != 0 && a.character != who)) {
        if (identity.id == m_local && !identity.own) m_local = 0;
        a = Actor{};
        a.id = identity.id;
    }
    a.is_player = true;
    a.inferred_side = Side::Unknown;
    if (who != 0) {
        a.stale = false;
        bind(a, who);
        Character& c = m_characters[who - 1];
        c.class_code = identity.class_code;
        // An unknown code keeps the class the player's skills gave.
        if (const AionClass cls = class_from_code(identity.class_code); cls != AionClass::Unknown) c.cls = cls;
    }
    if (identity.own) set_local(identity.id);
    settle_pending(identity.id);
}

void CombatantRegistry::on_skill_class(EntityId player, AionClass cls) {
    if (player == 0 || cls == AionClass::Unknown) return;
    Actor* a = find_mut(player);
    if (a == nullptr) return;
    if (a->cls == AionClass::Unknown) a->cls = cls;
    // A stale id's skills may be whoever has it now.
    if (a->character != 0 && !a->stale) {
        Character& c = m_characters[a->character - 1];
        if (c.cls == AionClass::Unknown) c.cls = cls;
    }
}

void CombatantRegistry::set_local(EntityId id) {
    if (m_local != 0 && m_local != id) {
        if (Actor* before = find_mut(m_local)) before->is_local = false;
    }
    m_local = id;
    Actor& a = get_or_create(id);
    a.is_local = true;
    if (a.character != 0) {
        m_local_character = a.character;
    } else if (m_local_character != 0) {
        // One character, on a new id: their name moves with them.
        bind(a, m_local_character);
    }
}

void CombatantRegistry::on_self_values(EntityId id) {
    if (id == 0) return;
    Actor& a = get_or_create(id);
    // An NPC or a summon is never the local player.
    if (a.spawned) return;
    // Only the local player gets these: the id is theirs on this map.
    a.stale = false;
    if (id == m_local) return;
    a.is_player = true;
    a.inferred_side = Side::Unknown;
    set_local(id);
    settle_pending(id);
}

void CombatantRegistry::on_hp(EntityId id, uint64_t hp) {
    if (id == 0) return;
    // Not resolve(): a summon's HP is its own, not its owner's.
    Actor& a = get_or_create(id);
    a.has_live_hp = true;
    a.live_hp = hp;
    // Without a spawn (it came into view before the meter started), the most HP
    // seen is the best lower bound on its max.
    if (hp > a.max_hp) a.max_hp = hp;
    if (hp > 0 && a.is_player) a.dead = false;
}

void CombatantRegistry::on_spawn(const proto::Spawn& spawn) {
    if (spawn.id == 0) return;
    Actor& a = get_or_create(spawn.id);
    // A spawn for an id an identity named on this map is a player appearing in
    // view; it says nothing about their side. One named on an earlier map has
    // been handed on.
    if (a.is_player && !a.stale) return;
    if (a.is_player) {
        if (spawn.id == m_local) m_local = 0;
        a = Actor{};
        a.id = spawn.id;
    }
    a.spawned = true;
    a.spawn_kind = spawn.kind;
    a.npc_code = spawn.npc_code;
    a.dead = false;
    a.inferred_side = Side::Unknown;
    a.has_live_hp = false;
    if (spawn.has_hp) {
        a.has_hp = true;
        a.spawn_hp = spawn.cur_hp;
        a.max_hp = spawn.max_hp;
    }
    if (proto::spawn_kind::is_player_object(spawn.kind) && spawn.owner != 0 && spawn.owner != spawn.id) {
        a.owner = spawn.owner;
        // Its hits are credited to the owner, whose side the spawn has just told us.
        make_player(spawn.owner);
    }
    settle_pending(spawn.id);
}

void CombatantRegistry::on_death(EntityId id) {
    if (id == 0) return;
    // Not resolve(): a summon dying leaves its owner alive.
    get_or_create(id).dead = true;
}

void CombatantRegistry::on_revive(EntityId id) {
    if (Actor* a = find_mut(id)) a->dead = false;
}

void CombatantRegistry::on_party(const proto::PartyRoster& roster) {
    m_roster.clear();
    for (const auto& member : roster.members) m_roster.insert(member.name);
}

void CombatantRegistry::mark_players_stale() {
    for (auto& [id, a] : m_actors) {
        if (a.is_player) a.stale = true;
    }
}

void CombatantRegistry::on_map_change(bool instance) {
    m_in_instance = instance;
    for (auto it = m_actors.begin(); it != m_actors.end();) {
        if (it->second.is_player) {
            it->second.dead = false;
            ++it;
        } else {
            it = m_actors.erase(it);
        }
    }
    mark_players_stale();
    m_pending.clear();
}

void CombatantRegistry::on_map_reload() { mark_players_stale(); }

EntityId CombatantRegistry::resolve(EntityId id) const {
    for (int hop = 0; hop < kMaxOwnerHops; ++hop) {
        const Actor* a = find(id);
        if (a == nullptr || a->owner == 0) return id;
        id = a->owner;
    }
    return id;
}

Side CombatantRegistry::side(EntityId id) const {
    if (id == 0) return Side::Unknown;
    for (int hop = 0; hop < kMaxOwnerHops; ++hop) {
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

bool CombatantRegistry::known_npc(EntityId id) const {
    const Actor* a = find(id);
    return a != nullptr && a->spawned && side(id) == Side::Enemy;
}

void CombatantRegistry::make_player(EntityId id) {
    const Actor* a = find(id);
    // Identity and spawn packets outrank anything inferred.
    if (a != nullptr && (a->is_player || a->spawned)) return;
    if (side(id) == Side::Player) return;
    get_or_create(id).inferred_side = Side::Player;
    settle_pending(id);
}

void CombatantRegistry::observe_hit(EntityId actor, EntityId target) {
    const EntityId a = resolve(actor);
    const EntityId t = resolve(target);
    if (a == 0 || t == 0 || a == t) return;
    // Mobs do not fight mobs, so the other end of a hit with a spawned NPC is a
    // player, whatever was inferred before: 04 38 carries heals too, and a heal
    // between two players makes one of them look like an enemy.
    if (known_npc(t)) {
        make_player(a);
        return;
    }
    if (known_npc(a)) {
        make_player(t);
        return;
    }
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
    if (!has_roster()) return m_in_instance;
    const Character* c = character(character_of(id));
    return c != nullptr && m_roster.count(c->name) != 0;
}

bool CombatantRegistry::character_in_party(CharacterId who) const {
    const Character* c = character(who);
    if (c == nullptr) return false;
    if (who == m_local_character) return true;
    return has_roster() ? m_roster.count(c->name) != 0 : m_in_instance;
}

AionClass CombatantRegistry::class_of(EntityId id) const {
    const Actor* a = find(id);
    if (a == nullptr) return AionClass::Unknown;
    if (const Character* c = character(a->character); c != nullptr && c->cls != AionClass::Unknown) return c->cls;
    return a->cls;
}

std::string CombatantRegistry::display_name(EntityId id) const {
    const EntityId r = resolve(id);
    if (const Character* c = character(character_of(r))) return c->name;
    if (r != 0 && r == m_local) return "You";
    return "#" + std::to_string(r);
}

} // namespace aion2::meter
