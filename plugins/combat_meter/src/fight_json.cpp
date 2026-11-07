#include "meter/fight_json.hpp"

#include "meter/fight_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
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
    j["block"] = h.blocks;
    j["multi"] = h.multi;
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
    // Written as "parry" before the bit was known to be a block.
    h.blocks = u64(j.contains("block") ? j["block"] : j["parry"]);
    h.multi = u64(j["multi"]);
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


/// Most of each list a record's detail reads: a misread or hand-edited file stops
/// here, never runs away with memory.
constexpr size_t kMaxTimelineRows = 256;
constexpr size_t kMaxEffectRows = 4096;
constexpr size_t kMaxBuffWindows = 4096;
/// Most skills one player's casts list: a class has fewer than 60.
constexpr size_t kMaxCastSkills = 512;

/// Milliseconds as written: seconds kept to the millisecond.
[[nodiscard]] uint64_t to_ms(double seconds) {
    return seconds > 0.0 ? static_cast<uint64_t>(std::llround(seconds * 1000.0)) : 0;
}
[[nodiscard]] double from_ms(const JsonValue& v) { return static_cast<double>(u64(v)) / 1000.0; }

template <typename T, typename F>
std::vector<T> bounded_vector_of(const JsonValue& j, size_t most, F from_json) {
    std::vector<T> out;
    for (const JsonValue& item : j.as_array()) {
        if (out.size() >= most) break;
        out.push_back(from_json(item));
    }
    return out;
}

/// One field of every bin, its trailing zeros cut: a number each second.
JsonValue series_to_json(const std::vector<TimelineBin>& bins, uint32_t TimelineBin::*field) {
    size_t n = bins.size();
    while (n > 0 && bins[n - 1].*field == 0) --n;
    JsonValue::ArrayType out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) out.emplace_back(bins[i].*field);
    return JsonValue(std::move(out));
}

void series_from_json(const JsonValue& j, std::vector<TimelineBin>& bins, uint32_t TimelineBin::*field) {
    const auto& values = j.as_array();
    const size_t n = std::min(values.size(), kTimelineMaxSeconds);
    if (bins.size() < n) bins.resize(n);
    for (size_t i = 0; i < n; ++i) bins[i].*field = u32(values[i]);
}

/// [code, casts, first ms, last ms]: one line, by the printer's rule for an array of
/// numbers. The first can be below 0 (the opening cast, before the first hit).
JsonValue cast_to_json(const SkillCasts& c) {
    JsonValue::ArrayType a;
    a.emplace_back(c.code);
    a.emplace_back(c.casts);
    a.emplace_back(static_cast<int64_t>(std::llround(c.first_s * 1000.0)));
    a.emplace_back(static_cast<int64_t>(std::llround(c.last_s * 1000.0)));
    return JsonValue(std::move(a));
}

SkillCasts cast_from_json(const JsonValue& j) {
    const auto& a = j.as_array();
    const auto at = [&](size_t i) -> const JsonValue& {
        static const JsonValue kNone;
        return i < a.size() ? a[i] : kNone;
    };
    const auto ms = [](const JsonValue& v) {
        const double d = v.as_double(0.0);
        return std::isfinite(d) ? d / 1000.0 : 0.0;
    };
    SkillCasts c;
    c.code = u32(at(0));
    c.casts = u64(at(1));
    c.first_s = ms(at(2));
    c.last_s = std::max(ms(at(3)), c.first_s);
    return c;
}

