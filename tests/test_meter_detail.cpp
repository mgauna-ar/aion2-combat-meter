#include "test_framework.hpp"
#include "meter_test_support.hpp"

#include "game/skill_names.hpp"
#include "meter/encounter_engine.hpp"
#include "meter/fight_detail.hpp"

#include <algorithm>
#include <numeric>
#include <string>

using namespace aion2;
using namespace aion2::meter;
using namespace aion2::test::meter_support;

namespace {

constexpr EntityId kMe = 100;
constexpr EntityId kAlly = 200;
constexpr EntityId kSpirit = 0x4300;
constexpr EntityId kBoss = 0x4100;
constexpr uint64_t kBossHp = 5'000'000;
constexpr uint32_t kBossCode = 777;
constexpr uint32_t kDungeon = 610010;
/// Undefeated Mantra, as the second capture had it.
constexpr uint32_t kMantra = 181'900'211;
constexpr uint32_t kMantraSkill = 18'190'021;
/// A monster's curse: no name.
constexpr uint32_t kCurse = 20'111'011;

/// A dungeon, where every player counts without a roster, with the local player,
/// an ally and a boss in view.
EncounterEngine setup(MeterSettings settings = {}) {
    EncounterEngine e(settings);
    e.on_event(ev(at_s(0), proto::MapLoad{1, kDungeon}));
    e.on_event(ev(at_s(0), player(kMe, "Ariel", true)));
    e.on_event(ev(at_s(0), player(kAlly, "Lumiel")));
    e.on_event(ev(at_s(0), npc(kBoss, kBossHp, kBossCode)));
    return e;
}

const FightDetail& detail_of(const FightSummary& s) {
    TEST_ASSERT(s.detail != nullptr);
    return *s.detail;
}

const TimelineRow& timeline_row(const FightSummary& s, EntityId id) {
    const TimelineRow* row = detail_of(s).timeline.row(id);
    TEST_ASSERT(row != nullptr);
    return *row;
}

const CombatantTotals& row_of(const FightSummary& s, EntityId id) {
    const auto it = std::find_if(s.combatants.begin(), s.combatants.end(),
                                 [id](const CombatantTotals& c) { return c.id == id; });
    TEST_ASSERT(it != s.combatants.end());
    return *it;
}

uint64_t sum_damage(const std::vector<TimelineBin>& bins) {
    return std::accumulate(bins.begin(), bins.end(), uint64_t{0},
                           [](uint64_t a, const TimelineBin& b) { return a + b.damage; });
}

/// The rows of one side.
std::vector<EffectUptimeRow> effects(const FightSummary& s, EffectSide side) {
    std::vector<EffectUptimeRow> out;
    for (const EffectUptimeRow& r : detail_of(s).effects) {
        if (r.side == side) out.push_back(r);
    }
    return out;
}

} // namespace

// ---- Timeline ---------------------------------------------------------------

TEST_CASE(Engine, TimelineBinsFollowTheSecondOfEachHit) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(10), hit(kMe, kBoss, 1000)));
    e.on_event(ev(at_s(10.5), hit(kMe, kBoss, 500)));
    e.on_event(ev(at_s(12.2), hit(kAlly, kBoss, 2000)));
    e.on_event(ev(at_s(13), dot(kMe, kBoss, 300)));
    const FightSummary s = e.current();
    const TimelineRow& me = timeline_row(s, kMe);
    TEST_ASSERT_EQ(me.bins.size(), size_t{4});
    TEST_ASSERT_EQ(me.bins[0].damage, uint32_t{1500});
    TEST_ASSERT_EQ(me.bins[3].damage, uint32_t{300});
    TEST_ASSERT_EQ(sum_damage(me.bins), row_of(s, kMe).damage);
    TEST_ASSERT_EQ(timeline_row(s, kAlly).bins[2].damage, uint32_t{2000});
    TEST_ASSERT_EQ(detail_of(s).timeline.seconds, uint32_t{4});
}

