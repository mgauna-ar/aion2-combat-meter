#include "test_framework.hpp"

#include "game/npc_names.hpp"

using namespace aion2;
using namespace aion2::game;

// The codes below are questlog.gg's, as npcs.tsv lists them.

TEST_CASE(NpcNames, MobsFromTheFirstCapture) {
    // Every mob spawn in the first capture carried one of these two.
    TEST_ASSERT_EQ(std::string(npc_name(2'100'795)), std::string("Highland Odyle Sparkie"));
    TEST_ASSERT_EQ(std::string(npc_name(2'100'740)), std::string("Docile Highland Brax"));
}

TEST_CASE(NpcNames, ABossHasAnIdPerDifficulty) {
    TEST_ASSERT_EQ(npc_label(2'300'171), std::string("Ultimate Berk"));
    TEST_ASSERT_EQ(npc_label(2'300'172), std::string("Ultimate Berk"));
    // Ludra's Conquest, Easy and Hard tiers: aion2.app's lists gave the first alone.
    TEST_ASSERT_EQ(npc_label(2'301'014), std::string("Eternal Ludra"));
    TEST_ASSERT_EQ(npc_label(2'301'038), std::string("Eternal Ludra"));
    TEST_ASSERT_EQ(npc_label(2'300'290), std::string("Eternal Ludra"));
}

TEST_CASE(NpcNames, MechanicsAndSummonsAreNamed) {
    // From the captures, none on aion2.app's lists: an Elementalist's spirit, a boss
    // mechanic aimed at a player, and a summon Ludra's boss owned.
    TEST_ASSERT_EQ(npc_label(2'920'107), std::string("Fire Spirit"));
    TEST_ASSERT_EQ(npc_label(2'920'360), std::string("Vakron's Thorn Root"));
    TEST_ASSERT_EQ(npc_label(2'920'671), std::string("Abyssal Wings Ketu"));
}

TEST_CASE(NpcNames, UnlistedCodesHaveALabel) {
    // A summon Ludra's boss owned, which questlog.gg names by a developer's
    // identifier (M_Summon_Kashapa_01_V01_018): no name to show.
    TEST_ASSERT(npc_name(2'920'702).empty());
    TEST_ASSERT_EQ(npc_label(2'920'702), std::string("NPC #2920702"));
    // What the old spawn read gave every mob.
    TEST_ASSERT_EQ(npc_label(8206), std::string("NPC #8206"));
}
