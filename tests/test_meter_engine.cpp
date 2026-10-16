#include "test_framework.hpp"
#include "meter_test_support.hpp"

#include "meter/encounter_engine.hpp"

using namespace aion2;
using namespace aion2::meter;
using namespace aion2::test::meter_support;

namespace {

constexpr EntityId kMe = 100;
constexpr EntityId kAlly = 200;
constexpr EntityId kBoss = 0x4100;
constexpr EntityId kAdd = 0x4101;
constexpr uint64_t kBossHp = 5'000'000;

/// An engine with the local player, one ally, a boss and an add in view.
EncounterEngine setup() {
    EncounterEngine e;
    e.on_event(ev(at_s(0), player(kMe, "Ariel", true)));
    e.on_event(ev(at_s(0), player(kAlly, "Lumiel")));
    e.on_event(ev(at_s(0), npc(kBoss, kBossHp, 777)));
    e.on_event(ev(at_s(0), npc(kAdd, 50'000, 778)));
    return e;
}

} // namespace

TEST_CASE(Engine, FightTotalsAndDps) {
    EncounterEngine e = setup();
    TEST_ASSERT_FALSE(e.in_fight());
    e.on_event(ev(at_s(10), hit(kMe, kBoss, 1000, true, true)));
    TEST_ASSERT(e.in_fight());
    e.on_event(ev(at_s(12), hit(kAlly, kBoss, 3000)));
    e.on_event(ev(at_s(20), hit(kMe, kBoss, 1000)));

    const FightSummary s = e.current();
    TEST_ASSERT(s.live);
    TEST_ASSERT_NEAR(s.duration_s, 10.0, 1e-9);
    TEST_ASSERT_EQ(s.total_damage, uint64_t{5000});
    TEST_ASSERT_NEAR(s.total_dps, 500.0, 1e-9);
    TEST_ASSERT_EQ(s.combatants.size(), size_t{2});
    TEST_ASSERT_EQ(s.combatants[0].name, std::string("Lumiel"));
    TEST_ASSERT_NEAR(s.combatants[0].share, 0.6, 1e-9);
    const CombatantTotals& me = s.combatants[1];
    TEST_ASSERT(me.is_local);
    TEST_ASSERT_EQ(me.hits.hits, uint64_t{2});
    TEST_ASSERT_EQ(me.hits.crits, uint64_t{1});
    TEST_ASSERT_NEAR(me.back_rate(), 0.5, 1e-9);
    TEST_ASSERT_NEAR(me.dps, 200.0, 1e-9);
}

TEST_CASE(Engine, BossIsTheBiggestEnemyWithHpEstimate) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kAdd, 40'000)));
    e.on_event(ev(at_s(2), hit(kAlly, kBoss, 1'000'000)));
    const BossSummary b = e.current().boss;
    TEST_ASSERT_EQ(b.id, kBoss);
    TEST_ASSERT(b.is_boss);
    TEST_ASSERT_EQ(b.npc_code, 777u);
    TEST_ASSERT_EQ(b.label, std::string("NPC #777"));
    TEST_ASSERT(b.hp_known);
    TEST_ASSERT_EQ(b.hp, kBossHp - 1'000'000);
    TEST_ASSERT_NEAR(b.hp_fraction(), 0.8, 1e-9);
}

TEST_CASE(Engine, TickAloneNeverStartsAFight) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), dot(kMe, kBoss, 500)));
    TEST_ASSERT_FALSE(e.in_fight());
    e.on_event(ev(at_s(2), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(3), dot(kMe, kBoss, 500)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(s.combatants[0].damage, uint64_t{600});
    TEST_ASSERT_EQ(s.combatants[0].dot_damage, uint64_t{500});
    TEST_ASSERT_EQ(s.combatants[0].hits.hits, uint64_t{1});
}

TEST_CASE(Engine, EnemyHittingPlayersStartsNothing) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kBoss, kMe, 9999)));
    TEST_ASSERT_FALSE(e.in_fight());
}

TEST_CASE(Engine, IdleEndsTrashFightAtLastHit) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kAdd, 100)));
    e.on_event(ev(at_s(5), hit(kMe, kAdd, 100)));
    e.tick(at_s(14.9));
    TEST_ASSERT(e.in_fight());
    e.tick(at_s(15));
    TEST_ASSERT_FALSE(e.in_fight());
    const auto done = e.take_finished();
    TEST_ASSERT_EQ(done.size(), size_t{1});
    TEST_ASSERT(done[0].end_reason == EndReason::Idle);
    TEST_ASSERT_NEAR(done[0].duration_s, 4.0, 1e-9);
    TEST_ASSERT_FALSE(done[0].boss.is_boss);
}