TEST_CASE(Engine, TimelineKeepsHealingAndTakenApart) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(1), hit(kAlly, kBoss, 100)));
    e.on_event(ev(at_s(2), hit(kBoss, kMe, 700)));
    e.on_event(ev(at_s(3), heal_tick(kAlly, kMe, 400)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(timeline_row(s, kMe).bins[1].taken, uint32_t{700});
    TEST_ASSERT_EQ(timeline_row(s, kMe).bins[1].damage, uint32_t{0});
    TEST_ASSERT_EQ(timeline_row(s, kAlly).bins[2].healing, uint32_t{400});
}

TEST_CASE(Engine, ASummonsDamageLandsInItsOwnersSeconds) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), summon(kSpirit, kMe)));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(2), hit(kSpirit, kBoss, 500)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(timeline_row(s, kMe).bins[1].damage, uint32_t{500});
    TEST_ASSERT(detail_of(s).timeline.row(kSpirit) == nullptr);
}

TEST_CASE(Engine, ATimelineRowJoinsItsCharacter) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(300, kBoss, 1'000)));
    e.on_event(ev(at_s(2), player(300, "Lumiel")));
    e.on_event(ev(at_s(3), hit(kAlly, kBoss, 500)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(detail_of(s).timeline.rows.size(), size_t{1});
    TEST_ASSERT_EQ(sum_damage(timeline_row(s, kAlly).bins), uint64_t{1'500});
}

TEST_CASE(Engine, TimelineStopsAtAnHour) {
    MeterSettings settings;
    settings.idle_timeout_s = 7200;
    settings.boss_idle_timeout_s = 7200;
    settings.capture_lost_s = 7200;
    EncounterEngine e = setup(settings);
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(3700), hit(kMe, kBoss, 900)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(row_of(s, kMe).damage, uint64_t{1000});
    TEST_ASSERT_EQ(sum_damage(timeline_row(s, kMe).bins), uint64_t{100});
    TEST_ASSERT_EQ(detail_of(s).timeline.seconds, static_cast<uint32_t>(kTimelineMaxSeconds));
}

TEST_CASE(Engine, BossHpLineIsPermilleCarriedForward) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(10), hit(kMe, kBoss, 1000)));
    e.on_event(ev(at_s(12.5), hp(kBoss, 4'000'000)));
    e.on_event(ev(at_s(15), hp(kBoss, 2'500'000)));
    e.on_event(ev(at_s(16), hit(kMe, kBoss, 1000)));
    const FightSummary s = e.current();
    const BossHpSeries& line = detail_of(s).timeline.boss_hp;
    // Its first figure in the fight came at second 2; the spawn's stood before it.
    TEST_ASSERT_EQ(line.first_s, uint32_t{0});
    TEST_ASSERT_EQ(line.at(0), 1000);
    TEST_ASSERT_EQ(line.at(1), 1000);
    TEST_ASSERT_EQ(line.at(2), 800);
    TEST_ASSERT_EQ(line.at(4), 800);
    TEST_ASSERT_EQ(line.at(5), 500);
    TEST_ASSERT_EQ(line.at(6), 500);
}

TEST_CASE(Engine, BossHpLineIgnoresDamage) {
    // Damage is in other units than HP: with no figure from the server, no line.
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 1'000'000)));
    e.on_event(ev(at_s(5), hit(kMe, kBoss, 1'000'000)));
    TEST_ASSERT(detail_of(e.current()).timeline.boss_hp.empty());
}

TEST_CASE(Engine, ABossKilledAfterTheLastHitStillReachesZero) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(10), hit(kMe, kBoss, 1000)));
    e.on_event(ev(at_s(14.9), hit(kMe, kBoss, 1000)));
    e.on_event(ev(at_s(15.2), hp(kBoss, 0)));
    e.tick(at_s(30));
    const FightSummary s = e.take_finished().at(0);
    const FightTimeline& tl = detail_of(s).timeline;
    TEST_ASSERT_EQ(tl.seconds, uint32_t{6});
    TEST_ASSERT_EQ(tl.boss_hp.at(5), 0);
    TEST_ASSERT_EQ(tl.boss_hp.at(4), 1000);
}

// ---- Active time -------------------------------------------------------------

