#include "test_framework.hpp"
#include "file_test_support.hpp"
#include "meter_test_support.hpp"

#include "meter/fight_detail.hpp"
#include "meter/fight_json.hpp"
#include "meter/history_store.hpp"
#include "meter/pull_grouping.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>

using namespace aion2;
using namespace aion2::meter;
using namespace aion2::test::file_support;
using namespace aion2::test::meter_support;

namespace {

FightSummary sample_fight(uint64_t id, double start_s) {
    FightSummary f;
    f.id = id;
    f.map_id = 610010;
    f.start_ts = at_s(start_s);
    f.end_ts = at_s(start_s + 95);
    f.duration_s = 95.0;
    f.end_reason = EndReason::BossKilled;
    f.boss.id = 0x4100;
    f.boss.npc_code = 777;
    f.boss.label = "NPC #777";
    f.boss.max_hp = 5'000'000;
    f.boss.is_boss = true;
    f.boss.killed = true;
    f.boss.hp_known = true;
    f.boss.damage_taken = 5'000'000;
    CombatantTotals a;
    a.id = 100;
    a.name = "Ariel";
    a.cls = AionClass::Sorcerer;
    a.is_local = true;
    a.in_party = true;
    a.damage = 3'000'000;
    a.hits.hits = 120;
    a.hits.crits = 40;
    a.dps = 3'000'000 / 95.0;
    a.share = 0.6;
    CombatantTotals b;
    b.id = 200;
    b.name = "Lumiel";
    b.damage = 2'000'000;
    b.share = 0.4;
    f.combatants = {a, b};
    f.total_damage = 5'000'000;
    f.total_dps = 5'000'000 / 95.0;
    f.quality.resyncs = 2;
    return f;
}

/// A fight's detail with something of each kind in it.
std::shared_ptr<const FightDetail> sample_detail() {
    auto d = std::make_shared<FightDetail>();
    d->timeline.seconds = 95;
    TimelineRow me;
    me.id = 100;
    me.bins.resize(4);
    me.bins[0].damage = 53'214;
    me.bins[2].damage = 61'200;
    me.bins[1].taken = 4'000;
    me.bins[3].healing = 0;  // trailing zeros are cut and read back as nothing
    TimelineRow ally;
    ally.id = 200;
    ally.bins.resize(2);
    ally.bins[1].healing = 1'800;
    d->timeline.rows = {me, ally};
    d->timeline.boss_hp.first_s = 0;
    d->timeline.boss_hp.permille = {1000, 998, 991, 0};
    d->timeline.buffs.push_back(BuffWindow{18'190'000, 181'900'211, 18'190'021, "Lumiel", AionClass::Chanter, 1.0, 11.5});

    DeathRecord up;
    up.player = 200;
    up.name = "Lumiel";
    up.cls = AionClass::Chanter;
    up.time_s = 25.004;
    up.up_after_s = 4.0;
    up.recap.push_back(RecapEvent{2.7, RecapKind::Hit, 900'101, 777, {}, 4'000, false});
    up.recap.push_back(RecapEvent{0.7, RecapKind::HealTick, 16'770'000, 0, "Zikel", 1'800, false});
    up.recap.push_back(RecapEvent{0.0, RecapKind::Hit, 900'102, 777, {}, 60'000, true});
    DeathRecord down = up;
    down.time_s = 80.5;
    down.up_after_s = -1.0;
    down.recap.clear();
    d->deaths = {up, down};

    EffectUptimeRow buff;
    buff.key = 18'190'000;
    buff.buff_id = 181'900'211;
    buff.source_skill = 18'190'021;
    buff.side = EffectSide::PartyBuff;
    buff.target = 100;
    buff.target_name = "Ariel";
    buff.target_cls = AionClass::Sorcerer;
    buff.caster = "Lumiel";
    buff.caster_cls = AionClass::Chanter;
    buff.applications = 7;
    buff.active_s = 81.25;
    EffectUptimeRow curse;
    curse.key = 153'200'011;
    curse.buff_id = 153'200'011;
    curse.side = EffectSide::OnEnemy;
    curse.target = 0x4100;
    curse.target_name = "NPC #777";
    curse.caster = "Ariel";
    curse.caster_cls = AionClass::Sorcerer;
    curse.applications = 9;
    curse.active_s = 60.5;
    d->effects = {buff, curse};
    return d;
}

} // namespace

TEST_CASE(FightJson, RoundTrip) {
    const FightSummary f = sample_fight(7, 0);
    const auto back = fight_from_json(fight_to_json(f));
    TEST_ASSERT(back.has_value());
    TEST_ASSERT_EQ(back->id, uint64_t{7});
    TEST_ASSERT_EQ(back->start_ts, f.start_ts);
    TEST_ASSERT(back->end_reason == EndReason::BossKilled);
    TEST_ASSERT_EQ(back->boss.label, std::string("NPC #777"));
    TEST_ASSERT(back->boss.killed);
    TEST_ASSERT_EQ(back->combatants.size(), size_t{2});
    TEST_ASSERT_EQ(back->combatants[0].name, std::string("Ariel"));
    TEST_ASSERT(back->combatants[0].cls == AionClass::Sorcerer);
    TEST_ASSERT_EQ(back->combatants[0].hits.crits, uint64_t{40});
    TEST_ASSERT_EQ(back->quality.resyncs, uint64_t{2});
}

TEST_CASE(FightJson, NonFiniteNumbersReadSafely) {
    auto j = fight_to_json(sample_fight(1, 0));
    j["total_damage"] = std::numeric_limits<double>::infinity();
    j["start_ts"] = std::nan("");
    const auto back = fight_from_json(j);
    TEST_ASSERT(back.has_value());
    TEST_ASSERT_EQ(back->total_damage, UINT64_MAX);
    TEST_ASSERT_EQ(back->start_ts, Micros{0});
}

TEST_CASE(FightJson, OtherVersionIsRefused) {
    auto j = fight_to_json(sample_fight(1, 0));
    j["v"] = 99;
    TEST_ASSERT_FALSE(fight_from_json(j).has_value());
}

TEST_CASE(FightJson, DetailRoundTrips) {
    FightSummary f = sample_fight(3, 0);
    f.combatants[0].active_s = 70.125;
    f.detail = sample_detail();
    const auto back = fight_from_json(fight_to_json(f));
    TEST_ASSERT(back.has_value());
    TEST_ASSERT(back->detail != nullptr);
    TEST_ASSERT_NEAR(back->combatants[0].active_s, 70.125, 1e-9);
    // The trailing zero second is cut, and nothing else differs.
    FightDetail expected = *f.detail;
    expected.timeline.rows[0].bins.resize(3);
    TEST_ASSERT(back->detail->timeline == expected.timeline);
    TEST_ASSERT(back->detail->deaths == expected.deaths);
    TEST_ASSERT(back->detail->effects == expected.effects);
    TEST_ASSERT_FALSE(back->detail->deaths[1].up());
    TEST_ASSERT(back->detail->deaths[0].killing_blow()->crit);
}

TEST_CASE(FightJson, SeriesAreOneLineEach) {
    FightSummary f = sample_fight(3, 0);
    f.detail = sample_detail();
    const std::string text = fight_to_json(f).stringify();
    TEST_ASSERT(text.find("[53214,0,61200]") != std::string::npos);
}

TEST_CASE(FightJson, ARecordWithoutDetailReadsWithNone) {
    const auto back = fight_from_json(fight_to_json(sample_fight(1, 0)));
    TEST_ASSERT(back.has_value());
    TEST_ASSERT(back->detail == nullptr);
    TEST_ASSERT_NEAR(back->combatants[0].active_s, 0.0, 1e-9);
}

TEST_CASE(FightJson, DetailNumbersAreBounded) {
    FightSummary f = sample_fight(3, 0);
    f.detail = sample_detail();
    auto j = fight_to_json(f);
    config::JsonValue::ArrayType long_series(5000, config::JsonValue(1));
    j["timeline"]["rows"].as_array()[0]["damage"] = config::JsonValue(long_series);
    j["timeline"]["boss_hp"]["permille"] = config::JsonValue::ArrayType{config::JsonValue(5000), config::JsonValue(-3)};
    j["timeline"]["seconds"] = 1e12;
    j["combatants"].as_array()[0]["active_s"] = std::nan("");
    config::JsonValue::ArrayType deaths(300, j["deaths"].as_array()[0]);
    j["deaths"] = config::JsonValue(deaths);
    config::JsonValue::ArrayType recap(20, j["deaths"].as_array()[0]["recap"].as_array()[0]);
    j["deaths"].as_array()[0]["recap"] = config::JsonValue(recap);
    j["deaths"].as_array()[0]["at_ms"] = -50;

    const auto back = fight_from_json(j);
    TEST_ASSERT(back.has_value());
    const FightDetail& d = *back->detail;
    TEST_ASSERT_EQ(d.timeline.rows[0].bins.size(), kTimelineMaxSeconds);
    TEST_ASSERT_EQ(d.timeline.seconds, static_cast<uint32_t>(kTimelineMaxSeconds));
    TEST_ASSERT_EQ(d.timeline.boss_hp.permille[0], uint16_t{1000});
    TEST_ASSERT_EQ(d.timeline.boss_hp.permille[1], uint16_t{0});
    TEST_ASSERT_EQ(d.deaths.size(), kMaxDeathsPerFight);
    TEST_ASSERT_EQ(d.deaths[0].recap.size(), kRecapShown);
    TEST_ASSERT_NEAR(d.deaths[0].time_s, 0.0, 1e-9);
    TEST_ASSERT_NEAR(back->combatants[0].active_s, 0.0, 1e-9);
}

TEST_CASE(History, SaveLoadKeepsTheDetail) {
    ScratchDir dir("aion2_test_history_detail");
    HistoryStore store(dir.path / "history");
    FightSummary f = sample_fight(5, 0);
    f.detail = sample_detail();
    TEST_ASSERT_EQ(store.save(f), std::string());
    const auto index = store.load_index();
    TEST_ASSERT_EQ(index.size(), size_t{1});
    const auto back = store.load(index[0].file);
    TEST_ASSERT(back.has_value());
    TEST_ASSERT(back->detail != nullptr);
    TEST_ASSERT(back->detail->effects == f.detail->effects);
    TEST_ASSERT_EQ(back->detail->deaths.size(), size_t{2});
}

TEST_CASE(History, FileNameIsUtcStartAndId) {
    TEST_ASSERT_EQ(fight_file_name(sample_fight(12, 0)), std::string("20261016-120000Z-12.json"));
}

TEST_CASE(History, WorthSaving) {
    MeterSettings s;
    FightSummary f;
    f.duration_s = 5;
    TEST_ASSERT_FALSE(worth_saving(f, s));
    f.duration_s = 25;
    TEST_ASSERT(worth_saving(f, s));
    f.duration_s = 5;
    f.boss.is_boss = true;
    TEST_ASSERT(worth_saving(f, s));
}

TEST_CASE(History, WorthSavingOnlyInDungeons) {
    MeterSettings s;
    FightSummary f;
    f.boss.is_boss = true;
    const uint32_t dungeon = 600001;  // Krao Cave (Conquest)
    const uint32_t open_world = 400001;
    for (const uint32_t map : {dungeon, open_world, 0u}) {
        f.map_id = map;
        TEST_ASSERT(worth_saving(f, s));
    }
    s.save_only_dungeons = true;
    f.map_id = dungeon;
    TEST_ASSERT(worth_saving(f, s));
    f.map_id = open_world;
    TEST_ASSERT_FALSE(worth_saving(f, s));
    // No map load seen yet: not known to be a dungeon.
    f.map_id = 0;
    TEST_ASSERT_FALSE(worth_saving(f, s));
}

TEST_CASE(History, SaveLoadAndIndex) {
    ScratchDir dir("aion2_test_history");
    HistoryStore store(dir.path / "history");
    TEST_ASSERT_EQ(store.save(sample_fight(1, 0)), std::string());
    TEST_ASSERT_EQ(store.save(sample_fight(2, 600)), std::string());
    const auto index = store.load_index();
    TEST_ASSERT_EQ(index.size(), size_t{2});
    TEST_ASSERT_EQ(index[0].id, uint64_t{1});
    TEST_ASSERT_EQ(index[1].boss_label, std::string("NPC #777"));
    TEST_ASSERT_EQ(index[1].players, size_t{2});
    const auto fight = store.load(index[1].file);
    TEST_ASSERT(fight.has_value());
    TEST_ASSERT_EQ(fight->id, uint64_t{2});
}

TEST_CASE(History, SameSecondGetsASuffix) {
    ScratchDir dir("aion2_test_history_clash");
    HistoryStore store(dir.path);
    TEST_ASSERT_EQ(store.save(sample_fight(1, 0)), std::string());
    TEST_ASSERT_EQ(store.save(sample_fight(1, 0)), std::string());
    const auto index = store.load_index();
    TEST_ASSERT_EQ(index.size(), size_t{2});
    TEST_ASSERT(index[0].file != index[1].file);
}

TEST_CASE(History, TruncatedIndexLineIsSkipped) {
    ScratchDir dir("aion2_test_history_cut");
    HistoryStore store(dir.path);
    TEST_ASSERT_EQ(store.save(sample_fight(1, 0)), std::string());
    {
        std::ofstream out(store.index_path(), std::ios::app | std::ios::binary);
        out << R"({"v":1,"file":"2026)";  // a crash mid-append
    }
    TEST_ASSERT_EQ(store.load_index().size(), size_t{1});
}

TEST_CASE(History, MissingIndexIsRebuilt) {
    ScratchDir dir("aion2_test_history_rebuild");
    HistoryStore store(dir.path);
    TEST_ASSERT_EQ(store.save(sample_fight(1, 0)), std::string());
    TEST_ASSERT_EQ(store.save(sample_fight(2, 60)), std::string());
    std::filesystem::remove(store.index_path());
    const auto index = store.load_index();
    TEST_ASSERT_EQ(index.size(), size_t{2});
    TEST_ASSERT(std::filesystem::exists(store.index_path()));
}

TEST_CASE(History, PruneKeepsNewest) {
    ScratchDir dir("aion2_test_history_prune");
    HistoryStore store(dir.path);
    for (uint64_t i = 1; i <= 4; ++i) TEST_ASSERT_EQ(store.save(sample_fight(i, 100.0 * i)), std::string());
    TEST_ASSERT_EQ(store.prune(2), size_t{2});
    const auto index = store.load_index();
    TEST_ASSERT_EQ(index.size(), size_t{2});
    TEST_ASSERT_EQ(index[0].id, uint64_t{3});
    TEST_ASSERT_FALSE(std::filesystem::exists(dir.path / fight_file_name(sample_fight(1, 100))));
}

TEST_CASE(History, LoadRefusesPathsOutsideTheFolder) {
    ScratchDir dir("aion2_test_history_paths");
    HistoryStore store(dir.path);
    TEST_ASSERT_FALSE(store.load("../config.json").has_value());
    TEST_ASSERT_FALSE(store.load("/etc/passwd").has_value());
}

TEST_CASE(FightJson, BreakdownsRoundTrip) {
    FightSummary f = sample_fight(3, 0);
    f.visit_ts = at_s(-30);
    f.total_healing = 900;
    CombatantTotals& a = f.combatants[0];
    a.hits.front = 7;
    a.hits.multi = 5;
    a.hits.blocks = 2;
    SkillTotals judgment;
    judgment.code = 12240000;
    judgment.damage = 12300;
    judgment.tick_damage = 300;
    judgment.ticks = 1;
    judgment.min_hit = 4000;
    judgment.max_hit = 8000;
    judgment.hits.hits = 2;
    judgment.hits.crits = 1;
    judgment.hits.front = 1;
    a.skills = {judgment};
    a.healing = 900;
    a.heal_hits = 3;
    a.hps = 9.5;
    a.heal_skills = {SkillAmount{12350000, 0, 900, 3, 400}};
    a.damage_taken = 1050;
    a.taken.hits = 2;
    a.taken.crits = 1;
    a.taken_by = {SkillAmount{900100, 777, 1000, 2, 600}};

    const auto back = fight_from_json(fight_to_json(f));
    TEST_ASSERT(back.has_value());
    TEST_ASSERT_EQ(back->visit_ts, at_s(-30));
    TEST_ASSERT_EQ(back->total_healing, uint64_t{900});
    const CombatantTotals& b = back->combatants[0];
    TEST_ASSERT_EQ(b.hits.front, uint64_t{7});
    TEST_ASSERT_EQ(b.hits.multi, uint64_t{5});
    TEST_ASSERT_EQ(b.hits.blocks, uint64_t{2});
    TEST_ASSERT_EQ(b.skills.size(), size_t{1});
    TEST_ASSERT(b.skills[0] == judgment);
    TEST_ASSERT_EQ(b.healing, uint64_t{900});
    TEST_ASSERT_EQ(b.heal_hits, uint64_t{3});
    TEST_ASSERT_NEAR(b.hps, 9.5, 1e-9);
    TEST_ASSERT(b.heal_skills == a.heal_skills);
    TEST_ASSERT_EQ(b.damage_taken, uint64_t{1050});
    TEST_ASSERT(b.taken == a.taken);
    TEST_ASSERT(b.taken_by == a.taken_by);
}

TEST_CASE(FightJson, BlocksSavedAsParryStillLoad) {
    // Before the second capture the block bit was read as a parry and saved so.
    FightSummary f = sample_fight(1, 0);
    f.combatants[0].hits.blocks = 4;
    auto j = fight_to_json(f);
    for (auto& row : j["combatants"].as_array()) {
        auto& hits = row["hits"];
        hits["parry"] = hits["block"];
        hits.as_object().erase("block");
    }
    const auto back = fight_from_json(j);
    TEST_ASSERT(back.has_value());
    TEST_ASSERT_EQ(back->combatants[0].hits.blocks, uint64_t{4});
}

TEST_CASE(FightJson, VersionOneReadsWithoutBreakdowns) {
    auto j = fight_to_json(sample_fight(1, 0));
    j["v"] = 1;
    for (auto& r : j["combatants"].as_array()) {
        r.as_object().erase("skills");
        r.as_object().erase("heal_skills");
        r.as_object().erase("taken_by");
    }
    const auto back = fight_from_json(j);
    TEST_ASSERT(back.has_value());
    TEST_ASSERT(back->combatants[0].skills.empty());
    TEST_ASSERT_EQ(back->combatants[0].damage, uint64_t{3'000'000});
}

TEST_CASE(History, IndexKeepsTheVisitAndTheBossHpLeft) {
    ScratchDir dir("aion2_history_visit");
    HistoryStore store(dir.path);
    FightSummary wipe = sample_fight(1, 0);
    wipe.visit_ts = at_s(-60);
    wipe.boss.killed = false;
    wipe.boss.hp = 1'250'000;
    TEST_ASSERT(store.save(wipe).empty());
    TEST_ASSERT(store.save(sample_fight(2, 200)).empty());
    const auto entries = store.load_index();
    TEST_ASSERT_EQ(entries.size(), size_t{2});
    TEST_ASSERT_EQ(entries[0].visit_ts, at_s(-60));
    TEST_ASSERT(entries[0].boss_hp_known);
    TEST_ASSERT_NEAR(entries[0].boss_hp_left, 0.25, 1e-9);
    // A killed boss has no HP left to show.
    TEST_ASSERT_FALSE(entries[1].boss_hp_known);
}

TEST_CASE(History, ClearDeletesFightsOnly) {
    ScratchDir dir("aion2_history_clear");
    HistoryStore store(dir.path);
    TEST_ASSERT(store.save(sample_fight(1, 0)).empty());
    TEST_ASSERT(store.save(sample_fight(2, 100)).empty());
    write_file(dir.path / "notes.json", "{\"mine\": true}");
    TEST_ASSERT_EQ(store.clear(), size_t{2});
    TEST_ASSERT(store.load_index().empty());
    TEST_ASSERT_FALSE(std::filesystem::exists(store.index_path()));
    TEST_ASSERT(std::filesystem::exists(dir.path / "notes.json"));
}

TEST_CASE(PullGrouping, VisitsNewestFirstNumberedFromOne) {
    std::vector<HistoryEntry> entries(5);
    // Two fights in one visit, three in the next.
    for (size_t i = 0; i < entries.size(); ++i) {
        entries[i].map_id = i < 2 ? 610010 : 400001;
        entries[i].visit_ts = i < 2 ? at_s(0) : at_s(500);
        entries[i].start_ts = at_s(10.0 + 100.0 * static_cast<double>(i));
    }
    const auto groups = group_by_visit(entries);
    TEST_ASSERT_EQ(groups.size(), size_t{2});
    TEST_ASSERT_EQ(groups[0].map_id, uint32_t{400001});
    TEST_ASSERT_EQ(groups[0].pulls.size(), size_t{3});
    TEST_ASSERT_EQ(groups[0].pulls[0].index, size_t{4});
    TEST_ASSERT_EQ(groups[0].pulls[0].number, 3);
    TEST_ASSERT_EQ(groups[0].pulls[2].number, 1);
    TEST_ASSERT_EQ(groups[1].pulls[0].index, size_t{1});
    TEST_ASSERT_EQ(groups[1].pulls[0].number, 2);
}

TEST_CASE(PullGrouping, TwoRunsOfOneDungeonAreTwoGroups) {
    std::vector<HistoryEntry> entries(4);
    for (size_t i = 0; i < entries.size(); ++i) {
        entries[i].map_id = 600001;
        entries[i].visit_ts = i < 2 ? at_s(0) : at_s(900);
        entries[i].start_ts = at_s(10.0 + 300.0 * static_cast<double>(i));
    }
    const auto groups = group_by_visit(entries);
    TEST_ASSERT_EQ(groups.size(), size_t{2});
    TEST_ASSERT_EQ(groups[0].visit_ts, at_s(900));
    TEST_ASSERT_EQ(groups[0].pulls.size(), size_t{2});
    TEST_ASSERT_EQ(groups[1].pulls.size(), size_t{2});
}

TEST_CASE(PullGrouping, OldRecordsGroupByMapRuns) {
    // Version 1 records have no visit: a run on one map stands in for one.
    std::vector<HistoryEntry> entries(4);
    const uint32_t maps[] = {610010, 610010, 400001, 610010};
    for (size_t i = 0; i < entries.size(); ++i) entries[i].map_id = maps[i];
    const auto groups = group_by_visit(entries);
    TEST_ASSERT_EQ(groups.size(), size_t{3});
    TEST_ASSERT_EQ(groups[2].pulls.size(), size_t{2});
    TEST_ASSERT_EQ(groups[0].pulls[0].number, 1);
}