TEST_CASE(Engine, StandingBossGetsLongerIdle) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    // A boss phase where it cannot be hit: traffic goes on, hits do not.
    for (double t = 2; t <= 30; t += 2) e.on_event(ev(at_s(t), proto::Heartbeat{1}));
    e.tick(at_s(30));
    TEST_ASSERT(e.in_fight());
    e.tick(at_s(31));
    TEST_ASSERT_FALSE(e.in_fight());
    TEST_ASSERT(e.take_finished().at(0).end_reason == EndReason::Idle);
}

TEST_CASE(Engine, BossKillEndsAfterGraceWithLateTicks) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 4'000'000)));
    e.on_event(ev(at_s(9), hit(kAlly, kBoss, 1'000'000)));
    e.on_event(ev(at_s(9), proto::Death{kBoss}));
    TEST_ASSERT(e.in_fight());
    e.on_event(ev(at_s(10), dot(kMe, kBoss, 300)));  // inside the grace
    e.tick(at_s(11));
    TEST_ASSERT_FALSE(e.in_fight());
    const FightSummary s = e.take_finished().at(0);
    TEST_ASSERT(s.end_reason == EndReason::BossKilled);
    TEST_ASSERT(s.boss.killed);
    TEST_ASSERT_EQ(s.boss.hp, uint64_t{0});
    TEST_ASSERT_EQ(s.total_damage, uint64_t{5'000'300});
    TEST_ASSERT_NEAR(s.duration_s, 9.0, 1e-9);
    TEST_ASSERT_FALSE(s.quality.partial);
}

TEST_CASE(Engine, ServerHpIsExactAndZeroIsTheKill) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 1'000'000)));
    // The server's figure wins over spawn HP less damage seen.
    e.on_event(ev(at_s(1), hp(kBoss, 3'500'000)));
    BossSummary b = e.current().boss;
    TEST_ASSERT(b.hp_exact);
    TEST_ASSERT_EQ(b.hp, uint64_t{3'500'000});

    e.on_event(ev(at_s(9), hit(kAlly, kBoss, 3'500'000)));
    e.on_event(ev(at_s(9), hp(kBoss, 0)));
    // No 42 36 yet (it comes with the corpse): HP 0 alone is the kill.
    e.tick(at_s(11));
    TEST_ASSERT_FALSE(e.in_fight());
    const FightSummary s = e.take_finished().at(0);
    TEST_ASSERT(s.end_reason == EndReason::BossKilled);
    TEST_ASSERT(s.boss.killed);
    TEST_ASSERT_NEAR(s.duration_s, 8.0, 1e-9);
}

TEST_CASE(Engine, LocalPlayerFromSelfValues) {
    EncounterEngine e;
    e.on_event(ev(at_s(0), npc(kAdd, 50'000, 778)));
    e.on_event(ev(at_s(0), self_values(6745)));
    e.on_event(ev(at_s(1), hit(6745, kAdd, 100)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(s.combatants.size(), size_t{1});
    TEST_ASSERT(s.combatants[0].is_local);
    TEST_ASSERT_EQ(s.combatants[0].name, std::string("You"));
}

TEST_CASE(Engine, KilledBossWithLittleDamageSeenIsPartial) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100'000)));
    e.on_event(ev(at_s(5), proto::Death{kBoss}));
    e.tick(at_s(10));
    const FightSummary s = e.take_finished().at(0);
    TEST_ASSERT(s.quality.partial);
    TEST_ASSERT_FALSE(s.quality.clean());
}

TEST_CASE(Engine, MapChangeEndsFightButTeleportDoesNot) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), proto::MapLoad{1, 610010}));
    e.on_event(ev(at_s(0), npc(kBoss, kBossHp)));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(2), proto::MapLoad{2, 610010}));
    TEST_ASSERT(e.in_fight());
    e.on_event(ev(at_s(3), proto::MapLoad{3, 200100}));
    TEST_ASSERT_FALSE(e.in_fight());
    const FightSummary s = e.take_finished().at(0);
    TEST_ASSERT(s.end_reason == EndReason::MapChange);
    TEST_ASSERT_EQ(s.map_id, 610010u);
    TEST_ASSERT_EQ(e.map_id(), 200100u);
}