TEST_CASE(Engine, ActiveTimeSumsGapsOfThreeSecondsOrLess) {
    EncounterEngine e = setup();
    for (const double t : {10.0, 11.0, 12.5, 16.0, 17.0, 20.0}) e.on_event(ev(at_s(t), hit(kMe, kBoss, 100)));
    const FightSummary s = e.current();
    // 1 + 1.5 + 1 + 3; the 3.5 s gap is not time spent hitting.
    TEST_ASSERT_NEAR(row_of(s, kMe).active_s, 6.5, 1e-9);
    TEST_ASSERT_NEAR(row_of(s, kMe).active_share(s.duration_s), 0.65, 1e-9);
}

TEST_CASE(Engine, ActiveTimeIgnoresTicksAndSummons) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), summon(kSpirit, kMe)));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(2), dot(kMe, kBoss, 100)));
    e.on_event(ev(at_s(3), hit(kSpirit, kBoss, 100)));
    e.on_event(ev(at_s(4.5), hit(kMe, kBoss, 100)));
    TEST_ASSERT_NEAR(row_of(e.current(), kMe).active_s, 0.0, 1e-9);
}

TEST_CASE(Engine, ActiveTimeBridgesAnIdNamedLate) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(300, kBoss, 100)));
    e.on_event(ev(at_s(2), player(300, "Lumiel")));
    e.on_event(ev(at_s(3), hit(300, kBoss, 100)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(s.combatants.size(), size_t{1});
    TEST_ASSERT_NEAR(s.combatants[0].active_s, 2.0, 1e-9);
}

TEST_CASE(Engine, FinishedFightsShareTheirDetail) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.tick(at_s(100));
    const FightSummary s = e.take_finished().at(0);
    TEST_ASSERT(s.detail != nullptr);
    TEST_ASSERT(s.detail == e.recent().back().detail);
    TEST_ASSERT(s.detail == e.current().detail);
}

// ---- Deaths ------------------------------------------------------------------

TEST_CASE(Engine, AnHpOfZeroIsADeathWithItsRecap) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(1.5), hit(kAlly, kBoss, 100)));
    e.on_event(ev(at_s(2), skilled(hit(kBoss, kAlly, 4000), 2'000'101)));
    e.on_event(ev(at_s(3), dot(kBoss, kAlly, 300)));
    e.on_event(ev(at_s(3.5), heal_tick(kMe, kAlly, 1800, 17'010'000)));
    e.on_event(ev(at_s(4), skilled(hit(kBoss, kAlly, 60'000, true), 2'000'102)));
    e.on_event(ev(at_s(4), hp(kAlly, 0)));
    const FightSummary s = e.current();
    const auto& deaths = detail_of(s).deaths;
    TEST_ASSERT_EQ(deaths.size(), size_t{1});
    const DeathRecord& d = deaths[0];
    TEST_ASSERT_EQ(d.player, kAlly);
    TEST_ASSERT_EQ(d.name, std::string("Lumiel"));
    TEST_ASSERT_NEAR(d.time_s, 3.0, 1e-9);
    TEST_ASSERT_FALSE(d.up());
    TEST_ASSERT_EQ(d.recap.size(), size_t{4});
    TEST_ASSERT(d.recap[0].kind == RecapKind::Hit);
    TEST_ASSERT_NEAR(d.recap[0].before_s, 2.0, 1e-9);
    TEST_ASSERT_EQ(d.recap[0].source_npc, kBossCode);
    TEST_ASSERT(d.recap[1].kind == RecapKind::Dot);
    TEST_ASSERT(d.recap[2].kind == RecapKind::HealTick);
    TEST_ASSERT_EQ(d.recap[2].healer, std::string("Ariel"));
    const RecapEvent* blow = d.killing_blow();
    TEST_ASSERT(blow != nullptr);
    TEST_ASSERT_EQ(blow->amount, uint64_t{60'000});
    TEST_ASSERT(blow->crit);
    TEST_ASSERT_NEAR(blow->before_s, 0.0, 1e-9);
}

TEST_CASE(Engine, RepeatedZerosAreOneDeath) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kAlly, kBoss, 100)));
    e.on_event(ev(at_s(2), hp(kAlly, 0)));
    e.on_event(ev(at_s(2.5), hp(kAlly, 0)));
    TEST_ASSERT_EQ(detail_of(e.current()).deaths.size(), size_t{1});
}

TEST_CASE(Engine, HpAboveZeroOrTheirOwnHitGetsThemUp) {
    EncounterEngine e = setup();
    // The local player stands throughout: no wipe.
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(1), hit(kAlly, kBoss, 100)));
    e.on_event(ev(at_s(4), hp(kAlly, 0)));
    e.on_event(ev(at_s(7), hp(kAlly, 5000)));
    e.on_event(ev(at_s(10), hp(kAlly, 0)));
    // A hit already in flight as they died, and their DoT ticking on: no proof
    // they are up.
    e.on_event(ev(at_s(10.05), hit(kAlly, kBoss, 100)));
    e.on_event(ev(at_s(11), dot(kAlly, kBoss, 30)));
    e.on_event(ev(at_s(12), hit(kAlly, kBoss, 100)));
    const FightSummary s = e.current();
    const auto& deaths = detail_of(s).deaths;
    TEST_ASSERT_EQ(deaths.size(), size_t{2});
    TEST_ASSERT_NEAR(deaths[0].up_after_s, 3.0, 1e-9);
    TEST_ASSERT_NEAR(deaths[1].up_after_s, 2.0, 1e-9);
}

