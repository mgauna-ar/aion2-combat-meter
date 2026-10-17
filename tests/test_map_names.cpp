#include "test_framework.hpp"

#include "meter/map_names.hpp"

using namespace aion2;
using namespace aion2::meter;

// The ids below are aion2.app's dungeon tier ids, as maps.tsv lists them. Whether 21 36
// carries the same ids is unchecked (AGENTS.md).

TEST_CASE(MapNames, EachTierHasItsOwnRow) {
    TEST_ASSERT_EQ(std::string(map_name(600002)), std::string("Krao Cave (Exploration)"));
    TEST_ASSERT_EQ(std::string(map_name(600001)), std::string("Krao Cave (Conquest)"));
    TEST_ASSERT_EQ(std::string(map_name(610052)), std::string("Deus Research Base (Stage 4)"));
    TEST_ASSERT_EQ(map_label(620021), std::string("Chalice of Muspel (Conquest [Hard])"));
}

TEST_CASE(MapNames, AOneTierPageKeepsItsOwnName) {
    // The Awakening pages name their tier already: no "(Challenge) (Extreme)".
    TEST_ASSERT_EQ(std::string(map_name(910064)), std::string("Chamber of the Dead (Challenge)"));
}

TEST_CASE(MapNames, UnlistedIdsHaveALabel) {
    // Open-world zones have no public ids, so none is listed.
    TEST_ASSERT(map_name(400001).empty());
    TEST_ASSERT_EQ(map_label(400001), std::string("Map 400001"));
    TEST_ASSERT_EQ(map_label(0), std::string("Unknown map"));
    TEST_ASSERT(map_name(0).empty());
}
