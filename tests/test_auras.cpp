#include "test_framework.hpp"
#include "meter_test_support.hpp"

#include "auras/aura_plugin.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace aion2;
using namespace aion2::auras;
using namespace aion2::test::meter_support;

namespace {

constexpr EntityId kMe = 100;
constexpr EntityId kChanter = 200;
constexpr EntityId kBoss = 0x4100;
constexpr uint32_t kDungeon = 610010;
/// Undefeated Mantra, as the second capture had it: effect 181900211 from skill 18190021.
constexpr uint32_t kMantra = 181'900'211;
constexpr uint32_t kMantraSkill = 18'190'021;
/// Dodge's effect on the player who used it.
constexpr uint32_t kDodge = 3015;
constexpr uint32_t kDodgeSkill = 12'000'100;
/// A boss's effect, as the second capture had it: no name.
constexpr uint32_t kBossDebuff = 16'044'202;

/// 00 8D with someone's HP and their max HP (stat 7).
proto::RemainingHp hp_with_max(EntityId id, uint64_t value, uint64_t max) {
    proto::RemainingHp v = hp(id, value);
    v.has_max_hp = true;
    v.max_hp = max;
    return v;
}

/// 00 8D with only someone's max HP, as the local player's login sends it.
proto::RemainingHp max_hp_only(EntityId id, uint64_t max) {
    proto::RemainingHp v;
    v.id = id;
    v.has_max_hp = true;
    v.max_hp = max;
    return v;
}

/// The server's clock at capture time `ts` when it runs `ahead_ms` ahead.
uint64_t server_ms(Micros ts, int64_t ahead_ms = 0) {
    return static_cast<uint64_t>(static_cast<int64_t>(ts / kMicrosPerMilli) + ahead_ms);
}

/// A dungeon with the local player, a Chanter and a boss, the server clock 3 s ahead.
AuraPlugin setup() {
    AuraPlugin e;
    e.on_event(ev(at_s(0), proto::MapLoad{1, kDungeon}));
    e.on_event(ev(at_s(0), player(kMe, "Ariel", true)));
    e.on_event(ev(at_s(0), player(kChanter, "Lumiel")));
    e.on_event(ev(at_s(0), npc(kBoss, 5'000'000, 777)));
    e.on_event(ev(at_s(0), proto::Heartbeat{server_ms(at_s(0), 3000)}));
    return e;
}

} // namespace

TEST_CASE(Auras, AnEffectRunsUntilTheServersStatedEnd) {
    AuraPlugin e = setup();
    const Micros t = at_s(1);
    e.on_event(ev(t, buff(kMe, kChanter, 1, kMantra, 2400, server_ms(t, 3000) + 2400, kMantraSkill)));
    const AuraSnapshot a = e.snapshot(t);
    TEST_ASSERT_EQ(a.on_self.size(), size_t{1});
    TEST_ASSERT_EQ(a.on_self[0].label, std::string("Undefeated Mantra"));
    TEST_ASSERT_EQ(a.on_self[0].key, uint32_t{18'190'000});
    TEST_ASSERT_EQ(a.on_self[0].expires_at, t + 2'400'000);
    TEST_ASSERT(!a.on_self[0].mine);
    TEST_ASSERT(!a.on_self[0].hostile);

    e.tick(at_s(3.5));
    TEST_ASSERT(e.snapshot(at_s(3.5)).on_self.empty());
}