TEST_CASE(Engine, ADeathStaysOpenToTheEnd) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(1), hit(kAlly, kBoss, 100)));
    e.on_event(ev(at_s(3), hp(kAlly, 0)));
    e.tick(at_s(100));
    const FightSummary s = e.take_finished().at(0);
    TEST_ASSERT_EQ(detail_of(s).deaths.size(), size_t{1});
    TEST_ASSERT_FALSE(detail_of(s).deaths[0].up());
}

TEST_CASE(Engine, DeathsOutsideAFightAreNotLogged) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hp(kAlly, 0)));
    e.on_event(ev(at_s(2), hp(kAlly, 9000)));
    e.on_event(ev(at_s(3), hit(kAlly, kBoss, 100)));
    TEST_ASSERT(detail_of(e.current()).deaths.empty());
}

TEST_CASE(Engine, StrangersAndSummonsDoNotDie) {
    // The open world: only the local player counts.
    EncounterEngine e;
    e.on_event(ev(at_s(0), player(kMe, "Ariel", true)));
    e.on_event(ev(at_s(0), player(kAlly, "Lumiel")));
    e.on_event(ev(at_s(0), npc(kBoss, kBossHp, kBossCode)));
    e.on_event(ev(at_s(0), summon(kSpirit, kMe)));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(1), hit(kAlly, kBoss, 100)));
    e.on_event(ev(at_s(2), hp(kAlly, 0)));
    e.on_event(ev(at_s(2), hp(kSpirit, 0)));
    TEST_ASSERT(detail_of(e.current()).deaths.empty());
}

TEST_CASE(Engine, AWipeEndsWithItsDeathsLogged) {
    MeterSettings settings;
    settings.wipe_detection = true;
    EncounterEngine e = setup(settings);
    e.on_event(ev(at_s(0), roster({"Ariel", "Lumiel"})));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(1), hit(kAlly, kBoss, 100)));
    e.on_event(ev(at_s(5), hp(kMe, 0)));
    e.on_event(ev(at_s(6), hp(kAlly, 0)));
    TEST_ASSERT_FALSE(e.in_fight());
    const FightSummary s = e.take_finished().at(0);
    TEST_ASSERT(s.end_reason == EndReason::Wipe);
    TEST_ASSERT_EQ(detail_of(s).deaths.size(), size_t{2});
}

TEST_CASE(Engine, ADeathAddsNoRow) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), roster({"Ariel", "Lumiel"})));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(2), hp(kAlly, 0)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(s.combatants.size(), size_t{1});
    TEST_ASSERT_EQ(detail_of(s).deaths.size(), size_t{1});
    TEST_ASSERT_EQ(detail_of(s).deaths[0].name, std::string("Lumiel"));
}

// ---- Effects -----------------------------------------------------------------