JsonValue timeline_to_json(const FightTimeline& tl) {
    JsonValue j;
    j["seconds"] = tl.seconds;
    JsonValue::ArrayType rows;
    for (const TimelineRow& r : tl.rows) {
        JsonValue row;
        row["id"] = r.id;
        row["damage"] = series_to_json(r.bins, &TimelineBin::damage);
        row["healing"] = series_to_json(r.bins, &TimelineBin::healing);
        row["taken"] = series_to_json(r.bins, &TimelineBin::taken);
        rows.push_back(std::move(row));
    }
    j["rows"] = JsonValue(std::move(rows));
    if (!tl.boss_hp.empty()) {
        JsonValue boss;
        boss["first_s"] = tl.boss_hp.first_s;
        JsonValue::ArrayType permille;
        permille.reserve(tl.boss_hp.permille.size());
        for (const uint16_t p : tl.boss_hp.permille) permille.emplace_back(p);
        boss["permille"] = JsonValue(std::move(permille));
        j["boss_hp"] = std::move(boss);
    }
    j["buffs"] = array_of(tl.buffs, [](const BuffWindow& w) {
        JsonValue b;
        b["key"] = w.key;
        b["buff"] = w.buff_id;
        if (w.source_skill != 0) b["skill"] = w.source_skill;
        b["caster"] = w.caster;
        b["class"] = std::string(game::class_name(w.caster_cls));
        b["begin_ms"] = to_ms(w.begin_s);
        b["end_ms"] = to_ms(w.end_s);
        return b;
    });
    return j;
}

FightTimeline timeline_from_json(const JsonValue& j) {
    FightTimeline tl;
    tl.seconds = std::min(u32(j["seconds"]), static_cast<uint32_t>(kTimelineMaxSeconds));
    tl.rows = bounded_vector_of<TimelineRow>(j["rows"], kMaxTimelineRows, [](const JsonValue& r) {
        TimelineRow row;
        row.id = u32(r["id"]);
        series_from_json(r["damage"], row.bins, &TimelineBin::damage);
        series_from_json(r["healing"], row.bins, &TimelineBin::healing);
        series_from_json(r["taken"], row.bins, &TimelineBin::taken);
        return row;
    });
    const JsonValue& boss = j["boss_hp"];
    tl.boss_hp.first_s = std::min(u32(boss["first_s"]), static_cast<uint32_t>(kTimelineMaxSeconds));
    for (const JsonValue& p : boss["permille"].as_array()) {
        if (tl.boss_hp.first_s + tl.boss_hp.permille.size() >= kTimelineMaxSeconds) break;
        tl.boss_hp.permille.push_back(static_cast<uint16_t>(std::min<uint64_t>(u64(p), 1000)));
    }
    tl.buffs = bounded_vector_of<BuffWindow>(j["buffs"], kMaxBuffWindows, [](const JsonValue& b) {
        BuffWindow w;
        w.key = u32(b["key"]);
        w.buff_id = u32(b["buff"]);
        w.source_skill = u32(b["skill"]);
        w.caster = b["caster"].as_string();
        w.caster_cls = game::class_from_name(b["class"].as_string());
        w.begin_s = from_ms(b["begin_ms"]);
        w.end_s = std::max(w.begin_s, from_ms(b["end_ms"]));
        return w;
    });
    return tl;
}

JsonValue death_to_json(const DeathRecord& d) {
    JsonValue j;
    j["player"] = d.player;
    j["name"] = d.name;
    j["class"] = std::string(game::class_name(d.cls));
    j["at_ms"] = to_ms(d.time_s);
    if (d.up()) j["up_after_ms"] = to_ms(d.up_after_s);
    j["recap"] = array_of(d.recap, [](const RecapEvent& e) {
        JsonValue r;
        r["before_ms"] = to_ms(e.before_s);
        r["kind"] = std::string(recap_kind_name(e.kind));
        r["skill"] = e.skill;
        if (e.source_npc != 0) r["npc"] = e.source_npc;
        if (!e.healer.empty()) r["from"] = e.healer;
        r["amount"] = e.amount;
        r["crit"] = e.crit;
        return r;
    });
    return j;
}

