#include "meter/encounter_engine.hpp"

#include "meter/npc_names.hpp"
#include "meter/skill_names.hpp"

#include <algorithm>
#include <type_traits>
#include <variant>

namespace aion2::meter {

namespace {

/// DPS divides by at least this, so a one-hit fight does not read as millions.
constexpr double kMinDurationSeconds = 1.0;

[[nodiscard]] uint64_t grew(uint64_t now, uint64_t then) noexcept { return now >= then ? now - then : 0; }

void add(HitCounts& into, const HitCounts& h) noexcept {
    into.hits += h.hits;
    into.crits += h.crits;
    into.back += h.back;
    into.front += h.front;
    into.blocks += h.blocks;
    into.multi += h.multi;
    into.perfect += h.perfect;
    into.doubles += h.doubles;
    into.heavy += h.heavy;
}

/// One direct hit's crit, modifiers and direction.
void count(HitCounts& h, const proto::Damage& d) noexcept {
    ++h.hits;
    if (d.crit) ++h.crits;
    if (d.back()) ++h.back;
    if (d.front()) ++h.front;
    if ((d.modifiers & proto::hit_modifier::Block) != 0) ++h.blocks;
    if (d.multi_hit()) ++h.multi;
    if ((d.modifiers & proto::hit_modifier::Perfect) != 0) ++h.perfect;
    if ((d.modifiers & proto::hit_modifier::Double) != 0) ++h.doubles;
    if ((d.modifiers & proto::hit_modifier::Heavy) != 0) ++h.heavy;
}

void add(SkillTotals& into, const SkillTotals& s) noexcept {
    if (s.hits.hits > 0) into.min_hit = into.hits.hits == 0 ? s.min_hit : std::min(into.min_hit, s.min_hit);
    into.max_hit = std::max(into.max_hit, s.max_hit);
    into.damage += s.damage;
    into.tick_damage += s.tick_damage;
    into.ticks += s.ticks;
    add(into.hits, s.hits);
}

void add(SkillAmount& into, const SkillAmount& s) noexcept {
    into.amount += s.amount;
    into.hits += s.hits;
    into.max = std::max(into.max, s.max);
}

void book(SkillAmount& s, uint64_t amount) noexcept {
    s.amount += amount;
    ++s.hits;
    s.max = std::max(s.max, amount);
}

/// The map's values, most `amount` first (ties by code, for a stable order).
template <typename Map, typename Amount>
auto sorted_values(const Map& map, Amount amount) {
    std::vector<typename Map::mapped_type> out;
    out.reserve(map.size());
    for (const auto& [key, value] : map) out.push_back(value);
    std::sort(out.begin(), out.end(), [&](const auto& a, const auto& b) {
        if (amount(a) != amount(b)) return amount(a) > amount(b);
        return a.code < b.code;
    });
    return out;
}

/// The boss's name from its npc code, or "NPC #code" for a code not listed, or "#id"
/// for a target never seen spawning.
[[nodiscard]] std::string target_label(uint32_t npc_code, EntityId id) {
    if (npc_code != 0) return npc_label(npc_code);
    return "#" + std::to_string(id);
}

} // namespace

EncounterEngine::EncounterEngine(MeterSettings settings) : m_settings(settings) {}

void EncounterEngine::Accum::merge(const Accum& o) {
    damage += o.damage;
    dot_damage += o.dot_damage;
    max_hit = std::max(max_hit, o.max_hit);
    add(hits, o.hits);
    for (const auto& [key, s] : o.skills) {
        SkillTotals& into = skills[key];
        into.code = s.code;
        add(into, s);
    }
    healing += o.healing;
    heal_hits += o.heal_hits;
    for (const auto& [key, s] : o.heals) {
        SkillAmount& into = heals[key];
        into.code = s.code;
        add(into, s);
    }
    taken += o.taken;
    add(taken_hits, o.taken_hits);
    for (const auto& [key, s] : o.taken_by) {
        SkillAmount& into = taken_by[key];
        into.code = s.code;
        into.source_npc = s.source_npc;
        add(into, s);
    }
}

void EncounterEngine::on_event(const proto::ParsedEvent& event) {
    const Micros ts = event.ts;
    check_end(ts);
    if (ts > m_last_event_ts) m_last_event_ts = ts;

    std::visit(
        [&](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, proto::Heartbeat>) {
                m_last_server_ms = e.server_ms;
            } else if constexpr (std::is_same_v<T, proto::Identity>) {
                m_registry.on_identity(e);
            } else if constexpr (std::is_same_v<T, proto::Spawn>) {
                on_spawn(e);
            } else if constexpr (std::is_same_v<T, proto::Death>) {
                on_death(e.id, ts);
            } else if constexpr (std::is_same_v<T, proto::MapLoad>) {
                on_map(e, ts);
            } else if constexpr (std::is_same_v<T, proto::Damage>) {
                on_damage(e, ts);
            } else if constexpr (std::is_same_v<T, proto::Tick>) {
                on_tick(e, ts);
            } else if constexpr (std::is_same_v<T, proto::PartyRoster>) {
                m_registry.on_party(e);
            } else if constexpr (std::is_same_v<T, proto::RemainingHp>) {
                on_values(e, ts);
            }
        },
        event.event);
}