TEST_CASE(Engine, ABuffRunningBeforeTheFightCountsFromItsStart) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), buff(kMe, kAlly, 1, kMantra, 60'000, 0, kMantraSkill)));
    e.on_event(ev(at_s(10), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(20), hit(kMe, kBoss, 100)));
    const FightSummary s = e.current();
    const auto buffs = effects(s, EffectSide::PartyBuff);
    TEST_ASSERT_EQ(buffs.size(), size_t{1});
    TEST_ASSERT_EQ(buffs[0].target, kMe);
    TEST_ASSERT_EQ(buffs[0].target_name, std::string("Ariel"));
    TEST_ASSERT_EQ(buffs[0].caster, std::string("Lumiel"));
    TEST_ASSERT_EQ(buffs[0].applications, uint32_t{1});
    TEST_ASSERT_NEAR(buffs[0].active_s, 10.0, 1e-9);
    TEST_ASSERT_NEAR(buffs[0].uptime(s.duration_s), 1.0, 1e-9);
}

TEST_CASE(Engine, ARenewalExtendsOneSpan) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(10), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(11), buff(kMe, kMe, 7, kMantra, 5000, 0, kMantraSkill)));
    proto::BuffApplied renewal = buff(kMe, kMe, 7, kMantra, 5000, 0, kMantraSkill);
    renewal.renewed = true;
    e.on_event(ev(at_s(15), renewal));
    e.on_event(ev(at_s(25), hit(kMe, kBoss, 100)));
    const auto buffs = effects(e.current(), EffectSide::PartyBuff);
    TEST_ASSERT_EQ(buffs.size(), size_t{1});
    TEST_ASSERT_EQ(buffs[0].applications, uint32_t{1});
    TEST_ASSERT_NEAR(buffs[0].active_s, 9.0, 1e-9);
}

TEST_CASE(Engine, ARemovalOrADeathEndsAnEffect) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(10), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(10), hit(kAlly, kBoss, 100)));
    e.on_event(ev(at_s(11), buff(kMe, kAlly, 1, kMantra, 60'000, 0, kMantraSkill)));
    e.on_event(ev(at_s(11), buff(kAlly, kAlly, 1, kMantra, 60'000, 0, kMantraSkill)));
    e.on_event(ev(at_s(13), hp(kAlly, 0)));
    e.on_event(ev(at_s(14), proto::BuffRemoved{kMe, kMantra}));
    e.on_event(ev(at_s(25), hit(kMe, kBoss, 100)));
    const FightSummary s = e.current();
    for (const EffectUptimeRow& r : effects(s, EffectSide::PartyBuff)) {
        TEST_ASSERT_NEAR(r.active_s, r.target == kMe ? 3.0 : 2.0, 1e-9);
    }
    TEST_ASSERT_EQ(effects(s, EffectSide::PartyBuff).size(), size_t{2});
}

TEST_CASE(Engine, PermanentEffectsAreLeftOut) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), permanent_buff(kMe, 1, 127'100'001)));
    e.on_event(ev(at_s(10), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(11), permanent_buff(kMe, 2, 127'100'001)));
    e.on_event(ev(at_s(20), hit(kMe, kBoss, 100)));
    TEST_ASSERT(detail_of(e.current()).effects.empty());
}

TEST_CASE(Engine, DebuffsOnThePartyAndOnEnemiesKeepTheirSides) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), summon(kSpirit, kMe)));
    e.on_event(ev(at_s(10), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(11), buff(kMe, kBoss, 1, kCurse, 4000)));
    e.on_event(ev(at_s(12), buff(kBoss, kSpirit, 1, 153'200'011, 5000, 0, 15'320'001)));
    e.on_event(ev(at_s(20), hit(kMe, kBoss, 100)));
    const FightSummary s = e.current();
    const auto debuffs = effects(s, EffectSide::PartyDebuff);
    TEST_ASSERT_EQ(debuffs.size(), size_t{1});
    TEST_ASSERT_EQ(debuffs[0].caster, std::string("NPC #777"));
    TEST_ASSERT_NEAR(debuffs[0].active_s, 4.0, 1e-9);
    const auto on_boss = effects(s, EffectSide::OnEnemy);
    TEST_ASSERT_EQ(on_boss.size(), size_t{1});
    TEST_ASSERT_EQ(on_boss[0].target, kBoss);
    TEST_ASSERT_EQ(on_boss[0].target_name, std::string("NPC #777"));
    // A summon's effect is its owner's.
    TEST_ASSERT_EQ(on_boss[0].caster, std::string("Ariel"));
}

