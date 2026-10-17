#include "meter/fight_json.hpp"

#include <cstdint>
#include <string>

namespace aion2::meter {

using config::JsonValue;

namespace {

[[nodiscard]] uint64_t u64(const JsonValue& v) {
    const double d = v.as_double(0.0);
    // NaN fails the first test (1e999 in a file reads as infinity); a cast of a
    // value past 2^64 would be undefined.
    if (!(d > 0.0)) return 0;
    if (d >= 18446744073709551616.0) return UINT64_MAX;
    return static_cast<uint64_t>(d);
}

[[nodiscard]] uint32_t u32(const JsonValue& v) {
    const uint64_t x = u64(v);
    return x > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(x);
}

JsonValue hits_to_json(const HitCounts& h) {
    JsonValue j;
    j["hits"] = h.hits;
    j["crits"] = h.crits;
    j["back"] = h.back;
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
    h.parry = u64(j["parry"]);
    h.perfect = u64(j["perfect"]);
    h.doubles = u64(j["double"]);
    h.heavy = u64(j["heavy"]);
    return h;
}

} // namespace

JsonValue fight_to_json(const FightSummary& f) {
    JsonValue j;
    j["v"] = kFightRecordVersion;
    j["id"] = f.id;
    j["map_id"] = f.map_id;
    j["start_ts"] = f.start_ts;
    j["end_ts"] = f.end_ts;
    j["duration_s"] = f.duration_s;
    j["end_reason"] = std::string(end_reason_name(f.end_reason));
    j["total_damage"] = f.total_damage;
    j["total_dps"] = f.total_dps;

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
        rows.push_back(std::move(r));
    }
    j["combatants"] = std::move(rows);
    return j;
}

std::optional<FightSummary> fight_from_json(const JsonValue& j) {
    if (!j.is_object() || j["v"].as_int(0) != kFightRecordVersion) return std::nullopt;
    FightSummary f;
    f.id = u64(j["id"]);
    f.map_id = u32(j["map_id"]);
    f.start_ts = u64(j["start_ts"]);
    f.end_ts = u64(j["end_ts"]);
    f.duration_s = j["duration_s"].as_double(0.0);
    f.end_reason = end_reason_from_name(j["end_reason"].as_string());
    f.total_damage = u64(j["total_damage"]);
    f.total_dps = j["total_dps"].as_double(0.0);
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
        f.combatants.push_back(std::move(c));
    }
    return f;
}

} // namespace aion2::meter
