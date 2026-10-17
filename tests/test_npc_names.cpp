#include "test_framework.hpp"

#include "meter/npc_names.hpp"

using namespace aion2;
using namespace aion2::meter;

// The codes below are aion2.app's, as npcs.tsv lists them.

TEST_CASE(NpcNames, MobsFromTheFirstCapture) {
    // Every mob spawn in the first capture carried one of these two.
    TEST_ASSERT_EQ(std::string(npc_name(2'100'795)), std::string("Highland Odyle Sparkie"));
    TEST_ASSERT_EQ(std::string(npc_name(2'100'740)), std::string("Docile Highland Brax"));
}

TEST_CASE(NpcNames, ABossHasAnIdPerDifficulty) {
    TEST_ASSERT_EQ(npc_label(2'300'171), std::string("Ultimate Berk"));
    TEST_ASSERT_EQ(npc_label(2'300'172), std::string("Ultimate Berk"));
}

TEST_CASE(NpcNames, UnlistedCodesHaveALabel) {
    // A summon's code, from the first capture: not in the lists.
    TEST_ASSERT(npc_name(2'920'107).empty());
    TEST_ASSERT_EQ(npc_label(2'920'107), std::string("NPC #2920107"));
    // What the old spawn read gave every mob.
    TEST_ASSERT_EQ(npc_label(8206), std::string("NPC #8206"));
}
