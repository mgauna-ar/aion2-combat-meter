#include "test_framework.hpp"
#include "meter_test_support.hpp"

#include "meter/encounter_engine.hpp"

#include <algorithm>
#include <optional>
#include <string>

using namespace aion2;
using namespace aion2::meter;
using namespace aion2::test::meter_support;

namespace {

constexpr EntityId kMe = 100;
constexpr EntityId kAlly = 200;
constexpr EntityId kBoss = 0x4100;
constexpr EntityId kAdd = 0x4101;
constexpr uint64_t kBossHp = 5'000'000;
constexpr uint32_t kDungeon = 610010;
constexpr uint32_t kOpenWorld = 200100;

/// An engine with the local player, one ally, a boss and an add in view, in a
/// dungeon, where every player counts without a roster; or, on a map not seen
/// loading, where only the local player does.
EncounterEngine setup(bool dungeon = true) {
    EncounterEngine e;
    if (dungeon) e.on_event(ev(at_s(0), proto::MapLoad{1, kDungeon}));
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

TEST_CASE(Engine, HpLostWithoutHitsSeenIsPartial) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100'000)));
    e.on_event(ev(at_s(1), hp(kBoss, 4'000'000)));
    // The server sends the boss's HP falling, but not the hits that did it.
    e.on_event(ev(at_s(2), hp(kBoss, 2'000'000)));
    e.on_event(ev(at_s(3), hp(kBoss, 500'000)));
    e.on_event(ev(at_s(4), hit(kMe, kBoss, 100'000)));
    e.on_event(ev(at_s(4), hp(kBoss, 0)));
    e.tick(at_s(10));
    const FightSummary s = e.take_finished().at(0);
    TEST_ASSERT(s.quality.partial);
    TEST_ASSERT_FALSE(s.quality.clean());
}

TEST_CASE(Engine, HpFallingByAMultipleOfDamageIsNotPartial) {
    // As in a real capture: every HP drop follows a hit, and is 4.5 times it.
    EncounterEngine e = setup();
    uint64_t hp_left = kBossHp;
    for (int i = 1; hp_left > 0; ++i) {
        e.on_event(ev(at_s(i), hit(kMe, kBoss, 100'000)));
        hp_left = hp_left > 450'000 ? hp_left - 450'000 : 0;
        e.on_event(ev(at_s(i), hp(kBoss, hp_left)));
    }
    e.tick(at_s(30));
    const FightSummary s = e.take_finished().at(0);
    TEST_ASSERT(s.boss.killed);
    TEST_ASSERT_FALSE(s.quality.partial);
}

TEST_CASE(Engine, EveryMapLoadOnANewConnectionEndsTheFight) {
    EncounterEngine e = setup();
    e.on_event(ev_on(1, at_s(0), proto::MapLoad{1, 610010}));
    e.on_event(ev(at_s(0), npc(kBoss, kBossHp)));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    // The same map on a new connection: running the dungeon again, straight back in.
    e.on_event(ev_on(2, at_s(2), proto::MapLoad{2, 610010}));
    TEST_ASSERT_FALSE(e.in_fight());
    e.on_event(ev(at_s(3), npc(kBoss, kBossHp)));
    e.on_event(ev(at_s(4), hit(kMe, kBoss, 100)));
    // Another map ends it, whatever the connection.
    e.on_event(ev_on(2, at_s(5), proto::MapLoad{3, 200100}));
    TEST_ASSERT_FALSE(e.in_fight());
    const auto finished = e.take_finished();
    TEST_ASSERT_EQ(finished.size(), size_t{2});
    TEST_ASSERT(finished[0].end_reason == EndReason::MapChange);
    TEST_ASSERT(finished[1].end_reason == EndReason::MapChange);
    TEST_ASSERT_EQ(finished[0].map_id, 610010u);
    TEST_ASSERT_EQ(finished[1].map_id, 610010u);
    // Each run is its own visit.
    TEST_ASSERT_EQ(finished[0].visit_ts, at_s(0));
    TEST_ASSERT_EQ(finished[1].visit_ts, at_s(2));
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
    // No identity or spawn seen: players appear as #id once they hit a folded-range
    // enemy. In a dungeon, every player counts without a roster.
    EncounterEngine e;
    e.on_event(ev(at_s(0), proto::MapLoad{1, kDungeon}));
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
    e.on_event(ev(at_s(0), roster({"Ariel", "Lumiel"})));
    e.on_event(ev(at_s(0), npc(kBoss, kBossHp)));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(1), hit(kAlly, kBoss, 100)));
    e.on_event(ev(at_s(5), proto::Death{kMe}));
    TEST_ASSERT(e.in_fight());
    e.on_event(ev(at_s(6), proto::Death{kAlly}));
    TEST_ASSERT_FALSE(e.in_fight());
    TEST_ASSERT(e.take_finished().at(0).end_reason == EndReason::Wipe);
}

TEST_CASE(Engine, SummonOfAnUnseenOwnerCountsOnAnyTarget) {
    // Mid-session: no names, and a target that spawned before the meter started.
    EncounterEngine e;
    e.on_event(ev(at_s(0), proto::MapLoad{1, kDungeon}));
    e.on_event(ev(at_s(0), summon(0x4300, 300)));
    e.on_event(ev(at_s(1), hit(0x4300, 900, 500)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(s.combatants.size(), size_t{1});
    TEST_ASSERT_EQ(s.combatants[0].id, EntityId{300});
    TEST_ASSERT_EQ(s.combatants[0].damage, uint64_t{500});
}

TEST_CASE(Engine, DeadPlayersDotDoesNotReviveThem) {
    MeterSettings settings;
    settings.wipe_detection = true;
    EncounterEngine e(settings);
    e.on_event(ev(at_s(0), player(kMe, "Ariel", true)));
    e.on_event(ev(at_s(0), player(kAlly, "Lumiel")));
    e.on_event(ev(at_s(0), roster({"Ariel", "Lumiel"})));
    e.on_event(ev(at_s(0), npc(kBoss, kBossHp)));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(1), hit(kAlly, kBoss, 100)));
    e.on_event(ev(at_s(5), proto::Death{kMe}));
    e.on_event(ev(at_s(5.5), dot(kMe, kBoss, 30)));  // still ticking on the boss
    e.on_event(ev(at_s(6), proto::Death{kAlly}));
    TEST_ASSERT_FALSE(e.in_fight());
    TEST_ASSERT(e.take_finished().at(0).end_reason == EndReason::Wipe);
}

TEST_CASE(Engine, BossBackInViewKeepsItsHpLoss) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100'000)));
    e.on_event(ev(at_s(1), hp(kBoss, 4'000'000)));
    // Out of view, others take it down to 1M; it spawns again when it comes back.
    e.on_event(ev(at_s(5), npc(kBoss, kBossHp, 777, 1'000'000)));
    e.on_event(ev(at_s(6), hit(kMe, kBoss, 100'000)));
    e.on_event(ev(at_s(6), hp(kBoss, 0)));
    e.tick(at_s(10));
    const FightSummary s = e.take_finished().at(0);
    TEST_ASSERT(s.boss.killed);
    TEST_ASSERT(s.quality.partial);
}

TEST_CASE(Engine, LateAliasJoinsTheRows) {
    EncounterEngine e = setup();
    // An ally under a second id, unnamed at first...
    e.on_event(ev(at_s(1), hit(300, kBoss, 1'000)));
    // ...then named: the same character as kAlly.
    e.on_event(ev(at_s(2), player(300, "Lumiel")));
    e.on_event(ev(at_s(3), hit(kAlly, kBoss, 500)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(s.combatants.size(), size_t{1});
    TEST_ASSERT_EQ(s.combatants[0].id, kAlly);
    TEST_ASSERT_EQ(s.combatants[0].damage, uint64_t{1'500});
    TEST_ASSERT_EQ(s.combatants[0].hits.hits, uint64_t{2});
}

TEST_CASE(Engine, BiggerBossHitDuringTheGraceKeepsTheFight) {
    constexpr EntityId kBigger = 0x4102;
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), npc(kBigger, 2 * kBossHp, 779)));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(5), proto::Death{kBoss}));
    e.on_event(ev(at_s(6), hit(kMe, kBigger, 100)));
    e.tick(at_s(8));
    TEST_ASSERT(e.in_fight());
    TEST_ASSERT_EQ(e.current().boss.id, kBigger);
    e.on_event(ev(at_s(20), proto::Death{kBigger}));
    e.tick(at_s(23));
    TEST_ASSERT_FALSE(e.in_fight());
    TEST_ASSERT(e.take_finished().at(0).end_reason == EndReason::BossKilled);
}

namespace {

const CombatantTotals& row_of(const FightSummary& s, EntityId id) {
    const auto it = std::find_if(s.combatants.begin(), s.combatants.end(),
                                 [id](const CombatantTotals& c) { return c.id == id; });
    TEST_ASSERT(it != s.combatants.end());
    return *it;
}

} // namespace

TEST_CASE(Engine, SkillsCollectTheirVariantsHitsAndTicks) {
    EncounterEngine e = setup();
    using proto::hit_direction::Back;
    using proto::hit_direction::Front;
    // Judgment twice, as two variants; Pummel once; a tick of Judgment.
    e.on_event(ev(at_s(1), skilled(hit(kMe, kBoss, 8000, true), 12240340, Front, proto::hit_modifier::Perfect)));
    e.on_event(ev(at_s(2), skilled(hit(kMe, kBoss, 4000), 12240347, Back)));
    e.on_event(ev(at_s(3), skilled(hit(kMe, kBoss, 1500), 12040230, Back, proto::hit_modifier::Double)));
    proto::Tick t = dot(kMe, kBoss, 300);
    t.skill = 12240340;
    e.on_event(ev(at_s(4), t));

    const FightSummary s = e.current();
    const CombatantTotals& me = row_of(s, kMe);
    TEST_ASSERT(me.cls == AionClass::Templar);
    TEST_ASSERT_EQ(me.skills.size(), size_t{2});
    const SkillTotals& judgment = me.skills[0];
    TEST_ASSERT_EQ(judgment.code, uint32_t{12240000});
    TEST_ASSERT_EQ(judgment.damage, uint64_t{12300});
    TEST_ASSERT_EQ(judgment.tick_damage, uint64_t{300});
    TEST_ASSERT_EQ(judgment.ticks, uint64_t{1});
    TEST_ASSERT_EQ(judgment.hits.hits, uint64_t{2});
    TEST_ASSERT_EQ(judgment.min_hit, uint64_t{4000});
    TEST_ASSERT_EQ(judgment.max_hit, uint64_t{8000});
    TEST_ASSERT_EQ(judgment.hits.crits, uint64_t{1});
    TEST_ASSERT_EQ(judgment.hits.perfect, uint64_t{1});
    TEST_ASSERT_EQ(judgment.hits.front, uint64_t{1});
    TEST_ASSERT_EQ(judgment.hits.back, uint64_t{1});
    TEST_ASSERT_EQ(me.skills[1].code, uint32_t{12040000});
    TEST_ASSERT_EQ(me.skills[1].hits.doubles, uint64_t{1});
    TEST_ASSERT_EQ(me.hits.front, uint64_t{1});
    TEST_ASSERT_EQ(me.hits.back, uint64_t{2});
}

TEST_CASE(Engine, AnIdentityWithAnUnknownClassKeepsTheSkillsClass) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), skilled(hit(kAlly, kBoss, 100), 16040020)));
    e.on_event(ev(at_s(2), player(kAlly, "Lumiel")));
    const FightSummary s = e.current();
    TEST_ASSERT(row_of(s, kAlly).cls == AionClass::Spiritmaster);
}

TEST_CASE(Engine, HitsOnATrapAroundAPlayerCountNothing) {
    // The second capture's: a mechanic aimed at a party member, 10 HP, that the
    // others break 1 damage a hit. The game counts neither the hits nor the damage.
    constexpr EntityId kTrap = 0x4102;
    EncounterEngine e = setup();
    proto::Spawn trap = npc(kTrap, 10, 2'920'360);
    trap.kind = proto::spawn_kind::Mechanic;
    trap.aimed_at = "Lumiel";
    e.on_event(ev(at_s(1), trap));
    e.on_event(ev(at_s(2), hit(kMe, kTrap, 1)));
    TEST_ASSERT_FALSE(e.in_fight());

    e.on_event(ev(at_s(3), hit(kMe, kBoss, 1000)));
    e.on_event(ev(at_s(4), hit(kMe, kTrap, 1)));
    e.on_event(ev(at_s(5), hit(kMe, kTrap, 1)));
    const FightSummary f = e.current();
    const CombatantTotals& me = row_of(f, kMe);
    TEST_ASSERT_EQ(me.damage, uint64_t{1000});
    TEST_ASSERT_EQ(me.hits.hits, uint64_t{1});
    TEST_ASSERT_NEAR(f.duration_s, 1.0, 1e-9);
}

TEST_CASE(Engine, MultiHitsAndBlocksAreCounted) {
    EncounterEngine e = setup();
    proto::Damage multi = hit(kMe, kBoss, 1000);
    multi.extra_hits = {1, 1};
    proto::Damage blocked = hit(kMe, kBoss, 800);
    blocked.layout = 6;
    blocked.modifiers = proto::hit_modifier::Block;
    e.on_event(ev(at_s(1), multi));
    e.on_event(ev(at_s(2), blocked));
    e.on_event(ev(at_s(3), hit(kMe, kBoss, 900)));
    const FightSummary f = e.current();
    const CombatantTotals& me = row_of(f, kMe);
    TEST_ASSERT_EQ(me.hits.hits, uint64_t{3});
    TEST_ASSERT_EQ(me.hits.multi, uint64_t{1});
    TEST_ASSERT_EQ(me.hits.blocks, uint64_t{1});
    // A multi-hit's further strikes add no damage.
    TEST_ASSERT_EQ(me.damage, uint64_t{2700});
}

TEST_CASE(Engine, HealingCountsOnlyDuringAFight) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), heal_tick(kAlly, kMe, 500, 17080000)));
    TEST_ASSERT_FALSE(e.in_fight());

    e.on_event(ev(at_s(2), hit(kMe, kBoss, 1000)));
    e.on_event(ev(at_s(3), hit(kAlly, kBoss, 1000)));
    e.on_event(ev(at_s(4), heal_tick(kAlly, kMe, 700, 17080000)));
    // A 04 38 record from a player to a player is a heal too.
    e.on_event(ev(at_s(5), skilled(hit(kAlly, kAlly, 300), 17010000)));
    const FightSummary s = e.current();
    const CombatantTotals& ally = row_of(s, kAlly);
    TEST_ASSERT_EQ(ally.healing, uint64_t{1000});
    TEST_ASSERT_EQ(ally.heal_hits, uint64_t{2});
    TEST_ASSERT(ally.cls == AionClass::Cleric);
    TEST_ASSERT_EQ(ally.heal_skills.size(), size_t{2});
    TEST_ASSERT_EQ(ally.heal_skills[0].code, uint32_t{17080000});
    TEST_ASSERT_EQ(ally.heal_skills[0].amount, uint64_t{700});
    // Healing is not damage, and the fight's clock stops at the last hit.
    TEST_ASSERT_EQ(ally.damage, uint64_t{1000});
    TEST_ASSERT_EQ(s.total_healing, uint64_t{1000});
    TEST_ASSERT_NEAR(s.duration_s, 1.0, 1e-9);
}

TEST_CASE(Engine, ASummonsRecordToItselfIsNoHeal) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), summon(0x4200, kMe)));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 1000)));
    // A spirit's record to itself as it spawned carried 100,000.
    e.on_event(ev(at_s(2), skilled(hit(0x4200, 0x4200, 100'000), 16990004)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(row_of(s, kMe).healing, uint64_t{0});
}

TEST_CASE(Engine, DamageTakenIsByAttackingSkillAndNpc) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 1000)));
    e.on_event(ev(at_s(2), skilled(hit(kBoss, kMe, 400, true), 900100, proto::hit_direction::Front)));
    e.on_event(ev(at_s(3), skilled(hit(kBoss, kMe, 600), 900100)));
    e.on_event(ev(at_s(4), dot(kAdd, kMe, 50)));
    const FightSummary s = e.current();
    const CombatantTotals& me = row_of(s, kMe);
    TEST_ASSERT_EQ(me.damage_taken, uint64_t{1050});
    TEST_ASSERT_EQ(me.taken.hits, uint64_t{2});
    TEST_ASSERT_EQ(me.taken.crits, uint64_t{1});
    TEST_ASSERT_EQ(me.taken.front, uint64_t{1});
    TEST_ASSERT_EQ(me.taken_by.size(), size_t{2});
    TEST_ASSERT_EQ(me.taken_by[0].code, uint32_t{900100});
    TEST_ASSERT_EQ(me.taken_by[0].source_npc, uint32_t{777});
    TEST_ASSERT_EQ(me.taken_by[0].amount, uint64_t{1000});
    TEST_ASSERT_EQ(me.taken_by[0].max, uint64_t{600});
    TEST_ASSERT_EQ(me.taken_by[1].source_npc, uint32_t{778});
}