TEST_CASE(Engine, UptimeIsCappedAtTheFight) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), buff(kMe, kMe, 1, kMantra, 100'000, 0, kMantraSkill)));
    e.on_event(ev(at_s(10), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(20), hit(kMe, kBoss, 100)));
    e.tick(at_s(60));
    const FightSummary s = e.take_finished().at(0);
    const auto buffs = effects(s, EffectSide::PartyBuff);
    TEST_ASSERT_NEAR(buffs.at(0).active_s, 10.0, 1e-9);
}

TEST_CASE(Engine, APartyWideBuffBecomesOneWindow) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(10), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(10), hit(kAlly, kBoss, 100)));
    e.on_event(ev(at_s(11), buff(kMe, kAlly, 1, kMantra, 10'000, 0, kMantraSkill)));
    e.on_event(ev(at_s(11.5), buff(kAlly, kAlly, 1, kMantra, 10'000, 0, kMantraSkill)));
    // On one player only: no band.
    e.on_event(ev(at_s(12), buff(kMe, kMe, 2, 3015, 3000, 0, 12'000'100)));
    e.on_event(ev(at_s(25), hit(kMe, kBoss, 100)));
    const FightSummary s = e.current();
    const auto& windows = detail_of(s).timeline.buffs;
    TEST_ASSERT_EQ(windows.size(), size_t{1});
    TEST_ASSERT_EQ(windows[0].caster, std::string("Lumiel"));
    TEST_ASSERT_NEAR(windows[0].begin_s, 1.0, 1e-9);
    TEST_ASSERT_NEAR(windows[0].end_s, 11.5, 1e-9);
}

TEST_CASE(Engine, AFightKeepsAtMostTheSpanCap) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(10), hit(kMe, kBoss, 100)));
    for (size_t i = 0; i < EffectUptime::kMaxSpans + 10; ++i) {
        const Micros t = at_s(11) + i * 100;
        e.on_event(ev(t, buff(kMe, kMe, 1, kMantra, 60'000, 0, kMantraSkill)));
        e.on_event(ev(t + 50, proto::BuffRemoved{kMe, kMantra}));
    }
    e.on_event(ev(at_s(20), hit(kMe, kBoss, 100)));
    const auto buffs = effects(e.current(), EffectSide::PartyBuff);
    TEST_ASSERT_EQ(buffs.at(0).applications, static_cast<uint32_t>(EffectUptime::kMaxSpans));
}

// ---- Wipes -------------------------------------------------------------------

namespace {

EncounterEngine wipes_on() {
    MeterSettings settings;
    settings.wipe_detection = true;
    return setup(settings);
}

} // namespace

TEST_CASE(Engine, WipeDetectionIsOffByDefault) {
    // The whole party went down and got back up in the second capture's last fight.
    TEST_ASSERT_FALSE(MeterSettings{}.wipe_detection);
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(1), hit(kAlly, kBoss, 100)));
    e.on_event(ev(at_s(5), hp(kMe, 0)));
    e.on_event(ev(at_s(5), hp(kAlly, 0)));
    TEST_ASSERT(e.in_fight());
}

TEST_CASE(Engine, ARevivedPlayerStandsInTheWayOfAWipe) {
    EncounterEngine e = wipes_on();
    e.on_event(ev(at_s(0), roster({"Ariel", "Lumiel"})));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(1), hit(kAlly, kBoss, 100)));
    e.on_event(ev(at_s(5), hp(kMe, 0)));
    e.on_event(ev(at_s(8), hp(kMe, 30'000)));
    e.on_event(ev(at_s(9), hp(kAlly, 0)));
    TEST_ASSERT(e.in_fight());
    e.on_event(ev(at_s(10), hp(kMe, 0)));
    TEST_ASSERT_FALSE(e.in_fight());
    TEST_ASSERT(e.take_finished().at(0).end_reason == EndReason::Wipe);
}

TEST_CASE(Engine, AHitInFlightAsTheyDieDoesNotStopAWipe) {
    EncounterEngine e = wipes_on();
    e.on_event(ev(at_s(0), roster({"Ariel", "Lumiel"})));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(1), hit(kAlly, kBoss, 100)));
    e.on_event(ev(at_s(5), hp(kMe, 0)));
    e.on_event(ev(at_s(5.05), hit(kMe, kBoss, 100)));  // cast before they fell
    e.on_event(ev(at_s(6), hp(kAlly, 0)));
    TEST_ASSERT_FALSE(e.in_fight());
    TEST_ASSERT(e.take_finished().at(0).end_reason == EndReason::Wipe);
}

TEST_CASE(Engine, AnUnnamedAllyIsUpAgainByTheirHp) {
    // Known only from the hits they trade: HP above 0 still says they are up.
    EncounterEngine e = wipes_on();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(1), hit(300, kBoss, 100)));
    e.on_event(ev(at_s(4), hp(300, 0)));
    e.on_event(ev(at_s(6), hp(300, 20'000)));
    e.on_event(ev(at_s(7), hp(kMe, 0)));
    TEST_ASSERT(e.in_fight());
}

// ---- Casts ----------------------------------------------------------------------

namespace {

/// Pummel, and a variant of it (other specialties).
constexpr uint32_t kPummel = 12'040'230;
constexpr uint32_t kPummelVariant = 12'040'237;
/// A Templar passive going off, as the dungeon capture had it.
constexpr uint32_t kPassive = 12'730'001;

const SkillCasts* casts_of(const CombatantTotals& c, uint32_t code) {
    for (const SkillCasts& s : c.cast_skills) {
        if (s.code == game::skill_key(code)) return &s;
    }
    return nullptr;
}

} // namespace

TEST_CASE(Engine, PressesCountPerSkillAndProcsDoNot) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    for (const double t : {2.0, 4.0, 6.0}) e.on_event(ev(at_s(t), press(kMe, kPummel, kBoss)));
    e.on_event(ev(at_s(8), press(kMe, kPummelVariant, kBoss)));
    e.on_event(ev(at_s(5), proc(kMe, kPassive)));
    // An NPC's skill (7 digits) is not a player's cast.
    e.on_event(ev(at_s(6), press(kBoss, 2'900'100, kMe)));
    e.on_event(ev(at_s(9), hit(kMe, kBoss, 100)));
    const FightSummary s = e.current();
    TEST_ASSERT(s.has_casts);
    const CombatantTotals& me = row_of(s, kMe);
    TEST_ASSERT_EQ(me.casts, uint64_t{4});
    TEST_ASSERT_EQ(me.cast_skills.size(), size_t{1});
    const SkillCasts* pummel = casts_of(me, kPummel);
    TEST_ASSERT(pummel != nullptr);
    TEST_ASSERT_EQ(pummel->casts, uint64_t{4});
    TEST_ASSERT_NEAR(pummel->first_s, 1.0, 1e-9);
    TEST_ASSERT_NEAR(pummel->last_s, 7.0, 1e-9);
    TEST_ASSERT_NEAR(pummel->mean_gap_s(), 2.0, 1e-9);
    TEST_ASSERT_NEAR(me.casts_per_minute(s.duration_s), 30.0, 1e-9);
}

TEST_CASE(Engine, OnlyTheOpeningPressLeadsIntoAFight) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0.5), press(kMe, kPummel, kBoss)));
    e.on_event(ev(at_s(3.2), press(kMe, kPummel, kBoss)));
    // A press never starts a fight.
    TEST_ASSERT_FALSE(e.in_fight());
    e.on_event(ev(at_s(3.5), hit(kMe, kBoss, 100)));
    const FightSummary s = e.current();
    const CombatantTotals& me = row_of(s, kMe);
    // The one 0.3 s before the first hit counts; the one 3 s before does not.
    TEST_ASSERT_EQ(me.casts, uint64_t{1});
    TEST_ASSERT_NEAR(me.cast_skills.at(0).first_s, -0.3, 1e-9);
}