DeathRecord death_from_json(const JsonValue& j) {
    DeathRecord d;
    d.player = u32(j["player"]);
    d.name = j["name"].as_string();
    d.cls = game::class_from_name(j["class"].as_string());
    d.time_s = from_ms(j["at_ms"]);
    d.up_after_s = j.contains("up_after_ms") ? from_ms(j["up_after_ms"]) : -1.0;
    d.recap = bounded_vector_of<RecapEvent>(j["recap"], kRecapShown, [](const JsonValue& r) {
        RecapEvent e;
        e.before_s = from_ms(r["before_ms"]);
        e.kind = recap_kind_from_name(r["kind"].as_string());
        e.skill = u32(r["skill"]);
        e.source_npc = u32(r["npc"]);
        e.healer = r["from"].as_string();
        e.amount = u64(r["amount"]);
        e.crit = r["crit"].as_bool();
        return e;
    });
    return d;
}

JsonValue effect_to_json(const EffectUptimeRow& r) {
    JsonValue j;
    j["key"] = r.key;
    j["buff"] = r.buff_id;
    if (r.source_skill != 0) j["skill"] = r.source_skill;
    j["side"] = std::string(effect_side_name(r.side));
    j["target"] = r.target;
    j["target_name"] = r.target_name;
    if (r.target_cls != AionClass::Unknown) j["target_class"] = std::string(game::class_name(r.target_cls));
    j["caster"] = r.caster;
    if (r.caster_cls != AionClass::Unknown) j["caster_class"] = std::string(game::class_name(r.caster_cls));
    j["applications"] = r.applications;
    j["active_ms"] = to_ms(r.active_s);
    return j;
}

EffectUptimeRow effect_from_json(const JsonValue& j) {
    EffectUptimeRow r;
    r.key = u32(j["key"]);
    r.buff_id = u32(j["buff"]);
    r.source_skill = u32(j["skill"]);
    r.side = effect_side_from_name(j["side"].as_string());
    r.target = u32(j["target"]);
    r.target_name = j["target_name"].as_string();
    r.target_cls = game::class_from_name(j["target_class"].as_string());
    r.caster = j["caster"].as_string();
    r.caster_cls = game::class_from_name(j["caster_class"].as_string());
    r.applications = u32(j["applications"]);
    r.active_s = from_ms(j["active_ms"]);
    return r;
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
        r["class"] = std::string(game::class_name(c.cls));
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
        if (f.detail != nullptr) r["active_s"] = c.active_s;
        if (f.has_casts) r["casts"] = array_of(c.cast_skills, cast_to_json);
        rows.push_back(std::move(r));
    }
    j["combatants"] = std::move(rows);
    if (f.detail != nullptr) {
        j["timeline"] = timeline_to_json(f.detail->timeline);
        j["deaths"] = array_of(f.detail->deaths, death_to_json);
        j["effects"] = array_of(f.detail->effects, effect_to_json);
    }
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
        c.cls = game::class_from_name(r["class"].as_string());
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
        const double active = r["active_s"].as_double(0.0);
        c.active_s = std::isfinite(active) && active > 0.0 ? active : 0.0;
        // Saved before the meter counted casts: none, and the fight says so.
        if (r.contains("casts")) {
            f.has_casts = true;
            c.cast_skills = bounded_vector_of<SkillCasts>(r["casts"], kMaxCastSkills, cast_from_json);
            for (const SkillCasts& s : c.cast_skills) c.casts += s.casts;
        }
        f.combatants.push_back(std::move(c));
    }
    // Saved before the meter kept them: no detail at all, rather than an empty one.
    if (j.contains("timeline") || j.contains("deaths") || j.contains("effects")) {
        auto detail = std::make_shared<FightDetail>();
        detail->timeline = timeline_from_json(j["timeline"]);
        detail->deaths = bounded_vector_of<DeathRecord>(j["deaths"], kMaxDeathsPerFight, death_from_json);
        detail->effects = bounded_vector_of<EffectUptimeRow>(j["effects"], kMaxEffectRows, effect_from_json);
        f.detail = std::move(detail);
    }
    return f;
}

} // namespace aion2::meter