TEST_CASE(Engine, StrangersHealingOrHitNearbyGetNoRow) {
    EncounterEngine e = setup();
    constexpr EntityId kStranger = 300;
    e.on_event(ev(at_s(0), player(kStranger, "Zikel")));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 1000)));
    e.on_event(ev(at_s(2), heal_tick(kStranger, kStranger, 900)));
    e.on_event(ev(at_s(3), hit(kAdd, kStranger, 900)));
    TEST_ASSERT_EQ(e.current().combatants.size(), size_t{1});
    // A summon's own HP is not its owner's.
    e.on_event(ev(at_s(0), summon(0x4200, kMe)));
    e.on_event(ev(at_s(4), hit(kBoss, 0x4200, 500)));
    const FightSummary after = e.current();
    TEST_ASSERT_EQ(row_of(after, kMe).damage_taken, uint64_t{0});
}

TEST_CASE(Engine, OutOfAPartyTheOpenWorldCountsOnlyYou) {
    // A world boss, solo: everyone around hits it.
    EncounterEngine e = setup(false);
    constexpr EntityId kStranger = 300;
    e.on_event(ev(at_s(0), player(kStranger, "Zikel")));
    e.on_event(ev(at_s(1), hit(kStranger, kAdd, 900)));
    TEST_ASSERT_FALSE(e.in_fight());
    e.on_event(ev(at_s(2), hit(kMe, kAdd, 100)));
    e.on_event(ev(at_s(3), hit(kStranger, kAdd, 900)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(s.combatants.size(), size_t{1});
    TEST_ASSERT(s.combatants[0].is_local);
    TEST_ASSERT_EQ(s.total_damage, uint64_t{100});
    // Their hits do not keep the fight going.
    e.on_event(ev(at_s(13), hit(kStranger, kAdd, 900)));
    TEST_ASSERT_FALSE(e.in_fight());

    // In a dungeon, only the group is there: everyone counts.
    e.on_event(ev(at_s(20), proto::MapLoad{2, kDungeon}));
    e.on_event(ev(at_s(21), npc(kBoss, kBossHp, 777)));
    e.on_event(ev(at_s(22), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(23), hit(kStranger, kBoss, 900)));
    TEST_ASSERT_EQ(e.current().combatants.size(), size_t{2});

    // Back out in the open world, only you again.
    e.on_event(ev(at_s(30), proto::MapLoad{3, kOpenWorld}));
    e.on_event(ev(at_s(31), npc(kAdd, 50'000, 778)));
    e.on_event(ev(at_s(32), hit(kStranger, kAdd, 900)));
    TEST_ASSERT_FALSE(e.in_fight());
}

TEST_CASE(Engine, APlayerToPlayerRecordOffThePartyIsNoHeal) {
    // PvP: a hit on a player of the other faction is a 04 38 between two players,
    // like a heal.
    EncounterEngine e = setup();
    constexpr EntityId kFoe = 300;
    e.on_event(ev(at_s(0), player(kFoe, "Zikel")));
    e.on_event(ev(at_s(0), roster({"Ariel", "Lumiel"})));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 1000)));
    e.on_event(ev(at_s(2), skilled(hit(kMe, kFoe, 5000), 17010000)));
    e.on_event(ev(at_s(3), skilled(hit(kMe, kAlly, 300), 17010000)));
    // A heal tick is a heal, wherever it lands.
    e.on_event(ev(at_s(4), heal_tick(kMe, kFoe, 200, 17080000)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(row_of(s, kMe).healing, uint64_t{500});
    TEST_ASSERT_EQ(s.total_healing, uint64_t{500});

    // Out of a party, in the open world, only a record on yourself counts.
    EncounterEngine solo = setup(false);
    solo.on_event(ev(at_s(1), hit(kMe, kBoss, 1000)));
    solo.on_event(ev(at_s(2), skilled(hit(kMe, kAlly, 5000), 17010000)));
    solo.on_event(ev(at_s(3), skilled(hit(kMe, kMe, 300), 17010000)));
    TEST_ASSERT_EQ(row_of(solo.current(), kMe).healing, uint64_t{300});
}

TEST_CASE(Engine, FightsCarryTheirMapVisit) {
    EncounterEngine e = setup(false);
    e.on_event(ev(at_s(10), hit(kMe, kAdd, 100)));
    e.end_fight(EndReason::Manual, at_s(11));
    e.on_event(ev(at_s(20), hit(kMe, kAdd, 100)));
    e.end_fight(EndReason::Manual, at_s(21));
    // Started mid-visit: the first fight opens it.
    TEST_ASSERT_EQ(e.recent()[0].visit_ts, at_s(10));
    TEST_ASSERT_EQ(e.recent()[1].visit_ts, at_s(10));

    proto::MapLoad map;
    map.map_id = 610010;
    e.on_event(ev_on(1, at_s(30), map));
    e.on_event(ev(at_s(31), npc(kBoss, kBossHp, 777)));
    e.on_event(ev(at_s(40), hit(kMe, kBoss, 100)));
    e.end_fight(EndReason::Manual, at_s(41));
    // Loading the same map again over a new connection is a new run of it: a visit
    // of its own.
    e.on_event(ev_on(2, at_s(50), map));
    e.on_event(ev(at_s(51), npc(kBoss, kBossHp, 777)));
    e.on_event(ev(at_s(60), hit(kMe, kBoss, 100)));
    e.end_fight(EndReason::Manual, at_s(61));
    TEST_ASSERT_EQ(e.recent()[2].visit_ts, at_s(30));
    TEST_ASSERT_EQ(e.recent()[3].visit_ts, at_s(50));
}

namespace {

/// The damage on the row named `name`, or nullopt with no such row.
std::optional<uint64_t> damage_by_name(const FightSummary& s, const std::string& name) {
    const auto it = std::find_if(s.combatants.begin(), s.combatants.end(),
                                 [&](const CombatantTotals& c) { return c.name == name; });
    if (it == s.combatants.end()) return std::nullopt;
    return it->damage;
}

} // namespace

TEST_CASE(Engine, AReloadOnTheSameConnectionKeepsTheRun) {
    // A teleport or a revive inside a dungeon loads its map again, over the same
    // connection: one run, one visit, and the fight goes on.
    EncounterEngine e = setup(false);
    e.on_event(ev(at_s(0), roster({"Ariel", "Lumiel"})));
    e.on_event(ev_on(1, at_s(1), proto::MapLoad{1, kDungeon}));
    e.on_event(ev(at_s(1), player(kMe, "Ariel", true)));
    e.on_event(ev(at_s(1), player(kAlly, "Lumiel")));
    e.on_event(ev(at_s(1), npc(kBoss, kBossHp, 777)));
    e.on_event(ev(at_s(2), hit(kMe, kBoss, 100)));
    e.on_event(ev_on(1, at_s(3), proto::MapLoad{2, kDungeon}));
    TEST_ASSERT(e.in_fight());
    // A scan arriving with the load names no one: it used to take Lumiel's name off
    // her id, stale after the load, and she fell out of the party.
    proto::Identity look_alike = player(4, "Lumiel");
    look_alike.scanned = true;
    e.on_event(ev_on(1, at_s(3), look_alike));
    e.on_event(ev(at_s(4), hit(kAlly, kBoss, 300)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(s.visit_ts, at_s(1));
    TEST_ASSERT_EQ(s.combatants.size(), size_t{2});
    TEST_ASSERT_EQ(damage_by_name(s, "Lumiel"), std::optional<uint64_t>{300});
    TEST_ASSERT_EQ(s.boss.id, kBoss);

    // A load with no connection to go by is a new visit.
    e.on_event(ev(at_s(10), proto::MapLoad{3, kDungeon}));
    TEST_ASSERT_FALSE(e.in_fight());
    TEST_ASSERT_EQ(e.take_finished().size(), size_t{1});
}

TEST_CASE(Engine, IdsHandedToOtherPlayersOnANewMapKeepThemApart) {
    // Ids are the server's per map, and it reuses them: on the second map the
    // Ranger has the Elementalist's old id, and on the third they swap back.
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), roster({"Ariel", "Lumiel", "Rhea", "Kaisinel"})));
    e.on_event(ev(at_s(0), proto::MapLoad{1, 610010}));
    e.on_event(ev(at_s(0), player(301, "Rhea")));
    e.on_event(ev(at_s(0), player(302, "Kaisinel")));
    e.on_event(ev(at_s(0), npc(kBoss, kBossHp, 777)));
    e.on_event(ev(at_s(1), hit(301, kBoss, 100)));
    e.on_event(ev(at_s(1), hit(302, kBoss, 200)));

    e.on_event(ev(at_s(10), proto::MapLoad{2, 620020}));
    e.on_event(ev(at_s(10), player(302, "Rhea")));
    e.on_event(ev(at_s(10), player(305, "Kaisinel")));
    e.on_event(ev(at_s(10), npc(0x4200, kBossHp, 888)));
    e.on_event(ev(at_s(11), hit(302, 0x4200, 1000)));
    e.on_event(ev(at_s(11), hit(305, 0x4200, 500)));
    FightSummary s = e.current();
    TEST_ASSERT_EQ(s.combatants.size(), size_t{2});
    TEST_ASSERT_EQ(damage_by_name(s, "Rhea"), std::optional<uint64_t>{1000});
    TEST_ASSERT_EQ(damage_by_name(s, "Kaisinel"), std::optional<uint64_t>{500});

    e.on_event(ev(at_s(20), proto::MapLoad{3, 630030}));
    e.on_event(ev(at_s(20), player(305, "Rhea")));
    e.on_event(ev(at_s(20), player(302, "Kaisinel")));
    e.on_event(ev(at_s(20), npc(0x4300, kBossHp, 999)));
    e.on_event(ev(at_s(21), hit(305, 0x4300, 700)));
    e.on_event(ev(at_s(21), hit(302, 0x4300, 300)));
    s = e.current();
    TEST_ASSERT_EQ(s.combatants.size(), size_t{2});
    TEST_ASSERT_EQ(damage_by_name(s, "Rhea"), std::optional<uint64_t>{700});
    TEST_ASSERT_EQ(damage_by_name(s, "Kaisinel"), std::optional<uint64_t>{300});
}

TEST_CASE(Engine, AnOldPlayersIdCanBeTheNextMapsBoss) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), player(301, "Rhea")));
    e.on_event(ev(at_s(10), proto::MapLoad{2, 620020}));
    e.on_event(ev(at_s(10), npc(301, kBossHp, 999)));
    e.on_event(ev(at_s(11), hit(kMe, 301, 1000)));
    const FightSummary s = e.current();
    TEST_ASSERT(e.in_fight());
    TEST_ASSERT_EQ(s.total_damage, uint64_t{1000});
    TEST_ASSERT_EQ(s.boss.id, EntityId{301});
    TEST_ASSERT(s.boss.is_boss);
}

