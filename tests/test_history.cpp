#include "test_framework.hpp"
#include "file_test_support.hpp"
#include "meter_test_support.hpp"

#include "meter/fight_json.hpp"
#include "meter/history_store.hpp"

#include <fstream>

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

TEST_CASE(FightJson, OtherVersionIsRefused) {
    auto j = fight_to_json(sample_fight(1, 0));
    j["v"] = 99;
    TEST_ASSERT_FALSE(fight_from_json(j).has_value());
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
