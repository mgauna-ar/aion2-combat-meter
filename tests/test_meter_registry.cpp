#include "test_framework.hpp"
#include "meter_test_support.hpp"

#include "meter/combatant_registry.hpp"

using namespace aion2;
using namespace aion2::meter;
using namespace aion2::test::meter_support;

TEST_CASE(Registry, IdentityAndSpawnSetSides) {
    CombatantRegistry r;
    r.on_identity(player(100, "Ariel", true));
    r.on_spawn(npc(0x4100, 5'000'000));
    TEST_ASSERT(r.side(100) == Side::Player);
    TEST_ASSERT(r.side(0x4100) == Side::Enemy);
    TEST_ASSERT_EQ(r.local_player(), EntityId{100});
    TEST_ASSERT_EQ(r.display_name(100), std::string("Ariel"));
    TEST_ASSERT_EQ(r.display_name(0x4100), std::string("#16640"));
}

TEST_CASE(Registry, SummonResolvesToOwner) {
    CombatantRegistry r;
    r.on_identity(player(100, "Ariel", true));
    r.on_spawn(summon(0x4200, 100));
    TEST_ASSERT_EQ(r.resolve(0x4200), EntityId{100});
    TEST_ASSERT(r.side(0x4200) == Side::Player);
    // A summon whose owner never arrived still fights for the players.
    r.on_spawn(summon(0x4201, 0));
    TEST_ASSERT(r.side(0x4201) == Side::Player);
}

TEST_CASE(Registry, SelfValuesNameTheLocalPlayer) {
    CombatantRegistry r;
    // The meter started after login: no 33 36, but 00 8D sends the player their own values.
    r.on_self_values(6745);
    TEST_ASSERT_EQ(r.local_player(), EntityId{6745});
    TEST_ASSERT(r.side(6745) == Side::Player);
    TEST_ASSERT(r.in_party(6745));
    TEST_ASSERT_EQ(r.display_name(6745), std::string("You"));
    r.on_identity(player(6745, "Ariel", true));
    TEST_ASSERT_EQ(r.display_name(6745), std::string("Ariel"));

    // An NPC or summon is never taken for the player.
    r.on_spawn(npc(0x4100, 1000));
    r.on_self_values(0x4100);
    TEST_ASSERT_EQ(r.local_player(), EntityId{6745});
}