TEST_CASE(Engine, ANewRunOfTheSameMapKeepsItsPlayersApart) {
    EncounterEngine e = setup();
    e.on_event(ev_on(1, at_s(0), proto::MapLoad{1, 610010}));
    e.on_event(ev(at_s(0), player(301, "Rhea")));
    e.on_event(ev(at_s(0), player(302, "Kaisinel")));
    e.on_event(ev(at_s(0), npc(kBoss, kBossHp, 777)));
    e.on_event(ev(at_s(1), hit(301, kBoss, 100)));
    e.on_event(ev(at_s(1), hit(302, kBoss, 200)));
    // The same map again, over a new connection: a new run, whose instance hands
    // out new ids.
    e.on_event(ev_on(2, at_s(5), proto::MapLoad{2, 610010}));
    e.on_event(ev(at_s(5), player(302, "Rhea")));
    e.on_event(ev(at_s(5), player(305, "Kaisinel")));
    e.on_event(ev(at_s(5), npc(kBoss, kBossHp, 777)));
    e.on_event(ev(at_s(6), hit(302, kBoss, 1000)));
    e.on_event(ev(at_s(6), hit(305, kBoss, 500)));
    TEST_ASSERT(e.in_fight());
    const FightSummary first = e.take_finished().at(0);
    TEST_ASSERT(first.end_reason == EndReason::MapChange);
    TEST_ASSERT_EQ(damage_by_name(first, "Rhea"), std::optional<uint64_t>{100});
    TEST_ASSERT_EQ(damage_by_name(first, "Kaisinel"), std::optional<uint64_t>{200});
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(s.combatants.size(), size_t{2});
    TEST_ASSERT_EQ(damage_by_name(s, "Rhea"), std::optional<uint64_t>{1000});
    TEST_ASSERT_EQ(damage_by_name(s, "Kaisinel"), std::optional<uint64_t>{500});
}
