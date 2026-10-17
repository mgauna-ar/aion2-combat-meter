#include "meter/fight_json.hpp"

#include <cstdint>
#include <string>

namespace aion2::meter {

using config::JsonValue;

namespace {

[[nodiscard]] uint64_t u64(const JsonValue& v) { return json_u64(v); }

[[nodiscard]] uint32_t u32(const JsonValue& v) {
    const uint64_t x = u64(v);
    return x > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(x);
}

JsonValue hits_to_json(const HitCounts& h) {
    JsonValue j;
    j["hits"] = h.hits;
    j["crits"] = h.crits;
    j["back"] = h.back;
    j["front"] = h.front;
    j["parry"] = h.parry;
    j["perfect"] = h.perfect;
    j["double"] = h.doubles;
    j["heavy"] = h.heavy;
    return j;
}

HitCounts hits_from_json(const JsonValue& j) {
    HitCounts h;
    h.hits = u64(j["hits"]);
    h.crits = u64(j["crits"]);
    h.back = u64(j["back"]);
    h.front = u64(j["front"]);
    h.parry = u64(j["parry"]);
    h.perfect = u64(j["perfect"]);
    h.doubles = u64(j["double"]);
    h.heavy = u64(j["heavy"]);
    return h;
}

JsonValue skill_to_json(const SkillTotals& s) {
    JsonValue j;
    j["code"] = s.code;
    j["damage"] = s.damage;
    j["tick_damage"] = s.tick_damage;
    j["ticks"] = s.ticks;
    j["min"] = s.min_hit;
    j["max"] = s.max_hit;
    j["hits"] = hits_to_json(s.hits);
    return j;
}

SkillTotals skill_from_json(const JsonValue& j) {
    SkillTotals s;
    s.code = u32(j["code"]);
    s.damage = u64(j["damage"]);
    s.tick_damage = u64(j["tick_damage"]);
    s.ticks = u64(j["ticks"]);
    s.min_hit = u64(j["min"]);
    s.max_hit = u64(j["max"]);
    s.hits = hits_from_json(j["hits"]);
    return s;
}

JsonValue amount_to_json(const SkillAmount& s) {
    JsonValue j;
    j["code"] = s.code;
    if (s.source_npc != 0) j["npc"] = s.source_npc;
    j["amount"] = s.amount;
    j["hits"] = s.hits;
    j["max"] = s.max;
    return j;
}

SkillAmount amount_from_json(const JsonValue& j) {
    SkillAmount s;
    s.code = u32(j["code"]);
    s.source_npc = u32(j["npc"]);
    s.amount = u64(j["amount"]);
    s.hits = u64(j["hits"]);
    s.max = u64(j["max"]);
    return s;
}

template <typename T, typename F>
JsonValue array_of(const std::vector<T>& items, F to_json) {
    JsonValue::ArrayType out;
    out.reserve(items.size());
    for (const T& item : items) out.push_back(to_json(item));
    return JsonValue(std::move(out));
}

template <typename T, typename F>
std::vector<T> vector_of(const JsonValue& j, F from_json) {
    std::vector<T> out;
    for (const JsonValue& item : j.as_array()) out.push_back(from_json(item));
    return out;
}

} // namespace

uint64_t json_u64(const JsonValue& v) {
    const double d = v.as_double(0.0);
    // NaN fails the first test; a cast of a value past 2^64 would be undefined.
    if (!(d > 0.0)) return 0;
    if (d >= 18446744073709551616.0) return UINT64_MAX;
    return static_cast<uint64_t>(d);
}

bool readable_version(const JsonValue& j) {
    const int v = j["v"].as_int(0);
    return v >= kOldestFightRecordVersion && v <= kFightRecordVersion;
}

JsonValue fight_to_json(const FightSummary& f) {
    JsonValue j;
    j["v"] = kFightRecordVersion;
    j["id"] = f.id;
    j["map_id"] = f.map_id;
    j["visit_ts"] = f.visit_ts;
    j["start_ts"] = f.start_ts;
    j["end_ts"] = f.end_ts;
    j["duration_s"] = f.duration_s;
    j["end_reason"] = std::string(end_reason_name(f.end_reason));
    j["total_damage"] = f.total_damage;
    j["total_dps"] = f.total_dps;
    j["total_healing"] = f.total_healing;

    JsonValue boss;
    boss["id"] = f.boss.id;
    boss["npc_code"] = f.boss.npc_code;
    boss["label"] = f.boss.label;
    boss["max_hp"] = f.boss.max_hp;
    boss["hp"] = f.boss.hp;
    boss["hp_known"] = f.boss.hp_known;
    boss["hp_exact"] = f.boss.hp_exact;
    boss["is_boss"] = f.boss.is_boss;
    boss["killed"] = f.boss.killed;
    boss["damage_taken"] = f.boss.damage_taken;
    j["boss"] = std::move(boss);

    JsonValue quality;
    quality["gap_bytes"] = f.quality.gap_bytes;
    quality["resyncs"] = f.quality.resyncs;
    quality["bundle_errors"] = f.quality.bundle_errors;
    quality["dropped_packets"] = f.quality.dropped_packets;
    quality["partial"] = f.quality.partial;
    j["quality"] = std::move(quality);

    JsonValue::ArrayType rows;
    for (const auto& c : f.combatants) {
        JsonValue r;
        r["id"] = c.id;
        r["name"] = c.name;
        r["class"] = std::string(class_name(c.cls));
        r["class_code"] = c.class_code;
        r["is_local"] = c.is_local;
        r["in_party"] = c.in_party;
        r["damage"] = c.damage;
        r["dot_damage"] = c.dot_damage;
        r["max_hit"] = c.max_hit;
        r["dps"] = c.dps;
        r["share"] = c.share;
        r["hits"] = hits_to_json(c.hits);
        r["skills"] = array_of(c.skills, skill_to_json);
        r["healing"] = c.healing;
        r["heal_hits"] = c.heal_hits;
        r["hps"] = c.hps;
        r["heal_skills"] = array_of(c.heal_skills, amount_to_json);
        r["damage_taken"] = c.damage_taken;
        r["taken"] = hits_to_json(c.taken);
        r["taken_by"] = array_of(c.taken_by, amount_to_json);
        rows.push_back(std::move(r));
    }
    j["combatants"] = std::move(rows);
    return j;
}

std::optional<FightSummary> fight_from_json(const JsonValue& j) {
    if (!j.is_object() || !readable_version(j)) return std::nullopt;
    FightSummary f;
    f.id = u64(j["id"]);
    f.map_id = u32(j["map_id"]);
    f.visit_ts = u64(j["visit_ts"]);
    f.start_ts = u64(j["start_ts"]);
    f.end_ts = u64(j["end_ts"]);
    f.duration_s = j["duration_s"].as_double(0.0);
    f.end_reason = end_reason_from_name(j["end_reason"].as_string());
    f.total_damage = u64(j["total_damage"]);
    f.total_dps = j["total_dps"].as_double(0.0);
    f.total_healing = u64(j["total_healing"]);
    f.live = false;

    const JsonValue& boss = j["boss"];
    f.boss.id = u32(boss["id"]);
    f.boss.npc_code = u32(boss["npc_code"]);
    f.boss.label = boss["label"].as_string();
    f.boss.max_hp = u64(boss["max_hp"]);
    f.boss.hp = u64(boss["hp"]);
    f.boss.hp_known = boss["hp_known"].as_bool();
    f.boss.hp_exact = boss["hp_exact"].as_bool();
    f.boss.is_boss = boss["is_boss"].as_bool();
    f.boss.killed = boss["killed"].as_bool();
    f.boss.damage_taken = u64(boss["damage_taken"]);

    const JsonValue& q = j["quality"];
    f.quality.gap_bytes = u64(q["gap_bytes"]);
    f.quality.resyncs = u64(q["resyncs"]);
    f.quality.bundle_errors = u64(q["bundle_errors"]);
    f.quality.dropped_packets = u64(q["dropped_packets"]);
    f.quality.partial = q["partial"].as_bool();

    for (const JsonValue& r : j["combatants"].as_array()) {
        CombatantTotals c;
        c.id = u32(r["id"]);
        c.name = r["name"].as_string();
        c.cls = class_from_name(r["class"].as_string());
        c.class_code = u32(r["class_code"]);
        c.is_local = r["is_local"].as_bool();
        c.in_party = r["in_party"].as_bool();
        c.damage = u64(r["damage"]);
        c.dot_damage = u64(r["dot_damage"]);
        c.max_hit = u64(r["max_hit"]);
        c.dps = r["dps"].as_double(0.0);
        c.share = r["share"].as_double(0.0);
        c.hits = hits_from_json(r["hits"]);
        c.skills = vector_of<SkillTotals>(r["skills"], skill_from_json);
        c.healing = u64(r["healing"]);
        c.heal_hits = u64(r["heal_hits"]);
        c.hps = r["hps"].as_double(0.0);
        c.heal_skills = vector_of<SkillAmount>(r["heal_skills"], amount_from_json);
        c.damage_taken = u64(r["damage_taken"]);
        c.taken = hits_from_json(r["taken"]);
        c.taken_by = vector_of<SkillAmount>(r["taken_by"], amount_from_json);
        f.combatants.push_back(std::move(c));
    }
    return f;
}

} // namespace aion2::meter