TEST_CASE(Auras, ARenewalEndsWhenTheServerSays) {
    // Renewals state the whole duration, while the end they state can come up to
    // 1.4 s sooner: the end is what counts.
    AuraPlugin e = setup();
    const Micros t = at_s(1);
    e.on_event(ev(t, buff(kMe, kMe, 7, 118'000'071, 3650, server_ms(t, 3000) + 2900)));
    TEST_ASSERT_EQ(e.snapshot(t).on_self[0].expires_at, t + 2'900'000);

    // An end far from the duration is not believed.
    e.on_event(ev(t, buff(kMe, kMe, 8, 127'700'071, 5000, server_ms(t, 3000) + 60'000)));
    const AuraSnapshot a = e.snapshot(t);
    TEST_ASSERT_EQ(a.on_self.size(), size_t{2});
    const auto roar = std::find_if(a.on_self.begin(), a.on_self.end(),
                                   [](const AuraEntry& x) { return x.buff_id == 127'700'071; });
    TEST_ASSERT(roar != a.on_self.end());
    TEST_ASSERT_EQ(roar->expires_at, t + 5'000'000);
}

TEST_CASE(Auras, EffectsKeepThePlaceTheyWerePutOnIn) {
    // By time left, every renewal and every proc reshuffled the list.
    AuraPlugin e = setup();
    e.on_event(ev(at_s(1), buff(kMe, kMe, 1, 127'800'011, 20'000, 0, 12'780'001)));  // Fury, 20 s
    e.on_event(ev(at_s(2), buff(kMe, kMe, 2, kDodge, 3000, 0, kDodgeSkill)));
    AuraSnapshot a = e.snapshot(at_s(2));
    TEST_ASSERT_EQ(a.on_self.size(), size_t{2});
    TEST_ASSERT_EQ(a.on_self[0].label, std::string("Fury"));  // ends last, came first
    TEST_ASSERT_EQ(a.on_self[1].label, std::string("Dodge"));

    // Renewed until long after Fury's end: Dodge stays second while both run.
    proto::BuffApplied renewal = buff(kMe, kMe, 2, kDodge, 30'000, 0, kDodgeSkill);
    renewal.renewed = true;
    e.on_event(ev(at_s(3), renewal));
    a = e.snapshot(at_s(3));
    TEST_ASSERT_EQ(a.on_self[0].label, std::string("Fury"));
    TEST_ASSERT_EQ(a.on_self[1].applied_at, at_s(2));
}

TEST_CASE(Auras, AShortEffectKeptUpByRenewalsIsSustained) {
    // A Chanter's mantra: 2.4 s, renewed each second, all fight.
    AuraPlugin e = setup();
    e.on_event(ev(at_s(1), buff(kMe, kChanter, 1, kMantra, 2400, 0, kMantraSkill)));
    TEST_ASSERT(!e.snapshot(at_s(1)).on_self[0].sustained);
    proto::BuffApplied renewal = buff(kMe, kChanter, 1, kMantra, 2400, 0, kMantraSkill);
    renewal.renewed = true;
    for (int s = 2; s <= 8; ++s) e.on_event(ev(at_s(s), renewal));
    // Renewed, but not yet for twice its length: still a countdown.
    TEST_ASSERT(!e.snapshot(at_s(3)).on_self[0].sustained);
    const AuraSnapshot a = e.snapshot(at_s(8));
    TEST_ASSERT(a.on_self[0].sustained);
    TEST_ASSERT_EQ(a.on_self[0].stacks, uint32_t{1});

    // A long effect keeps its countdown however often renewed: it says when to
    // use the skill again.
    e.on_event(ev(at_s(1), buff(kMe, kMe, 2, 127'800'011, 20'000, 0, 12'780'001)));
    proto::BuffApplied fury = buff(kMe, kMe, 2, 127'800'011, 20'000, 0, 12'780'001);
    fury.renewed = true;
    e.on_event(ev(at_s(15), fury));
    e.on_event(ev(at_s(30), fury));
    e.on_event(ev(at_s(45), fury));
    const AuraSnapshot late = e.snapshot(at_s(50));
    TEST_ASSERT_EQ(late.on_self.size(), size_t{1});
    TEST_ASSERT(!late.on_self[0].sustained);
}

TEST_CASE(Auras, OneSkillsEffectsAreOneEntry) {
    // Doom Shield put two effects on in the second capture, 120700271 and
    // 120700071: two rows with one name.
    AuraPlugin e = setup();
    e.on_event(ev(at_s(1), buff(kMe, kMe, 1, 120'700'271, 5000, 0, 12'070'027)));
    e.on_event(ev(at_s(1), buff(kMe, kMe, 2, 120'700'071, 6000, 0, 12'070'027)));
    AuraSnapshot a = e.snapshot(at_s(1));
    TEST_ASSERT_EQ(a.on_self.size(), size_t{1});
    TEST_ASSERT_EQ(a.on_self[0].stacks, uint32_t{1});  // another effect is no stack
    TEST_ASSERT_EQ(a.on_self[0].expires_at, at_s(7));

    // The same skill from someone else is theirs.
    e.on_event(ev(at_s(1), buff(kMe, kChanter, 3, 120'700'071, 6000, 0, 12'070'027)));
    TEST_ASSERT_EQ(e.snapshot(at_s(1)).on_self.size(), size_t{2});
}

TEST_CASE(Auras, APassiveSentAgainIsNoStackAndGivesWayToItsProc) {
    // A map sends each passive up to three times, under new instance numbers.
    AuraPlugin e = setup();
    e.on_event(ev(at_s(1), permanent_buff(kMe, 1, 127'300'001)));
    e.on_event(ev(at_s(1), permanent_buff(kMe, 2, 127'300'001)));
    e.on_event(ev(at_s(1), permanent_buff(kMe, 3, 127'300'001)));
    AuraSnapshot a = e.snapshot(at_s(1));
    TEST_ASSERT_EQ(a.on_self.size(), size_t{1});
    TEST_ASSERT(a.on_self[0].permanent);
    TEST_ASSERT_EQ(a.on_self[0].stacks, uint32_t{1});

    // Punishing Benediction going off: the one entry counts it down, then is the
    // passive again.
    e.on_event(ev(at_s(2), buff(kMe, kMe, 4, 127'300'021, 10'000, 0, 12'730'002)));
    a = e.snapshot(at_s(2));
    TEST_ASSERT_EQ(a.on_self.size(), size_t{1});
    TEST_ASSERT(!a.on_self[0].permanent);
    TEST_ASSERT_EQ(a.on_self[0].expires_at, at_s(12));
    TEST_ASSERT_EQ(a.on_self[0].stacks, uint32_t{1});
    TEST_ASSERT(e.snapshot(at_s(13)).on_self[0].permanent);
}

TEST_CASE(Auras, InstancesStackAndARemovalTakesTheSoonest) {
    AuraPlugin e = setup();
    e.on_event(ev(at_s(1), buff(kMe, kMe, 10, kDodge, 3000, 0, kDodgeSkill)));
    e.on_event(ev(at_s(2), buff(kMe, kMe, 11, kDodge, 3000, 0, kDodgeSkill)));
    AuraSnapshot a = e.snapshot(at_s(2));
    TEST_ASSERT_EQ(a.on_self.size(), size_t{1});
    TEST_ASSERT_EQ(a.on_self[0].stacks, uint32_t{2});
    TEST_ASSERT_EQ(a.on_self[0].expires_at, at_s(5));
    TEST_ASSERT(a.on_self[0].mine);

    e.on_event(ev(at_s(2.5), proto::BuffRemoved{kMe, kDodge}));
    a = e.snapshot(at_s(2.5));
    TEST_ASSERT_EQ(a.on_self[0].stacks, uint32_t{1});
    TEST_ASSERT_EQ(a.on_self[0].expires_at, at_s(5));

    // Renewed: the instance runs on.
    proto::BuffApplied renewal = buff(kMe, kMe, 11, kDodge, 3000, 0, kDodgeSkill);
    renewal.renewed = true;
    e.on_event(ev(at_s(4), renewal));
    TEST_ASSERT_EQ(e.snapshot(at_s(4)).on_self[0].expires_at, at_s(7));

    e.on_event(ev(at_s(4.1), proto::BuffRemoved{kMe, kDodge}));
    TEST_ASSERT(e.snapshot(at_s(4.1)).on_self.empty());
}

TEST_CASE(Auras, AMarkerThatEndsAsItStartsIsNotKept) {
    AuraPlugin e = setup();
    e.on_event(ev(at_s(1), buff(kMe, kMe, 3, 122'400'072, 0)));
    TEST_ASSERT(e.snapshot(at_s(1)).on_self.empty());
}

TEST_CASE(Auras, PermanentEffectsComeLast) {
    AuraPlugin e = setup();
    e.on_event(ev(at_s(1), permanent_buff(kMe, 1, 127'100'001)));
    e.on_event(ev(at_s(1), buff(kMe, kMe, 2, kDodge, 3000, 0, kDodgeSkill)));
    const AuraSnapshot a = e.snapshot(at_s(100));  // long after Dodge's
    TEST_ASSERT_EQ(a.on_self.size(), size_t{1});
    TEST_ASSERT(a.on_self[0].permanent);
    TEST_ASSERT_EQ(e.snapshot(at_s(1)).on_self[1].label, std::string("Enhance Health"));
}

TEST_CASE(Auras, AMapLoadKeepsOnlyTheNewMapsEffects) {
    // The new map's effects come shortly before its 21 36; the old map's go.
    AuraPlugin e = setup();
    e.on_event(ev(at_s(1), permanent_buff(kMe, 1, 127'100'001)));
    e.on_event(ev(at_s(9.6), permanent_buff(kMe, 1, 127'200'001)));
    e.on_event(ev(at_s(10), proto::MapLoad{1, kDungeon + 1}));
    const AuraSnapshot a = e.snapshot(at_s(10));
    TEST_ASSERT_EQ(a.on_self.size(), size_t{1});
    TEST_ASSERT_EQ(a.on_self[0].label, std::string("Warding Shield"));
}

TEST_CASE(Auras, ADeathClearsTheEffects) {
    AuraPlugin e = setup();
    e.on_event(ev(at_s(1), buff(kBoss, kMe, 1, kDodge, 30'000)));
    e.on_event(ev(at_s(2), proto::Death{kBoss}));
    e.tick(at_s(2));
    TEST_ASSERT(e.snapshot(at_s(2)).on_target.empty());
}

TEST_CASE(Auras, TheTargetShowsOnlyTheLocalPlayersEffects) {
    AuraPlugin e = setup();
    e.on_event(ev(at_s(1), hit(kMe, kBoss, 1000)));
    e.on_event(ev(at_s(1), buff(kBoss, kMe, 1, 122'700'011, 8000, 0, 12'270'001)));
    e.on_event(ev(at_s(1), buff(kBoss, kChanter, 2, 182'900'071, 8000, 0, 18'290'007)));
    e.on_event(ev(at_s(1), buff(kMe, kBoss, 3, kBossDebuff, 10'000, 0, 1'604'420)));
    const AuraSnapshot a = e.snapshot(at_s(1));
    TEST_ASSERT_EQ(a.target, kBoss);
    TEST_ASSERT_EQ(a.on_target.size(), size_t{1});
    TEST_ASSERT_EQ(a.on_target[0].label, std::string("Debilitating Smash"));
    TEST_ASSERT(a.on_target[0].mine);
    TEST_ASSERT_EQ(a.on_self.size(), size_t{1});
    TEST_ASSERT(a.on_self[0].hostile);
    // An enemy's effect with no name takes the enemy's.
    TEST_ASSERT_EQ(a.on_self[0].label, std::string("NPC #777"));

    TEST_ASSERT_EQ(a.target_label, std::string("NPC #777"));

    // Not hit for a while: no target.
    TEST_ASSERT_EQ(e.snapshot(at_s(1) + Entities::kTargetMemory + 1).target, EntityId{0});
}

TEST_CASE(Auras, EffectsSeenOnTheLocalPlayerAreRemembered) {
    AuraPlugin e = setup();
    // Before the local player is known their effects are kept; they count as seen
    // once the plugin knows whose they are.
    AuraPlugin fresh;
    fresh.on_event(ev(at_s(1), buff(kMe, kChanter, 1, kMantra, 2400, 0, kMantraSkill)));
    fresh.on_event(ev(at_s(1), self_values(kMe)));
    fresh.tick(at_s(1.5));
    TEST_ASSERT_EQ(fresh.snapshot(at_s(1.5)).seen.size(), size_t{1});

    e.on_event(ev(at_s(1), buff(kMe, kChanter, 1, kMantra, 2400, 0, kMantraSkill)));
    e.on_event(ev(at_s(1), buff(kMe, kMe, 2, kDodge, 3000, 0, kDodgeSkill)));
    e.on_event(ev(at_s(1), buff(kChanter, kChanter, 3, 181'600'211, 2400, 0, 18'160'021)));
    e.on_event(ev(at_s(1), buff(kMe, kBoss, 4, kBossDebuff, 2400)));  // an enemy's: to hide, not to watch
    e.tick(at_s(2));
    e.tick(at_s(10));  // long gone, still seen
    const AuraSnapshot a = e.snapshot(at_s(10));
    TEST_ASSERT(a.on_self.empty());
    TEST_ASSERT_EQ(a.seen.size(), size_t{3});
    TEST_ASSERT_EQ(a.seen[0].label, std::string("Dodge"));
    TEST_ASSERT(!a.seen[0].hostile);
    TEST_ASSERT_EQ(a.seen[1].label, std::string("NPC #777"));
    TEST_ASSERT(a.seen[1].hostile);
    TEST_ASSERT_EQ(a.seen[1].key, kBossDebuff);
    TEST_ASSERT_EQ(a.seen[2].label, std::string("Undefeated Mantra"));
}

// --- Cooldowns (01 38, 47 38, plugins/auras/data/cooldowns.tsv) ---

namespace {

constexpr Micros kSec = kMicrosPerSecond;

/// The skills still cooling at `now`: a snapshot keeps the ready ones too.
size_t cooling(const std::vector<CooldownEntry>& shown, Micros now) {
    return static_cast<size_t>(
        std::count_if(shown.begin(), shown.end(), [&](const CooldownEntry& e) { return e.ready_at > now; }));
}

} // namespace

TEST_CASE(Cooldowns, TheTableLessThePickedSpecialties) {
    // Warding Strike: 30 s, specialty 1 cuts 5 s. Cast as 12350130: 1 and 3 picked.
    TEST_ASSERT_EQ(listed_cooldown_ms(12'350'130), uint32_t{25'000});
    TEST_ASSERT_EQ(listed_cooldown_ms(12'350'030), uint32_t{30'000});
    // Shield Rush and Shield Smite: specialty 4 cuts 10 s and 2 s.
    TEST_ASSERT_EQ(listed_cooldown_ms(12'430'240), uint32_t{10'000});
    TEST_ASSERT_EQ(listed_cooldown_ms(12'100'030), uint32_t{10'000});
    TEST_ASSERT_EQ(listed_cooldown_ms(12'100'040), uint32_t{8'000});
    // A stigma keeps every specialty it unlocked, counted by the tens digit: Taunt's
    // third cuts 5 s.
    TEST_ASSERT(find_cooldown(12'120'000) != nullptr && find_cooldown(12'120'000)->stigma);
    TEST_ASSERT_EQ(listed_cooldown_ms(12'120'020), uint32_t{30'000});
    TEST_ASSERT_EQ(listed_cooldown_ms(12'120'030), uint32_t{25'000});
    // Basic attacks and unknown codes have none.
    TEST_ASSERT_EQ(listed_cooldown_ms(12'010'340), uint32_t{0});
    TEST_ASSERT_EQ(listed_cooldown_ms(999), uint32_t{0});
}

TEST_CASE(Cooldowns, AUseStartsTheEstimateAndTheServerCorrectsIt) {
    CooldownTracker c;
    const Micros t = at_s(10);
    c.on_used(12'350'130, t);
    auto shown = c.snapshot(t);
    TEST_ASSERT_EQ(shown.size(), size_t{1});
    TEST_ASSERT_EQ(shown[0].label, std::string("Warding Strike"));
    TEST_ASSERT_EQ(shown[0].ready_at, t + 25 * kSec);
    TEST_ASSERT(!shown[0].exact);

    // Vicious Strike's specialty cut it: the server says what is left.
    c.on_set(12'350'000, 20'400, t + 700'000);
    shown = c.snapshot(t + 700'000);
    TEST_ASSERT_EQ(shown[0].ready_at, t + 21'100'000);
    TEST_ASSERT(shown[0].exact);
    // A reset: ready now, and still listed, so the overlay can light it up.
    c.on_set(12'350'000, 0, t + 2 * kSec);
    shown = c.snapshot(t + 2 * kSec);
    TEST_ASSERT_EQ(shown.size(), size_t{1});
    TEST_ASSERT_EQ(cooling(shown, t + 2 * kSec), size_t{0});
    TEST_ASSERT_EQ(shown[0].ready_at, t + 2 * kSec);
    // The gap across that cut and reset teaches nothing.
    c.on_used(12'350'130, t + 3 * kSec);
    TEST_ASSERT_EQ(c.estimate_ms(12'350'130), uint64_t{25'000});
}

TEST_CASE(Cooldowns, TheQuickestReuseShortensTheEstimate) {
    // Shield Smite, 10 s by the table, used again after 8.85 s: the character's
    // cooldown reduction, which the table does not know.
    CooldownTracker c;
    c.on_used(12'100'030, at_s(0));
    c.on_used(12'100'030, at_s(9.5));
    TEST_ASSERT_EQ(c.estimate_ms(12'100'030), uint64_t{9'500});
    c.on_used(12'100'030, at_s(18.35));
    TEST_ASSERT_EQ(c.estimate_ms(12'100'030), uint64_t{8'850});
    TEST_ASSERT_EQ(c.snapshot(at_s(18.35))[0].ready_at, at_s(18.35) + 8'850'000);
    // A slower reuse changes nothing; one under half the figure is a trigger.
    c.on_used(12'100'030, at_s(40));
    c.on_used(12'100'030, at_s(43));
    TEST_ASSERT_EQ(c.estimate_ms(12'100'030), uint64_t{8'850});
    // Other specialties picked: start again from the table.
    c.on_used(12'100'040, at_s(60));
    TEST_ASSERT_EQ(c.estimate_ms(12'100'040), uint64_t{8'000});
}

TEST_CASE(Cooldowns, ThePluginShowsTheLocalPlayersAndForgetsAnotherCharacters) {
    AuraPlugin e = setup();
    e.on_event(ev(at_s(1), proto::SkillUsed{12'100'030}));
    e.on_event(ev(at_s(9), proto::SkillUsed{12'100'030}));
    TEST_ASSERT_EQ(e.snapshot(at_s(9)).cooldowns.size(), size_t{1});
    TEST_ASSERT_EQ(e.snapshot(at_s(9)).cooldowns[0].ready_at, at_s(17));

    // Logged in as someone else: nothing learned carries over.
    e.on_event(ev(at_s(20), player(kMe + 1, "Zikel", true)));
    e.on_event(ev(at_s(21), proto::SkillUsed{12'100'030}));
    TEST_ASSERT_EQ(e.snapshot(at_s(21)).cooldowns[0].ready_at, at_s(31));
}

TEST_CASE(Cooldowns, ASecondUseShowsOnlyOnceBothAreSpent) {
    // Shield of Protection, a stigma: its first specialty is "+1 consecutive use".
    // Cast as 12110010 it has unlocked that one.
    TEST_ASSERT_EQ(skill_charges(12'110'010), uint32_t{2});
    TEST_ASSERT_EQ(skill_charges(12'110'000), uint32_t{1});
    TEST_ASSERT_EQ(skill_charges(12'350'130), uint32_t{1});
    CooldownTracker c;
    c.on_used(12'110'010, at_s(0));
    TEST_ASSERT_EQ(cooling(c.snapshot(at_s(0)), at_s(0)), size_t{0});  // one use left
    c.on_used(12'110'010, at_s(3.35));
    auto shown = c.snapshot(at_s(3.35));
    TEST_ASSERT_EQ(shown.size(), size_t{1});
    TEST_ASSERT_EQ(shown[0].ready_at, at_s(20));  // the first use's, ready soonest
    // The quick second use taught nothing.
    TEST_ASSERT_EQ(c.estimate_ms(12'110'010), uint64_t{20'000});
}

// --- Who is who (Entities): the plugin's own record, not the meter's registry ---

TEST_CASE(AuraEntities, TheLocalPlayerFromTheirIdentityOrTheirOwnValues) {
    Entities w;
    w.on_identity(player(kMe, "Ariel", true));
    TEST_ASSERT_EQ(w.local(), kMe);
    TEST_ASSERT_EQ(w.local_character(), std::string("Ariel@291"));
    // A new map, a new id: only their own values say which.
    w.on_map_load(false);
    w.on_values(self_values(kMe + 7));
    TEST_ASSERT_EQ(w.local(), EntityId{kMe + 7});
    // An NPC is never the local player.
    w.on_spawn(npc(kBoss, 5'000'000, 777));
    w.on_values(self_values(kBoss));
    TEST_ASSERT_EQ(w.local(), EntityId{kMe + 7});
    // Another player's identity on the local player's old id: no longer theirs.
    w.on_identity(player(kMe + 7, "Lumiel"));
    TEST_ASSERT_EQ(w.local(), EntityId{0});
}

TEST_CASE(AuraEntities, SummonsAreTheirOwnersAndMobsAreEnemies) {
    Entities w;
    w.on_identity(player(kMe, "Ariel", true));
    w.on_spawn(summon(0x4200, kMe));
    w.on_spawn(npc(kBoss, 5'000'000, 777));
    TEST_ASSERT(w.mine(0x4200));
    TEST_ASSERT(w.mine(kMe));
    TEST_ASSERT(!w.mine(kChanter));
    TEST_ASSERT(w.is_enemy(kBoss));
    TEST_ASSERT(!w.is_enemy(0x4200));
    TEST_ASSERT(!w.is_enemy(kChanter));
    // A spawn for a player named on this map is them coming into view.
    w.on_identity(player(kChanter, "Lumiel"));
    w.on_spawn(npc(kChanter, 20'000));
    TEST_ASSERT(!w.is_enemy(kChanter));
}

TEST_CASE(AuraEntities, AMapLoadForgetsTheSpawnsButAReloadKeepsThem) {
    AuraPlugin p = setup();
    // Entered over its own connection: what the town showed is gone.
    p.on_event(ev_on(1, at_s(1), proto::MapLoad{2, kDungeon}));
    TEST_ASSERT(!p.entities().is_enemy(kBoss));
    p.on_event(ev_on(1, at_s(1.5), npc(kBoss, 5'000'000, 777)));
    TEST_ASSERT(p.entities().is_enemy(kBoss));
    // The same map over the same connection: a teleport or a revive within the run.
    p.on_event(ev_on(1, at_s(2), proto::MapLoad{3, kDungeon}));
    TEST_ASSERT(p.entities().is_enemy(kBoss));
    // Over a new connection: the dungeon again, its ids new.
    p.on_event(ev_on(2, at_s(3), proto::MapLoad{4, kDungeon}));
    TEST_ASSERT(!p.entities().is_enemy(kBoss));
}

TEST_CASE(AuraTarget, TheBiggestEnemyHitLatelyWins) {
    constexpr EntityId kAdd = 0x4101;
    AuraPlugin p = setup();
    p.on_event(ev(at_s(0), npc(kAdd, 300'000, 778)));
    p.on_event(ev(at_s(1), hit(kMe, kBoss, 1000)));
    p.on_event(ev(at_s(2), hit(kMe, kAdd, 1000)));
    TEST_ASSERT_EQ(p.snapshot(at_s(2)).target, kBoss);
    // Another player's hits pick nothing.
    AuraPlugin q = setup();
    q.on_event(ev(at_s(1), hit(kChanter, kBoss, 1000)));
    TEST_ASSERT_EQ(q.snapshot(at_s(1)).target, EntityId{0});
    // A summon's hit counts as its owner's.
    q.on_event(ev(at_s(1), summon(0x4200, kMe)));
    q.on_event(ev(at_s(2), hit(0x4200, kBoss, 1000)));
    TEST_ASSERT_EQ(q.snapshot(at_s(2)).target, kBoss);

    // HP 0 is the boss's death: the add is what is left.
    p.on_event(ev(at_s(3), hp(kBoss, 0)));
    TEST_ASSERT_EQ(p.snapshot(at_s(3)).target, kAdd);
    TEST_ASSERT_EQ(p.snapshot(at_s(3)).target_label, std::string("NPC #778"));
}

TEST_CASE(AuraPluginState, ClearForgetsEverything) {
    AuraPlugin p = setup();
    p.on_event(ev(at_s(1), buff(kMe, kChanter, 1, kMantra, 2400, 0, kMantraSkill)));
    p.on_event(ev(at_s(1), proto::SkillUsed{12'100'030}));
    p.on_event(ev(at_s(1), proto::ChainOffered{12'010'340, 12'020'340}));
    p.on_event(ev(at_s(1), hp_with_max(kMe, 5'000, 25'373)));
    TEST_ASSERT(!p.snapshot(at_s(1)).on_self.empty());
    TEST_ASSERT(!p.snapshot(at_s(1)).chains.empty());
    p.clear();
    const AuraSnapshot a = p.snapshot(at_s(1));
    TEST_ASSERT(a.on_self.empty());
    TEST_ASSERT(a.cooldowns.empty());
    TEST_ASSERT(a.chains.empty());
    TEST_ASSERT_EQ(a.max_hp, uint64_t{0});
    TEST_ASSERT_EQ(p.entities().local(), EntityId{0});
}

// --- Chain skills on offer (18 38, 19 38) ---

TEST_CASE(Chains, AnOfferStandsUntilTheServerWithdrawsIt) {
    AuraPlugin e = setup();
    // Vicious Strike offers Decisive Strike, which offers Desperate Strike.
    e.on_event(ev(at_s(1), proto::ChainOffered{12'010'340, 12'020'340}));
    AuraSnapshot a = e.snapshot(at_s(1.2));
    TEST_ASSERT_EQ(a.chains.size(), size_t{1});
    TEST_ASSERT_EQ(a.chains[0].label, std::string("Decisive Strike"));
    TEST_ASSERT_EQ(a.chains[0].trigger, uint32_t{12'010'340});
    TEST_ASSERT_EQ(a.chains[0].offered_at, at_s(1));
    e.on_event(ev(at_s(1.6), proto::ChainOffered{12'020'340, 12'030'340}));
    e.on_event(ev(at_s(1.6), proto::ChainWithdrawn{12'020'340}));
    a = e.snapshot(at_s(1.6));
    TEST_ASSERT_EQ(a.chains.size(), size_t{1});
    TEST_ASSERT_EQ(a.chains[0].label, std::string("Desperate Strike"));
    // A withdrawal matches by skill: another variant's code takes it off too.
    e.on_event(ev(at_s(2), proto::ChainWithdrawn{12'030'000}));
    TEST_ASSERT(e.snapshot(at_s(2)).chains.empty());
}

TEST_CASE(Chains, ALostWithdrawalOrAMapLoadEndsAnOffer) {
    AuraPlugin e = setup();
    e.on_event(ev(at_s(1), proto::ChainOffered{12'040'230, 12'060'230}));
    // The server lets it run out after about 3 s; its 19 38 never came.
    e.tick(at_s(4));
    TEST_ASSERT_EQ(e.snapshot(at_s(4)).chains.size(), size_t{1});
    TEST_ASSERT(e.snapshot(at_s(1) + ChainTracker::kMaxOffer).chains.empty());
    e.tick(at_s(1) + ChainTracker::kMaxOffer);
    TEST_ASSERT(e.chains().snapshot(at_s(1)).empty());

    // Offered again before its end: one entry, from the new offer.
    e.on_event(ev(at_s(10), proto::ChainOffered{12'040'230, 12'060'230}));
    e.on_event(ev(at_s(11), proto::ChainOffered{12'040'230, 12'060'230}));
    TEST_ASSERT_EQ(e.snapshot(at_s(11)).chains.size(), size_t{1});
    TEST_ASSERT_EQ(e.snapshot(at_s(11)).chains[0].offered_at, at_s(11));
    e.on_event(ev(at_s(12), proto::MapLoad{2, kDungeon + 1}));
    TEST_ASSERT(e.snapshot(at_s(12)).chains.empty());
}

// --- The local player's HP (00 8D stats 0 and 7) ---

TEST_CASE(AuraHealth, TheLocalPlayersHpAndTheirStatedMax) {
    AuraPlugin e = setup();
    TEST_ASSERT_EQ(e.snapshot(at_s(1)).max_hp, uint64_t{0});
    // Before the max comes, the most HP seen stands in for it.
    e.on_event(ev(at_s(1), hp(kMe, 21'000)));
    e.on_event(ev(at_s(2), hp(kMe, 18'000)));
    AuraSnapshot a = e.snapshot(at_s(2));
    TEST_ASSERT_EQ(a.hp, uint64_t{18'000});
    TEST_ASSERT_EQ(a.max_hp, uint64_t{21'000});
    // The stated max, on a record of its own.
    e.on_event(ev(at_s(3), max_hp_only(kMe, 25'373)));
    a = e.snapshot(at_s(3));
    TEST_ASSERT_EQ(a.hp, uint64_t{18'000});
    TEST_ASSERT_EQ(a.max_hp, uint64_t{25'373});
    // Others' HP is not the local player's.
    e.on_event(ev(at_s(4), hp(kChanter, 900)));
    e.on_event(ev(at_s(4), hp(kBoss, 4'000'000)));
    TEST_ASSERT_EQ(e.snapshot(at_s(4)).hp, uint64_t{18'000});

    // A new map, a new id: nothing known until it is sent again.
    e.on_event(ev(at_s(10), proto::MapLoad{2, kDungeon}));
    e.on_event(ev(at_s(10), player(kMe + 5, "Ariel", true)));
    a = e.snapshot(at_s(10));
    TEST_ASSERT_EQ(a.hp, uint64_t{0});
    TEST_ASSERT_EQ(a.max_hp, uint64_t{0});
    e.on_event(ev(at_s(11), hp_with_max(kMe + 5, 24'000, 25'373)));
    a = e.snapshot(at_s(11));
    TEST_ASSERT_EQ(a.hp, uint64_t{24'000});
    TEST_ASSERT_EQ(a.max_hp, uint64_t{25'373});
}