TEST_CASE(Engine, APressNeverExtendsAFight) {
    // A mob, not the boss: 10 s with no party hit ends the fight.
    constexpr EntityId kMob = 0x4200;
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), npc(kMob, 100'000)));
    e.on_event(ev(at_s(1), hit(kMe, kMob, 100)));
    e.on_event(ev(at_s(5), press(kMe, kPummel, kMob)));
    e.on_event(ev(at_s(9), press(kMe, kPummel, kMob)));
    e.tick(at_s(11.5));
    TEST_ASSERT_FALSE(e.in_fight());
    const FightSummary s = e.take_finished().at(0);
    TEST_ASSERT(s.end_reason == EndReason::Idle);
    TEST_ASSERT_EQ(row_of(s, kMe).casts, uint64_t{2});
    TEST_ASSERT_NEAR(s.duration_s, 1.0, 1e-9);
}

TEST_CASE(Engine, ASummonsSkillsAreNotItsOwnersCasts) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), summon(kSpirit, kMe)));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(2), press(kSpirit, 16'990'010, kBoss)));
    e.on_event(ev(at_s(3), press(kMe, kPummel, kBoss)));
    TEST_ASSERT_EQ(row_of(e.current(), kMe).casts, uint64_t{1});
}

TEST_CASE(Engine, AnAllysPressesCountOnceTheyHit) {
    // No roster: an ally counts once they hit, and their presses since the fight
    // began (and the one leading into their first hit) go in with them.
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(2), press(kAlly, kPummel, kBoss)));
    TEST_ASSERT_EQ(e.current().combatants.size(), size_t{1});
    e.on_event(ev(at_s(2.3), hit(kAlly, kBoss, 100)));
    e.on_event(ev(at_s(4), press(kAlly, kPummel, kBoss)));
    TEST_ASSERT_EQ(row_of(e.current(), kAlly).casts, uint64_t{2});
}

