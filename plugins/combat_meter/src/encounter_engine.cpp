#include "meter/encounter_engine.hpp"

#include "game/npc_names.hpp"
#include "game/skill_names.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
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
    if (npc_code != 0) return game::npc_label(npc_code);
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
    if (bins.size() < o.bins.size()) bins.resize(o.bins.size());
    for (size_t i = 0; i < o.bins.size(); ++i) {
        add_capped(bins[i].damage, o.bins[i].damage);
        add_capped(bins[i].healing, o.bins[i].healing);
        add_capped(bins[i].taken, o.bins[i].taken);
    }
    active_us += o.active_us;
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
                m_effects.on_heartbeat(e.server_ms, ts);
            } else if constexpr (std::is_same_v<T, proto::Identity>) {
                m_registry.on_identity(e);
            } else if constexpr (std::is_same_v<T, proto::Spawn>) {
                on_spawn(e, ts);
            } else if constexpr (std::is_same_v<T, proto::Death>) {
                on_death(e.id, ts);
            } else if constexpr (std::is_same_v<T, proto::MapLoad>) {
                on_map(e, event.connection, ts);
            } else if constexpr (std::is_same_v<T, proto::Damage>) {
                on_damage(e, ts);
            } else if constexpr (std::is_same_v<T, proto::Tick>) {
                on_tick(e, ts);
            } else if constexpr (std::is_same_v<T, proto::PartyRoster>) {
                m_registry.on_party(e);
            } else if constexpr (std::is_same_v<T, proto::RemainingHp>) {
                on_values(e, ts);
            } else if constexpr (std::is_same_v<T, proto::BuffApplied>) {
                m_effects.on_applied(e, ts, effect_tag(e.target, e.caster));
            } else if constexpr (std::is_same_v<T, proto::BuffRemoved>) {
                m_effects.on_removed(e, ts);
            }
        },
        event.event);
}

void EncounterEngine::note_hit_on(EntityId target) { m_hp_loss[target].hit_since = true; }

void EncounterEngine::on_spawn(const proto::Spawn& s, Micros ts) {
    const std::optional<uint64_t> before = exact_hp(s.id);
    m_registry.on_spawn(s);
    // Without HP the registry keeps the spawn HP it had, and the damage counted
    // against it with it.
    if (!s.has_hp) return;
    sample_hp(s.id, s.cur_hp, before, ts);
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
    book_heal(d.actor, d.target, amount, d.skill, true, ts);
    book_taken(d.actor, d.target, amount, d.skill, &d, ts);
}

void EncounterEngine::on_tick(const proto::Tick& t, Micros ts) {
    if (t.amount == 0 || t.actor == 0 || t.target == 0) return;
    // A heal settles no side: healers and their targets are on one.
    if (t.heal) {
        book_heal(t.actor, t.target, t.amount, t.skill, false, ts);
        return;
    }
    note_hit_on(t.target);
    m_registry.observe_hit(t.actor, t.target);
    m_damage_since_spawn[m_registry.resolve(t.target)] += t.amount;
    credit(t.actor, t.target, t.amount, t.skill, nullptr, ts);
    book_taken(t.actor, t.target, t.amount, t.skill, nullptr, ts);
}

bool EncounterEngine::counts_in_fight(EntityId player) const {
    if (!m_fight || !m_registry.in_party(player)) return false;
    if (m_registry.has_roster() || player == m_registry.local_player()) return true;
    return m_fight->players.contains(row_key(player));
}