TEST_CASE(Registry, HitOnASpawnedNpcOverridesAHealInference) {
    CombatantRegistry r;
    r.on_spawn(npc(18024, 119'700));
    // A heal between two players nobody knows yet, then a mob hits one of them.
    r.observe_hit(12914, 50363);
    r.observe_hit(18024, 50363);
    TEST_ASSERT(r.side(50363) == Side::Player);
    TEST_ASSERT(r.side(12914) == Side::Enemy);
    // Hitting the mob settles it: only players do that.
    r.observe_hit(12914, 18024);
    TEST_ASSERT(r.side(12914) == Side::Player);
}

TEST_CASE(Registry, SummonDeathLeavesOwnerAlive) {
    CombatantRegistry r;
    r.on_identity(player(100, "Ariel", true));
    r.on_spawn(summon(0x4200, 100));
    r.on_death(0x4200);
    TEST_ASSERT(r.find(0x4200)->dead);
    TEST_ASSERT_FALSE(r.find(100)->dead);
}

TEST_CASE(Registry, LiveHpBoundsAnUnspawnedMax) {
    CombatantRegistry r;
    r.on_hp(0x4100, 114'988);
    r.on_hp(0x4100, 98'613);
    TEST_ASSERT_EQ(r.find(0x4100)->live_hp, uint64_t{98'613});
    TEST_ASSERT_EQ(r.find(0x4100)->max_hp, uint64_t{114'988});
}

TEST_CASE(Registry, OwnerLoopIsBounded) {
    CombatantRegistry r;
    r.on_spawn(summon(0x4300, 0x4301));
    r.on_spawn(summon(0x4301, 0x4300));
    (void)r.resolve(0x4300);  // must return
    TEST_ASSERT(r.side(0x4300) == Side::Player);
}

TEST_CASE(Registry, HitsInferUnknownSides) {
    CombatantRegistry r;
    r.on_identity(player(100, "Ariel", true));
    r.observe_hit(100, 9001);  // an NPC the meter never saw spawn
    TEST_ASSERT(r.side(9001) == Side::Enemy);
    r.observe_hit(9001, 200);  // it hits someone else: a player
    TEST_ASSERT(r.side(200) == Side::Player);
}

TEST_CASE(Registry, PendingHitsSettleOnAnchor) {
    CombatantRegistry r;
    r.observe_hit(300, 9100);
    r.observe_hit(301, 9100);
    TEST_ASSERT(r.side(300) == Side::Unknown);
    TEST_ASSERT_EQ(r.pending_edges(), size_t{2});
    r.on_identity(player(300, "Lumiel"));
    TEST_ASSERT(r.side(9100) == Side::Enemy);
    TEST_ASSERT(r.side(301) == Side::Player);
    TEST_ASSERT_EQ(r.pending_edges(), size_t{0});
}

TEST_CASE(Registry, FoldedRangeGuessIsAnEnemy) {
    CombatantRegistry r;
    TEST_ASSERT(r.side_or_guess(0x4500) == Side::Enemy);
    TEST_ASSERT(r.side_or_guess(77) == Side::Unknown);
    r.observe_hit(77, 0x4500);
    TEST_ASSERT(r.side(77) == Side::Player);
}

TEST_CASE(Registry, SameNameOnTwoIdsIsOneCharacter) {
    CombatantRegistry r;
    r.on_identity(player(100, "Ariel", true));
    r.on_identity(player(140, "Ariel", false));
    TEST_ASSERT(r.character_of(140) != 0);
    TEST_ASSERT_EQ(r.character_of(140), r.character_of(100));
    // Never joined as ids: each stays its own.
    TEST_ASSERT_EQ(r.resolve(140), EntityId{140});
    // A different server is a different character.
    r.on_identity(player(150, "Ariel", false, 0x0999));
    TEST_ASSERT(r.character_of(150) != r.character_of(100));
}

TEST_CASE(Registry, AnIdNamedAgainIsTheNewCharacter) {
    CombatantRegistry r;
    r.on_identity(player(301, "Rhea"));
    r.on_skill_class(301, AionClass::Ranger);
    r.on_identity(player(301, "Kaisinel"));
    TEST_ASSERT_EQ(r.display_name(301), std::string("Kaisinel"));
    // The Ranger's skills were Rhea's, not Kaisinel's.
    TEST_ASSERT(r.class_of(301) == AionClass::Unknown);
}

TEST_CASE(Registry, ANameOnANewIdLeavesItsIdFromAnEarlierMap) {
    CombatantRegistry r;
    r.on_identity(player(301, "Rhea"));
    r.on_identity(player(302, "Kaisinel"));
    r.on_map_change(false);
    // Still named until something says otherwise: ids may outlive the map.
    TEST_ASSERT_EQ(r.display_name(301), std::string("Rhea"));
    r.on_identity(player(305, "Rhea"));
    TEST_ASSERT_EQ(r.display_name(305), std::string("Rhea"));
    TEST_ASSERT(r.find(301) == nullptr);
    // On the same map, both ids keep the name: they may both be in use.
    r.on_identity(player(306, "Rhea"));
    TEST_ASSERT_EQ(r.display_name(305), std::string("Rhea"));
    TEST_ASSERT_EQ(r.display_name(306), std::string("Rhea"));
}

TEST_CASE(Registry, AStaleIdGivesWayToASpawn) {
    CombatantRegistry r;
    r.on_identity(player(301, "Rhea"));
    // On the map it was named on, a spawn is the player coming into view.
    r.on_spawn(npc(301, 1000));
    TEST_ASSERT(r.side(301) == Side::Player);
    r.on_map_change(true);
    r.on_spawn(npc(301, 1000));
    TEST_ASSERT(r.side(301) == Side::Enemy);
    TEST_ASSERT_EQ(r.display_name(301), std::string("#301"));
}

TEST_CASE(Registry, TheLocalPlayersNameFollowsThemToANewId) {
    CombatantRegistry r;
    r.on_identity(player(100, "Ariel", true));
    r.on_map_change(false);
    r.on_self_values(130);
    TEST_ASSERT_EQ(r.local_player(), EntityId{130});
    TEST_ASSERT_EQ(r.display_name(130), std::string("Ariel"));
    TEST_ASSERT(r.find(100) == nullptr);
    // Their old id, given to a spawn on the next map, is the spawn's.
    r.on_identity(player(140, "Lumiel"));
    r.on_map_change(false);
    r.on_spawn(npc(130, 1000));
    TEST_ASSERT_EQ(r.local_player(), EntityId{0});
    r.on_self_values(150);
    TEST_ASSERT_EQ(r.display_name(150), std::string("Ariel"));
}

TEST_CASE(Registry, MapChangeKeepsPlayersDropsNpcs) {
    CombatantRegistry r;
    r.on_identity(player(100, "Ariel", true));
    r.on_spawn(npc(0x4100, 1000));
    r.observe_hit(100, 9001);
    r.on_map_change(false);
    TEST_ASSERT(r.find(0x4100) == nullptr);
    TEST_ASSERT(r.side(9001) == Side::Unknown);
    TEST_ASSERT_EQ(r.display_name(100), std::string("Ariel"));
    TEST_ASSERT_EQ(r.local_player(), EntityId{100});
}

TEST_CASE(Registry, PartyMembershipFollowsRoster) {
    CombatantRegistry r;
    r.on_identity(player(100, "Ariel", true));
    r.on_identity(player(200, "Lumiel"));
    r.on_identity(player(300, "Stranger"));
    const CharacterId stranger = r.character_of(300);
    // No roster, and no map seen loading: only the local player counts.
    TEST_ASSERT(r.in_party(100));
    TEST_ASSERT_FALSE(r.in_party(300));
    TEST_ASSERT(r.character_in_party(r.local_character()));
    TEST_ASSERT_FALSE(r.character_in_party(stranger));
    // In an instance, every player.
    r.on_map_change(true);
    TEST_ASSERT(r.in_party(300));
    TEST_ASSERT(r.character_in_party(stranger));
    r.on_party(roster({"Ariel", "Lumiel"}));
    TEST_ASSERT(r.in_party(100));
    TEST_ASSERT(r.in_party(200));
    TEST_ASSERT_FALSE(r.in_party(300));
    TEST_ASSERT_FALSE(r.character_in_party(stranger));
    // Back in the open world with the party gone: only the local player.
    r.on_party(roster({}));
    r.on_map_change(false);
    TEST_ASSERT(r.in_party(100));
    TEST_ASSERT_FALSE(r.in_party(200));
}

TEST_CASE(Registry, SummonsOwnerIsAPlayerBeforeTheirName) {
    CombatantRegistry r;
    r.on_spawn(summon(0x4200, 300));
    TEST_ASSERT(r.side(300) == Side::Player);
    // So its hit on a target nothing else told us about settles that target.
    r.observe_hit(0x4200, 900);
    TEST_ASSERT(r.side(900) == Side::Enemy);
    // A name later is still the same player.
    r.on_identity(player(300, "Lumiel"));
    TEST_ASSERT(r.side(300) == Side::Player);
}

TEST_CASE(Registry, AScannedIdentityNamesNoOne) {
    // A scan pairs a name with some other number: in the second capture, none of
    // its 38 had the named character's id.
    const auto scanned = [](EntityId id, const char* name) {
        proto::Identity i = player(id, name);
        i.scanned = true;
        return i;
    };
    CombatantRegistry r;
    r.on_spawn(npc(0x4100, 5'000'000));
    r.on_identity(scanned(0x4100, "Bogus"));
    TEST_ASSERT(r.side(0x4100) == Side::Enemy);
    TEST_ASSERT_EQ(r.display_name(0x4100), std::string("#16640"));

    r.on_identity(player(100, "Ariel", true));
    r.on_identity(player(301, "Rhea"));
    r.on_party(roster({"Ariel", "Rhea"}));
    r.on_identity(scanned(301, "Kaisinel"));
    TEST_ASSERT_EQ(r.display_name(301), std::string("Rhea"));
    r.on_identity(scanned(778, "Lumiel"));
    TEST_ASSERT(r.find(778) == nullptr);
    // After a map load, before Rhea's identity comes again, her id is stale. A scan
    // with her name used to move it to its own number and forget hers, and she fell
    // out of the party.
    r.on_map_reload();
    r.on_identity(scanned(4, "Rhea"));
    TEST_ASSERT(r.find(4) == nullptr);
    TEST_ASSERT_EQ(r.display_name(301), std::string("Rhea"));
    TEST_ASSERT(r.in_party(301));
}

TEST_CASE(Registry, NamelessOwnIdentityKeepsTheIdsName) {
    CombatantRegistry r;
    r.on_identity(player(100, "Ariel"));
    r.on_identity(player(140, "Ariel"));
    proto::Identity own;
    own.id = 140;
    own.own = true;
    r.on_identity(own);
    TEST_ASSERT_EQ(r.local_player(), EntityId{140});
    TEST_ASSERT_EQ(r.display_name(140), std::string("Ariel"));
    TEST_ASSERT(r.in_party(140));
}