TEST_CASE(Engine, AStrangerOutsideTheRosterCastsNothing) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), player(300, "Zikel")));
    e.on_event(ev(at_s(0), roster({"Ariel", "Lumiel"})));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(2), press(300, kPummel, kBoss)));
    e.on_event(ev(at_s(2.2), hit(300, kBoss, 100)));
    // With a roster, a member's presses count before any hit of theirs.
    e.on_event(ev(at_s(3), press(kAlly, kPummel, kBoss)));
    e.on_event(ev(at_s(3.4), hit(kAlly, kBoss, 100)));
    const FightSummary s = e.current();
    for (const CombatantTotals& c : s.combatants) TEST_ASSERT(c.id != EntityId{300});
    TEST_ASSERT_EQ(row_of(s, kAlly).casts, uint64_t{1});
}

TEST_CASE(Engine, APressAddsNoOneToAFight) {
    // A member who only pressed (a buff, say) did not take part: no row of theirs.
    EncounterEngine e = setup();
    e.on_event(ev(at_s(0), roster({"Ariel", "Lumiel"})));
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 100)));
    e.on_event(ev(at_s(2), press(kAlly, kPummel, kAlly)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(s.combatants.size(), size_t{1});
    TEST_ASSERT_EQ(s.combatants[0].id, kMe);
}

TEST_CASE(Engine, ACharactersRowsJoinTheirCasts) {
    EncounterEngine e = setup();
    e.on_event(ev(at_s(1), hit(300, kBoss, 1'000)));
    e.on_event(ev(at_s(1.5), press(300, kPummel, kBoss)));
    e.on_event(ev(at_s(2), player(300, "Lumiel")));
    e.on_event(ev(at_s(3), hit(kAlly, kBoss, 500)));
    e.on_event(ev(at_s(4.5), press(kAlly, kPummelVariant, kBoss)));
    const FightSummary s = e.current();
    TEST_ASSERT_EQ(s.combatants.size(), size_t{1});
    const SkillCasts* pummel = casts_of(s.combatants[0], kPummel);
    TEST_ASSERT(pummel != nullptr);
    TEST_ASSERT_EQ(pummel->casts, uint64_t{2});
    TEST_ASSERT_NEAR(pummel->first_s, 0.5, 1e-9);
    TEST_ASSERT_NEAR(pummel->last_s, 3.5, 1e-9);
}