void EncounterEngine::book_heal(EntityId actor, EntityId target, uint64_t amount, uint32_t skill, bool record,
                                Micros ts) {
    if (!m_fight) return;
    const EntityId healer = m_registry.resolve(actor);
    if (m_registry.side(healer) != Side::Player || m_registry.side(m_registry.resolve(target)) != Side::Player) return;
    // Not a summon's record: a spirit's to itself, as it spawned, carried 100,000.
    if (record && actor != healer) return;
    // A record between two players may be a PvP hit, not a heal: only one landing
    // on the party counts. Heal ticks are heals, wherever they land.
    if (record && !m_registry.in_party(target)) return;
    if (!counts_in_fight(healer)) return;
    m_registry.on_skill_class(healer, game::class_of_skill(skill));
    Accum& acc = m_fight->players[row_key(healer)];
    acc.healing += amount;
    ++acc.heal_hits;
    const uint32_t key = game::skill_key(skill);
    SkillAmount& s = acc.heals[key];
    s.code = key;
    book(s, amount);
    if (TimelineBin* bin = timeline_bin(acc.bins, m_fight->start_ts, ts)) add_capped(bin->healing, amount);
    // The healed player's recap: a party member's own, not a summon's.
    if (m_registry.resolve(target) == target && m_registry.in_party(target)) {
        push_recap(target, RecapSample{ts, record ? RecapKind::Heal : RecapKind::HealTick, key, 0, healer, amount, false});
    }
}

void EncounterEngine::book_taken(EntityId actor, EntityId target, uint64_t amount, uint32_t skill,
                                 const proto::Damage* direct, Micros ts) {
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
    const uint32_t key = game::skill_key(skill);
    SkillAmount& s = acc.taken_by[(static_cast<uint64_t>(key) << 32) | npc];
    s.code = key;
    s.source_npc = npc;
    book(s, amount);
    if (TimelineBin* bin = timeline_bin(acc.bins, m_fight->start_ts, ts)) add_capped(bin->taken, amount);
    push_recap(victim, RecapSample{ts, direct != nullptr ? RecapKind::Hit : RecapKind::Dot, key, npc, attacker, amount,
                                   direct != nullptr && direct->crit});
}

void EncounterEngine::credit(EntityId actor, EntityId target, uint64_t amount, uint32_t skill,
                             const proto::Damage* direct, Micros ts) {
    const EntityId player = m_registry.resolve(actor);
    const EntityId enemy = m_registry.resolve(target);
    if (m_registry.side(player) != Side::Player || m_registry.side_or_guess(enemy) != Side::Enemy) return;
    m_registry.on_skill_class(player, game::class_of_skill(skill));
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
        // What is already on the party and the enemies counts from here.
        m_effects.begin_fight(ts, [this](EntityId on, EntityId from) { return effect_tag(on, from); });
    }

    Fight& fight = *m_fight;
    Accum& acc = fight.players[row_key(player)];
    acc.damage += amount;
    acc.max_hit = std::max(acc.max_hit, amount);
    const uint32_t key = game::skill_key(skill);
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
    if (TimelineBin* bin = timeline_bin(acc.bins, fight.start_ts, ts)) add_capped(bin->damage, amount);
    // Only the player's own hand proves them alive, and only it counts as time spent
    // hitting: their DoTs and summons keep going after they die.
    if (direct != nullptr && actor == player) {
        Micros& last = fight.last_direct[player];
        if (last != 0 && ts >= last && ts - last <= kActiveGap) acc.active_us += ts - last;
        last = std::max(last, ts);
        m_registry.on_revive(player);
        note_alive(player, ts);
    }
}

void EncounterEngine::on_values(const proto::RemainingHp& v, Micros ts) {
    if (v.self) m_registry.on_self_values(v.id);
    if (!v.has_hp) return;
    const std::optional<uint64_t> before = exact_hp(v.id);
    m_registry.on_hp(v.id, v.hp);
    m_hp_loss[v.id].observe(v.hp);
    sample_hp(v.id, v.hp, before, ts);
    // HP reaching 0 is the death; 42 36 follows seconds later, with the corpse. A
    // player gets no 42 36: HP 0 is all the server says, and HP above 0 that they
    // are up.
    if (v.hp == 0) {
        on_death(v.id, ts);
    } else {
        note_alive(v.id, ts);
    }
}