void EncounterEngine::note_hit_on(EntityId target) { m_hp_loss[target].hit_since = true; }

void EncounterEngine::on_spawn(const proto::Spawn& s) {
    m_registry.on_spawn(s);
    // Without HP the registry keeps the spawn HP it had, and the damage counted
    // against it with it.
    if (!s.has_hp) return;
    m_damage_since_spawn.erase(s.id);
    // Coming back into view, a boss keeps the HP loss counted so far, and its HP
    // now is one more figure. A new id, or one last seen at 0, starts afresh.
    const auto loss = m_hp_loss.find(s.id);
    if (loss == m_hp_loss.end() || !loss->second.known || loss->second.last == 0) {
        m_hp_loss[s.id] = HpLoss{true, s.cur_hp, false, 0, 0};
    } else {
        loss->second.observe(s.cur_hp);
    }
}

void EncounterEngine::on_damage(const proto::Damage& d, Micros ts) {
    if (d.actor == 0 || d.target == 0) return;
    note_hit_on(d.target);
    m_registry.observe_hit(d.actor, d.target);
    const uint64_t amount = d.amount;
    if (amount == 0) return;
    m_damage_since_spawn[m_registry.resolve(d.target)] += amount;
    credit(d.actor, d.target, amount, d.skill, &d, ts);
    book_heal(d.actor, d.target, amount, d.skill, true);
    book_taken(d.actor, d.target, amount, d.skill, &d);
}

void EncounterEngine::on_tick(const proto::Tick& t, Micros ts) {
    if (t.amount == 0 || t.actor == 0 || t.target == 0) return;
    // A heal settles no side: healers and their targets are on one.
    if (t.heal) {
        book_heal(t.actor, t.target, t.amount, t.skill, false);
        return;
    }
    note_hit_on(t.target);
    m_registry.observe_hit(t.actor, t.target);
    m_damage_since_spawn[m_registry.resolve(t.target)] += t.amount;
    credit(t.actor, t.target, t.amount, t.skill, nullptr, ts);
    book_taken(t.actor, t.target, t.amount, t.skill, nullptr);
}

bool EncounterEngine::counts_in_fight(EntityId player) const {
    if (!m_fight || !m_registry.in_party(player)) return false;
    if (m_registry.has_roster() || player == m_registry.local_player()) return true;
    return m_fight->players.contains(row_key(player));
}

void EncounterEngine::book_heal(EntityId actor, EntityId target, uint64_t amount, uint32_t skill, bool record) {
    if (!m_fight) return;
    const EntityId healer = m_registry.resolve(actor);
    if (m_registry.side(healer) != Side::Player || m_registry.side(m_registry.resolve(target)) != Side::Player) return;
    // Not a summon's record: a spirit's to itself, as it spawned, carried 100,000.
    if (record && actor != healer) return;
    // A record between two players may be a PvP hit, not a heal: only one landing
    // on the party counts. Heal ticks are heals, wherever they land.
    if (record && !m_registry.in_party(target)) return;
    if (!counts_in_fight(healer)) return;
    m_registry.on_skill_class(healer, class_of_skill(skill));
    Accum& acc = m_fight->players[row_key(healer)];
    acc.healing += amount;
    ++acc.heal_hits;
    const uint32_t key = skill_key(skill);
    SkillAmount& s = acc.heals[key];
    s.code = key;
    book(s, amount);
}