TEST_CASE(Engine, CaptureLostEndsFight) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.tick(at_s(16.5));
    TEST_ASSERT_FALSE(e.in_fight());
    TEST_ASSERT(e.take_finished().at(0).end_reason == EndReason::CaptureLost);
}

TEST_CASE(Engine, ManualEnd) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.end_fight(EndReason::Manual, at_s(2));
    TEST_ASSERT_FALSE(e.in_fight());
    TEST_ASSERT(e.current().end_reason == EndReason::Manual);
    TEST_ASSERT_FALSE(e.current().live);
}

TEST_CASE(Engine, SummonDamageGoesToOwner) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), summon(0x4300, kAlly)));
    e.on_event(ev(at_s(1), hit(0x4300, kBoss, 700)));
    e.on_event(ev(at_s(2), hit(kAlly, kBoss, 300)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(s.combatants.size(), size_t{1});
    TEST_ASSERT_EQ(s.combatants[0].id, kAlly);
    TEST_ASSERT_EQ(s.combatants[0].damage, uint64_t{1000});
}

TEST_CASE(Engine, RosterExcludesStrangers) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), player(300, "Stranger")));
    e.on_event(ev(at_s(0), roster({"Ariel", "Lumiel"})));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(2), hit(300, kBoss, 900)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(s.combatants.size(), size_t{1});
    TEST_ASSERT_EQ(s.total_damage, uint64_t{100});
    // Their hits still lower the boss's HP.
    TEST_ASSERT_EQ(s.boss.hp, kBossHp - 1000);
}

TEST_CASE(Engine, MidSessionStartInfersSides) {
    // No identity or spawn seen: players appear as #id once they hit a folded-range enemy.
    EncounterEngine e;
    e.on_event(ev(at_s(1), hit(55, 0x4600, 500)));
    e.on_event(ev(at_s(2), hit(56, 0x4600, 250)));
    const FightSummary s = e.current();
    TEST_ASSERT(s.live);
    TEST_ASSERT_EQ(s.combatants.size(), size_t{2});
    TEST_ASSERT_EQ(s.combatants[0].name, std::string("#55"));
    TEST_ASSERT_FALSE(s.boss.hp_known);
    TEST_ASSERT_FALSE(s.boss.is_boss);
}

TEST_CASE(Engine, CaptureQualityIsPerFight) {
    EncounterEngine e = setup();
    CaptureQuality totals;
    totals.gap_bytes = 100;
    e.set_capture_counters(totals);
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    totals.gap_bytes = 160;
    totals.resyncs = 1;
    e.set_capture_counters(totals);
    e.end_fight(EndReason::Manual, at_s(2));
    const FightSummary s = e.take_finished().at(0);
    TEST_ASSERT_EQ(s.quality.gap_bytes, uint64_t{60});
    TEST_ASSERT_EQ(s.quality.resyncs, uint64_t{1});
}

TEST_CASE(Engine, RecentKeepsTheLastFew) {
    MeterSettings settings;
    settings.memory_history = 2;
    EncounterEngine e(settings);
    e.on_event(ev(at_s(0), player(kMe, "Ariel", true)));
    for (int i = 0; i < 3; ++i) {
        e.on_event(ev(at_s(100.0 * i + 1), hit(kMe, 0x4700, 10)));
        e.end_fight(EndReason::Manual, at_s(100.0 * i + 2));
    }
    TEST_ASSERT_EQ(e.recent().size(), size_t{2});
    TEST_ASSERT_EQ(e.recent().front().id, uint64_t{2});
    TEST_ASSERT_EQ(e.take_finished().size(), size_t{3});
}

TEST_CASE(Engine, WipeDetectionWhenEnabled) {
    MeterSettings settings;
    settings.wipe_detection = true;
    EncounterEngine e(settings);
    e.on_event(ev(at_s(0), player(kMe, "Ariel", true)));
    e.on_event(ev(at_s(0), player(kAlly, "Lumiel")));
    e.on_event(ev(at_s(0), npc(kBoss, kBossHp)));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(1), hit(kAlly, kBoss, 100)));
    e.on_event(ev(at_s(5), proto::Death{kMe}));
    TEST_ASSERT(e.in_fight());
    e.on_event(ev(at_s(6), proto::Death{kAlly}));
    TEST_ASSERT_FALSE(e.in_fight());
    TEST_ASSERT(e.take_finished().at(0).end_reason == EndReason::Wipe);
}