void EncounterEngine::on_death(EntityId id, Micros ts) {
    m_registry.on_death(id);
    m_effects.on_death(id, ts);
    if (!m_fight) return;
    const EntityId who = m_registry.resolve(id);
    const BossSummary boss = boss_of(*m_fight);
    if (boss.present() && boss.is_boss && boss.id == who && !m_fight->boss_killed) {
        m_fight->boss_killed = true;
        m_fight->kill_ts = ts;
        return;
    }
    record_death(id, ts);
    if (m_settings.wipe_detection && m_registry.side(who) == Side::Player && party_wiped(*m_fight)) {
        finish(EndReason::Wipe, ts);
    }
}

void EncounterEngine::on_map(const proto::MapLoad& m, uint32_t connection, Micros ts) {
    // Entering a dungeon opens a connection to its server, so running it again,
    // straight back in, loads the same map id on a new one: each run is its own
    // visit. The same map on the same connection is a teleport or a revive within
    // the run: the fight and the visit go on, though players may have new ids.
    const bool reload = m.map_id == m_map_id && connection != 0 && connection == m_map_connection;
    m_map_connection = connection;
    if (reload) {
        m_registry.on_map_reload();
        m_effects.on_map_load(ts);
        return;
    }
    if (m_fight) finish(EndReason::MapChange, ts);
    m_effects.on_map_load(ts);
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

void EncounterEngine::tick(Micros now) {
    check_end(now);
    m_effects.prune(now);
}

void EncounterEngine::end_fight(EndReason reason, Micros now) {
    if (m_fight) finish(reason, now);
}

void EncounterEngine::finish(EndReason reason, Micros) {
    if (!m_fight) return;
    FightSummary s = summarize(*m_fight, false, reason);
    m_fight.reset();
    m_effects.end_fight();
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
        Group& g = groups.try_emplace(group_key(key.id, key.who), Group{key.id, who, false, {}}).first->second;
        // The lowest id, so a row keeps its id from one snapshot to the next.
        g.id = std::min(g.id, key.id);
        g.local = g.local || key.id == m_registry.local_player();
        g.acc.merge(acc);
    }
    std::unordered_map<uint64_t, EntityId> row_ids;
    std::vector<TimelineRow> timeline_rows;
    timeline_rows.reserve(groups.size());
    for (const auto& [gk, g] : groups) {
        const EntityId id = g.id;
        const Accum& acc = g.acc;
        row_ids[gk] = id;
        timeline_rows.push_back(TimelineRow{id, acc.bins});
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
        row.active_s = to_seconds(acc.active_us);
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
    s.detail = build_detail(fight, s, row_ids, std::move(timeline_rows));
    return s;
}

std::optional<uint64_t> EncounterEngine::exact_hp(EntityId id) const {
    const Actor* a = m_registry.find(id);
    if (a == nullptr) return std::nullopt;
    if (a->has_live_hp) return a->live_hp;
    if (a->has_hp) return a->spawn_hp;
    return std::nullopt;
}

void EncounterEngine::sample_hp(EntityId id, uint64_t hp, std::optional<uint64_t> before, Micros ts) {
    if (!m_fight || m_registry.side_or_guess(id) != Side::Enemy) return;
    const Micros start = m_fight->start_ts;
    const uint64_t second = ts > start ? (ts - start) / kMicrosPerSecond : 0;
    if (second >= kTimelineMaxSeconds) return;
    auto it = m_fight->enemy_hp.find(id);
    if (it == m_fight->enemy_hp.end()) {
        if (m_fight->enemy_hp.size() >= kMaxHpSeries) return;
        it = m_fight->enemy_hp.emplace(id, std::vector<Fight::HpSample>{}).first;
    }
    auto& series = it->second;
    const auto at = static_cast<uint32_t>(second);
    // The first figure in a fight: the one the meter had before stood at its start.
    if (series.empty() && at > 0 && before) series.push_back(Fight::HpSample{0, *before});
    if (!series.empty() && series.back().second == at) {
        series.back().hp = hp;
    } else {
        series.push_back(Fight::HpSample{at, hp});
    }
}

void EncounterEngine::push_recap(EntityId target, const RecapSample& sample) {
    if (!m_fight) return;
    auto it = m_fight->recaps.find(target);
    if (it == m_fight->recaps.end()) {
        if (m_fight->recaps.size() >= kMaxRecapRings) return;
        it = m_fight->recaps.emplace(target, RecapRing{}).first;
    }
    it->second.push(sample);
}

void EncounterEngine::record_death(EntityId id, Micros ts) {
    if (!m_fight || m_fight->deaths.size() >= kMaxDeathsPerFight) return;
    // A party member's own: a summon's HP is its own, and strangers stay out.
    if (m_registry.resolve(id) != id || m_registry.side(id) != Side::Player || !counts_in_fight(id)) return;
    const CharacterId who = m_registry.character_of(id);
    for (const Fight::Death& d : m_fight->deaths) {
        // Down already: a second 0 is the same death.
        if (d.up_ts == 0 && (d.key.id == id || (who != 0 && d.key.who == who))) return;
    }
    Fight::Death d;
    d.key = RowKey{id, who};
    d.ts = ts;
    if (const auto ring = m_fight->recaps.find(id); ring != m_fight->recaps.end()) {
        for (const RecapSample& sample : recap_before(ring->second, ts)) {
            RecapEvent e;
            e.before_s = to_seconds(ts - sample.ts);
            e.kind = sample.kind;
            e.skill = sample.skill;
            e.source_npc = sample.source_npc;
            e.amount = sample.amount;
            e.crit = sample.crit;
            if (!e.damage() && sample.source != 0) e.healer = player_label(sample.source, 0).first;
            d.recap.push_back(std::move(e));
        }
    }
    m_fight->deaths.push_back(std::move(d));
}

void EncounterEngine::note_alive(EntityId id, Micros ts) {
    if (!m_fight) return;
    const CharacterId who = m_registry.character_of(id);
    for (auto it = m_fight->deaths.rbegin(); it != m_fight->deaths.rend(); ++it) {
        if (it->up_ts != 0) continue;
        if (it->key.id == id || (who != 0 && it->key.who == who)) {
            it->up_ts = std::max(ts, it->ts);
            return;
        }
    }
}

std::optional<EffectTag> EncounterEngine::effect_tag(EntityId target, EntityId caster) const {
    if (!m_fight || target == 0) return std::nullopt;
    EffectTag t;
    t.target = target;
    t.caster = caster != 0 ? m_registry.resolve(caster) : 0;
    t.caster_who = t.caster != 0 ? m_registry.character_of(t.caster) : 0;
    const Side from = t.caster != 0 ? m_registry.side_or_guess(t.caster) : Side::Unknown;
    // On a party member: the party by the roster, or by the map (Party). A summon's
    // effects are its own.
    if (m_registry.resolve(target) == target && m_registry.side(target) == Side::Player &&
        m_registry.in_party(target)) {
        t.side = from == Side::Enemy ? EffectSide::PartyDebuff : EffectSide::PartyBuff;
        t.target_who = m_registry.character_of(target);
        return t;
    }
    // A party member's, or their summon's, on an enemy.
    if (from == Side::Player && m_registry.in_party(t.caster) && m_registry.side_or_guess(target) == Side::Enemy) {
        t.side = EffectSide::OnEnemy;
        return t;
    }
    return std::nullopt;
}

uint64_t EncounterEngine::group_key(EntityId id, CharacterId who) const {
    const CharacterId w = who != 0 ? who : m_registry.character_of(id);
    return w != 0 ? (uint64_t{1} << 32) | w : id;
}

std::pair<std::string, AionClass> EncounterEngine::player_label(EntityId id, CharacterId who) const {
    const CharacterId w = who != 0 ? who : m_registry.character_of(id);
    if (const Character* c = m_registry.character(w)) {
        return {c->name, c->cls != AionClass::Unknown ? c->cls : m_registry.class_of(id)};
    }
    return {m_registry.display_name(id), m_registry.class_of(id)};
}

std::shared_ptr<const FightDetail> EncounterEngine::build_detail(const Fight& fight, const FightSummary& s,
                                                                 const std::unordered_map<uint64_t, EntityId>& row_ids,
                                                                 std::vector<TimelineRow> rows) const {
    auto detail = std::make_shared<FightDetail>();
    const Micros start = fight.start_ts;
    const Micros end = std::max(fight.last_hit_ts, start);
    const auto since_start = [&](Micros ts) { return to_seconds(ts > start ? ts - start : 0); };
    const auto row_of = [&](EntityId id) -> const CombatantTotals* {
        const auto it = std::find_if(s.combatants.begin(), s.combatants.end(),
                                     [&](const CombatantTotals& c) { return c.id == id; });
        return it == s.combatants.end() ? nullptr : &*it;
    };

    // How long the timeline runs: the fight, and whatever its rows, its deaths and the
    // boss's last figure (its HP 0) booked after the last hit.
    size_t seconds = static_cast<size_t>((end - start + kMicrosPerSecond - 1) / kMicrosPerSecond);
    for (const TimelineRow& r : rows) seconds = std::max(seconds, r.bins.size());

    for (const Fight::Death& d : fight.deaths) {
        DeathRecord rec;
        const uint64_t gk = group_key(d.key.id, d.key.who);
        const auto row = row_ids.find(gk);
        const CombatantTotals* totals = row != row_ids.end() ? row_of(row->second) : nullptr;
        rec.player = totals != nullptr ? totals->id : d.key.id;
        if (totals != nullptr) {
            rec.name = totals->name;
            rec.cls = totals->cls;
        } else {
            std::tie(rec.name, rec.cls) = player_label(d.key.id, d.key.who);
        }
        rec.time_s = since_start(d.ts);
        rec.up_after_s = d.up_ts != 0 ? to_seconds(d.up_ts - d.ts) : -1.0;
        rec.recap = d.recap;
        seconds = std::max(seconds, static_cast<size_t>(rec.time_s) + 1);
        detail->deaths.push_back(std::move(rec));
    }

    const std::vector<Fight::HpSample>* boss_hp = nullptr;
    if (s.boss.present() && s.boss.max_hp > 0) {
        if (const auto it = fight.enemy_hp.find(s.boss.id); it != fight.enemy_hp.end() && !it->second.empty()) {
            boss_hp = &it->second;
            if (s.boss.killed) {
                const auto zero = std::find_if(boss_hp->begin(), boss_hp->end(),
                                               [](const Fight::HpSample& h) { return h.hp == 0; });
                if (zero != boss_hp->end()) seconds = std::max<size_t>(seconds, zero->second + size_t{1});
            }
        }
    }
    seconds = std::min(seconds, kTimelineMaxSeconds);

    FightTimeline& tl = detail->timeline;
    tl.seconds = static_cast<uint32_t>(seconds);
    std::sort(rows.begin(), rows.end(), [](const TimelineRow& a, const TimelineRow& b) { return a.id < b.id; });
    tl.rows = std::move(rows);
    if (boss_hp != nullptr && boss_hp->front().second < seconds) {
        tl.boss_hp.first_s = boss_hp->front().second;
        tl.boss_hp.permille.reserve(seconds - tl.boss_hp.first_s);
        size_t next = 0;
        uint64_t hp = boss_hp->front().hp;
        for (size_t sec = tl.boss_hp.first_s; sec < seconds; ++sec) {
            while (next < boss_hp->size() && (*boss_hp)[next].second <= sec) hp = (*boss_hp)[next++].hp;
            uint64_t pm = std::min<uint64_t>(1000, hp * 1000 / s.boss.max_hp);
            // Standing is never 0.
            if (pm == 0 && hp > 0) pm = 1;
            tl.boss_hp.permille.push_back(static_cast<uint16_t>(pm));
        }
    }

    // Effects: one row per side, target, effect and caster; stacks count once.
    struct Agg {
        EffectUptimeRow row;
        std::vector<TimeSpan> spans;
        std::vector<uint64_t> targets;
    };
    std::map<std::tuple<int, uint64_t, uint32_t, uint64_t>, Agg> by_row;
    for (const EffectSpan& sp : m_effects.spans()) {
        const EffectTag& t = sp.tag;
        uint64_t target_key = t.target;
        EntityId target_row = t.target;
        if (t.side == EffectSide::OnEnemy) {
            // Only the enemies the party fought.
            if (!fight.enemies.contains(t.target)) continue;
        } else {
            // A party member in the fight; others were strangers.
            target_key = group_key(t.target, t.target_who);
            const auto row = row_ids.find(target_key);
            if (row == row_ids.end()) continue;
            target_row = row->second;
        }
        const uint64_t caster_key =
            t.caster == 0 ? 0 : (t.side == EffectSide::PartyDebuff ? t.caster : group_key(t.caster, t.caster_who));
        const uint32_t key = game::effect_key(sp.buff_id, sp.source_skill);
        Agg& a = by_row[{static_cast<int>(t.side), target_key, key, caster_key}];
        if (a.row.applications == 0) {
            EffectUptimeRow& r = a.row;
            r.key = key;
            r.buff_id = sp.buff_id;
            r.source_skill = sp.source_skill;
            r.side = t.side;
            r.target = target_row;
            if (t.side == EffectSide::OnEnemy) {
                const Actor* e = m_registry.find(t.target);
                r.target_name = target_label(e != nullptr ? e->npc_code : 0, t.target);
            } else if (const CombatantTotals* totals = row_of(target_row)) {
                r.target_name = totals->name;
                r.target_cls = totals->cls;
            }
            if (t.caster != 0 && t.side == EffectSide::PartyDebuff) {
                const Actor* c = m_registry.find(t.caster);
                r.caster = target_label(c != nullptr ? c->npc_code : 0, t.caster);
            } else if (t.caster != 0) {
                std::tie(r.caster, r.caster_cls) = player_label(t.caster, t.caster_who);
            }
        }
        ++a.row.applications;
        a.spans.emplace_back(sp.begin, sp.end);
    }
    // Party-wide buffs, for the timeline's bands: one effect from one caster on two
    // or more party members, merged over all of them.
    struct Band {
        uint32_t buff_id{0};
        uint32_t source_skill{0};
        std::string caster;
        AionClass caster_cls{AionClass::Unknown};
        std::vector<uint64_t> targets;
        std::vector<TimeSpan> spans;
    };
    std::map<std::pair<uint32_t, uint64_t>, Band> bands;
    for (auto& [k, a] : by_row) {
        a.row.active_s = covered_seconds(a.spans, start, end);
        if (a.row.active_s <= 0.0) continue;
        if (a.row.side == EffectSide::PartyBuff && std::get<3>(k) != 0) {
            Band& b = bands[{a.row.key, std::get<3>(k)}];
            b.buff_id = a.row.buff_id;
            b.source_skill = a.row.source_skill;
            b.caster = a.row.caster;
            b.caster_cls = a.row.caster_cls;
            b.targets.push_back(std::get<1>(k));
            b.spans.insert(b.spans.end(), a.spans.begin(), a.spans.end());
        }
        detail->effects.push_back(std::move(a.row));
    }
    std::sort(detail->effects.begin(), detail->effects.end(), [](const EffectUptimeRow& a, const EffectUptimeRow& b) {
        if (a.side != b.side) return a.side < b.side;
        if (a.active_s != b.active_s) return a.active_s > b.active_s;
        return std::tie(a.key, a.target, a.caster) < std::tie(b.key, b.target, b.caster);
    });
    for (const auto& [k, b] : bands) {
        if (b.targets.size() < 2) continue;
        for (const auto& [from, to] : merged_spans(b.spans, start, end, kMicrosPerSecond)) {
            tl.buffs.push_back(BuffWindow{k.first, b.buff_id, b.source_skill, b.caster, b.caster_cls,
                                          since_start(from), since_start(to)});
        }
    }
    std::sort(tl.buffs.begin(), tl.buffs.end(), [](const BuffWindow& a, const BuffWindow& b) {
        return std::tie(a.begin_s, a.key, a.caster) < std::tie(b.begin_s, b.key, b.caster);
    });
    return detail;
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