void EncounterEngine::book_taken(EntityId actor, EntityId target, uint64_t amount, uint32_t skill,
                                 const proto::Damage* direct) {
    if (!m_fight) return;
    // A summon's HP is its own, not its owner's.
    const EntityId victim = target;
    if (m_registry.resolve(victim) != victim || m_registry.side(victim) != Side::Player) return;
    const EntityId attacker = m_registry.resolve(actor);
    if (m_registry.side_or_guess(attacker) != Side::Enemy) return;
    if (!counts_in_fight(victim)) return;
    Accum& acc = m_fight->players[row_key(victim)];
    acc.taken += amount;
    if (direct != nullptr) count(acc.taken_hits, *direct);
    const Actor* a = m_registry.find(attacker);
    const uint32_t npc = a != nullptr ? a->npc_code : 0;
    const uint32_t key = skill_key(skill);
    SkillAmount& s = acc.taken_by[(static_cast<uint64_t>(key) << 32) | npc];
    s.code = key;
    s.source_npc = npc;
    book(s, amount);
}

void EncounterEngine::credit(EntityId actor, EntityId target, uint64_t amount, uint32_t skill,
                             const proto::Damage* direct, Micros ts) {
    const EntityId player = m_registry.resolve(actor);
    const EntityId enemy = m_registry.resolve(target);
    if (m_registry.side(player) != Side::Player || m_registry.side_or_guess(enemy) != Side::Enemy) return;
    m_registry.on_skill_class(player, class_of_skill(skill));
    // Breaking a trap around a party member, 1 damage a hit: the game counts no hit
    // and no damage, and neither starts or extends a fight.
    if (const Actor* e = m_registry.find(enemy); e != nullptr && e->aimed_mechanic) return;
    // In a party, its members count; out of one, everyone in an instance, and
    // only the local player in the open world.
    if (!m_registry.in_party(player)) return;

    if (!m_fight) {
        if (direct == nullptr) return;  // a tick alone never starts a fight
        // The meter started mid-visit: its first fight opens one.
        if (m_visit_ts == 0) m_visit_ts = ts;
        Fight f;
        f.id = m_next_fight_id++;
        f.map_id = m_map_id;
        f.visit_ts = m_visit_ts;
        f.start_ts = ts;
        f.counters_at_start = m_counters;
        m_fight = std::move(f);
    }

    Fight& fight = *m_fight;
    Accum& acc = fight.players[row_key(player)];
    acc.damage += amount;
    acc.max_hit = std::max(acc.max_hit, amount);
    const uint32_t key = skill_key(skill);
    SkillTotals& sk = acc.skills[key];
    sk.code = key;
    sk.damage += amount;
    if (direct != nullptr) {
        count(acc.hits, *direct);
        sk.min_hit = sk.hits.hits == 0 ? amount : std::min(sk.min_hit, amount);
        sk.max_hit = std::max(sk.max_hit, amount);
        count(sk.hits, *direct);
    } else {
        acc.dot_damage += amount;
        sk.tick_damage += amount;
        ++sk.ticks;
    }
    fight.enemies[enemy] += amount;
    if (ts > fight.last_hit_ts) fight.last_hit_ts = ts;
    // Only the player's own hand proves them alive: their DoTs and summons keep
    // going after they die.
    if (direct != nullptr && actor == player) m_registry.on_revive(player);
}

void EncounterEngine::on_values(const proto::RemainingHp& v, Micros ts) {
    if (v.self) m_registry.on_self_values(v.id);
    if (!v.has_hp) return;
    m_registry.on_hp(v.id, v.hp);
    m_hp_loss[v.id].observe(v.hp);
    // HP reaching 0 is the death; 42 36 follows seconds later, with the corpse.
    if (v.hp == 0) on_death(v.id, ts);
}

void EncounterEngine::on_death(EntityId id, Micros ts) {
    m_registry.on_death(id);
    if (!m_fight) return;
    const EntityId who = m_registry.resolve(id);
    const BossSummary boss = boss_of(*m_fight);
    if (boss.present() && boss.is_boss && boss.id == who && !m_fight->boss_killed) {
        m_fight->boss_killed = true;
        m_fight->kill_ts = ts;
        return;
    }
    if (m_settings.wipe_detection && m_registry.side(who) == Side::Player && party_wiped(*m_fight)) {
        finish(EndReason::Wipe, ts);
    }
}

