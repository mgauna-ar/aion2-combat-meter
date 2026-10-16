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

TEST_CASE(Registry, SameNameOnTwoIdsIsOnePlayer) {
    CombatantRegistry r;
    r.on_identity(player(100, "Ariel", true));
    r.on_identity(player(140, "Ariel", false));
    TEST_ASSERT_EQ(r.resolve(140), EntityId{100});
    // A different server is a different character.
    r.on_identity(player(150, "Ariel", false, 0x0999));
    TEST_ASSERT_EQ(r.resolve(150), EntityId{150});
}

TEST_CASE(Registry, MapChangeKeepsPlayersDropsNpcs) {
    CombatantRegistry r;
    r.on_identity(player(100, "Ariel", true));
    r.on_spawn(npc(0x4100, 1000));
    r.observe_hit(100, 9001);
    r.on_map_change();
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
    // No roster: every player counts.
    TEST_ASSERT(r.in_party(300));
    r.on_party(roster({"Ariel", "Lumiel"}));
    TEST_ASSERT(r.in_party(100));
    TEST_ASSERT(r.in_party(200));
    TEST_ASSERT_FALSE(r.in_party(300));
}