void EncounterEngine::on_map(const proto::MapLoad& m, Micros ts) {
    // Every load is a new visit, the map you are on included: running a dungeon
    // again, straight back in, loads the same map id, and each run is its own.
    if (m_fight) finish(EndReason::MapChange, ts);
    m_map_id = m.map_id;
    m_visit_ts = ts;
    m_registry.on_map_change(m.is_instance());
    m_damage_since_spawn.clear();
    m_hp_loss.clear();
}

bool EncounterEngine::party_wiped(const Fight& fight) const {
    bool any = false;
    for (const auto& [key, acc] : fight.players) {
        // An id that has since become someone else says nothing of this row.
        if (key.who != 0 && m_registry.character_of(key.id) != key.who) continue;
        const Actor* a = m_registry.find(key.id);
        if (a == nullptr || !a->dead) return false;
        any = true;
    }
    return any;
}

void EncounterEngine::check_end(Micros now) {
    if (!m_fight) return;
    const Fight& f = *m_fight;
    const BossSummary boss = boss_of(f);
    if (f.boss_killed && now >= f.kill_ts + from_seconds(m_settings.kill_grace_s)) {
        // Unless a bigger boss was hit during the grace: the fight goes on.
        if (boss.present() && boss.is_boss && boss.killed) {
            finish(EndReason::BossKilled, now);
            return;
        }
        m_fight->boss_killed = false;
    }
    const bool boss_standing = boss.present() && boss.is_boss && !boss.killed;
    const double idle = boss_standing ? m_settings.boss_idle_timeout_s : m_settings.idle_timeout_s;
    if (now > f.last_hit_ts && now - f.last_hit_ts >= from_seconds(idle)) {
        finish(EndReason::Idle, now);
        return;
    }
    if (m_last_event_ts != 0 && now > m_last_event_ts &&
        now - m_last_event_ts >= from_seconds(m_settings.capture_lost_s)) {
        finish(EndReason::CaptureLost, now);
    }
}

void EncounterEngine::tick(Micros now) { check_end(now); }

void EncounterEngine::end_fight(EndReason reason, Micros now) {
    if (m_fight) finish(reason, now);
}

void EncounterEngine::finish(EndReason reason, Micros) {
    if (!m_fight) return;
    FightSummary s = summarize(*m_fight, false, reason);
    m_fight.reset();
    m_finished.push_back(s);
    m_recent.push_back(std::move(s));
    const size_t cap = static_cast<size_t>(std::max(1, m_settings.memory_history));
    while (m_recent.size() > cap) m_recent.pop_front();
    ++m_recent_revision;
}

BossSummary EncounterEngine::boss_of(const Fight& fight) const {
    EntityId best = 0;
    uint64_t best_max = 0;
    uint64_t best_taken = 0;
    for (const auto& [id, taken] : fight.enemies) {
        const Actor* a = m_registry.find(id);
        const uint64_t max_hp = a != nullptr ? a->max_hp : 0;
        const bool better = best == 0 || max_hp > best_max ||
                            (max_hp == best_max && (taken > best_taken || (taken == best_taken && id < best)));
        if (better) {
            best = id;
            best_max = max_hp;
            best_taken = taken;
        }
    }

    BossSummary b;
    if (best == 0) return b;
    b.id = best;
    b.damage_taken = best_taken;
    b.max_hp = best_max;
    b.is_boss = best_max >= m_settings.boss_min_max_hp && best_max > 0;
    if (const Actor* a = m_registry.find(best)) {
        b.npc_code = a->npc_code;
        b.killed = a->dead;
        if (a->has_live_hp) {
            b.hp = a->live_hp;
            b.hp_known = true;
            b.hp_exact = true;
        } else if (a->has_hp) {
            const auto seen = m_damage_since_spawn.find(best);
            const uint64_t taken = seen == m_damage_since_spawn.end() ? 0 : seen->second;
            b.hp = taken >= a->spawn_hp ? 0 : a->spawn_hp - taken;
            b.hp_known = true;
        }
        if (b.killed) b.hp = 0;
    }
    b.label = target_label(b.npc_code, b.id);
    return b;
}

FightSummary EncounterEngine::summarize(const Fight& fight, bool live, EndReason reason) const {
    FightSummary s;
    s.id = fight.id;
    s.map_id = fight.map_id;
    s.visit_ts = fight.visit_ts;
    s.start_ts = fight.start_ts;
    s.end_ts = fight.last_hit_ts;
    s.live = live;
    s.end_reason = reason;
    const double span = to_seconds(fight.last_hit_ts > fight.start_ts ? fight.last_hit_ts - fight.start_ts : 0);
    s.duration_s = std::max(span, kMinDurationSeconds);
    s.boss = boss_of(fight);

    // One row per character, whatever ids it had; an id named after it hit joins
    // its character. Unnamed ids stay apart.
    struct Group {
        EntityId id{0};
        CharacterId who{0};
        bool local{false};
        Accum acc;
    };
    std::unordered_map<uint64_t, Group> groups;
    for (const auto& [key, acc] : fight.players) {
        const CharacterId who = key.who != 0 ? key.who : m_registry.character_of(key.id);
        const uint64_t group_key = who != 0 ? (uint64_t{1} << 32) | who : key.id;
        Group& g = groups.try_emplace(group_key, Group{key.id, who, false, {}}).first->second;
        // The lowest id, so a row keeps its id from one snapshot to the next.
        g.id = std::min(g.id, key.id);
        g.local = g.local || key.id == m_registry.local_player();
        g.acc.merge(acc);
    }
    for (const auto& [group_key, g] : groups) {
        const EntityId id = g.id;
        const Accum& acc = g.acc;
        CombatantTotals row;
        row.id = id;
        if (const Character* c = m_registry.character(g.who)) {
            row.name = c->name;
            row.class_code = c->class_code;
            row.cls = c->cls != AionClass::Unknown ? c->cls : m_registry.class_of(id);
            row.is_local = g.local || g.who == m_registry.local_character();
            row.in_party = m_registry.character_in_party(g.who);
        } else {
            row.name = m_registry.display_name(id);
            row.cls = m_registry.class_of(id);
            row.is_local = g.local;
            row.in_party = m_registry.in_party(id);
        }
        row.damage = acc.damage;
        row.dot_damage = acc.dot_damage;
        row.max_hit = acc.max_hit;
        row.hits = acc.hits;
        row.dps = static_cast<double>(acc.damage) / s.duration_s;
        row.skills = sorted_values(acc.skills, [](const SkillTotals& k) { return k.damage; });
        row.healing = acc.healing;
        row.heal_hits = acc.heal_hits;
        row.hps = static_cast<double>(acc.healing) / s.duration_s;
        row.heal_skills = sorted_values(acc.heals, [](const SkillAmount& k) { return k.amount; });
        row.damage_taken = acc.taken;
        row.taken = acc.taken_hits;
        row.taken_by = sorted_values(acc.taken_by, [](const SkillAmount& k) { return k.amount; });
        s.total_damage += acc.damage;
        s.total_healing += acc.healing;
        s.combatants.push_back(std::move(row));
    }
    for (auto& row : s.combatants) {
        row.share = s.total_damage == 0 ? 0.0 : static_cast<double>(row.damage) / static_cast<double>(s.total_damage);
    }
    std::sort(s.combatants.begin(), s.combatants.end(), [](const CombatantTotals& a, const CombatantTotals& b) {
        if (a.damage != b.damage) return a.damage > b.damage;
        return a.id < b.id;
    });
    s.total_dps = static_cast<double>(s.total_damage) / s.duration_s;

    const CaptureQuality& a = fight.counters_at_start;
    s.quality.gap_bytes = grew(m_counters.gap_bytes, a.gap_bytes);
    s.quality.resyncs = grew(m_counters.resyncs, a.resyncs);
    s.quality.bundle_errors = grew(m_counters.bundle_errors, a.bundle_errors);
    s.quality.dropped_packets = grew(m_counters.dropped_packets, a.dropped_packets);
    // Not spawn HP against damage seen: HP falls by several times the damage
    // records' numbers, so that would call every boss partial.
    if (s.boss.killed && s.boss.is_boss) {
        if (const auto loss = m_hp_loss.find(s.boss.id); loss != m_hp_loss.end() && loss->second.lost > 0) {
            const auto& l = loss->second;
            const auto seen = static_cast<double>(l.lost - l.lost_unseen);
            s.quality.partial = seen < m_settings.partial_damage_ratio * static_cast<double>(l.lost);
        }
    }
    return s;
}

FightSummary EncounterEngine::current() const {
    if (m_fight) return summarize(*m_fight, true, EndReason::None);
    if (!m_recent.empty()) return m_recent.back();
    return FightSummary{};
}

std::vector<FightSummary> EncounterEngine::take_finished() {
    std::vector<FightSummary> out;
    out.swap(m_finished);
    return out;
}

} // namespace aion2::meter
