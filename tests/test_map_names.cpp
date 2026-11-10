#include "test_framework.hpp"

#include "game/map_names.hpp"

using namespace aion2;
using namespace aion2::game;

// The ids below are aion2.app's dungeon tier ids, as maps.tsv lists them. A capture's
// dungeon came as 600072, the tier it was: 21 36 carries the same ids (AGENTS.md).

TEST_CASE(MapNames, EachTierHasItsOwnRow) {
    TEST_ASSERT_EQ(std::string(map_name(600002)), std::string("Krao Cave (Exploration)"));
    TEST_ASSERT_EQ(std::string(map_name(600001)), std::string("Krao Cave (Conquest)"));
    TEST_ASSERT_EQ(std::string(map_name(610052)), std::string("Deus Research Base (Stage 4)"));
    TEST_ASSERT_EQ(map_label(620021), std::string("Chalice of Muspel (Conquest [Hard])"));
}

TEST_CASE(MapNames, HardTiersHaveTheirOwnIds) {
    // Krao Cave's skips 600003: tier ids follow no pattern, so none is guessed.
    TEST_ASSERT_EQ(std::string(map_name(600004)), std::string("Krao Cave (Conquest [Hard])"));
    TEST_ASSERT(map_name(600003).empty());
    TEST_ASSERT_EQ(std::string(map_name(600073)), std::string("Vakron Sky Island (Conquest [Hard])"));
    // The raid's page gained a Hard tier, so its normal one now says which it is.
    TEST_ASSERT_EQ(std::string(map_name(600082)), std::string("Abyssal Forge: Ludra (Conquest)"));
    TEST_ASSERT_EQ(std::string(map_name(600083)), std::string("Abyssal Forge: Ludra (Conquest [Hard])"));
    TEST_ASSERT(is_dungeon(600083));
}

TEST_CASE(MapNames, AOneTierPageKeepsItsOwnName) {
    // The Awakening pages name their tier already: no "(Challenge) (Extreme)".
    TEST_ASSERT_EQ(std::string(map_name(910064)), std::string("Chamber of the Dead (Challenge)"));
}

TEST_CASE(MapNames, ADungeonGoesByItsIdNotTheTable) {
    TEST_ASSERT(is_dungeon(600001));
    TEST_ASSERT(is_dungeon(910064));
    // A tier a patch added, which the table cannot name yet: a dungeon all the same.
    TEST_ASSERT(map_name(600003).empty());
    TEST_ASSERT(is_dungeon(600003));
    TEST_ASSERT(map_name(910099).empty());
    TEST_ASSERT(is_dungeon(910099));
    TEST_ASSERT(is_dungeon(kFirstInstanceMapId));
    // The open world (the second capture's town was 1010), and no map load seen yet.
    TEST_ASSERT_FALSE(is_dungeon(kFirstInstanceMapId - 1));
    TEST_ASSERT_FALSE(is_dungeon(400001));
    TEST_ASSERT_FALSE(is_dungeon(1010));
    TEST_ASSERT_FALSE(is_dungeon(0));
}

TEST_CASE(MapNames, AnUnnamedDungeonSaysItIsOne) {
    TEST_ASSERT_EQ(map_label(600003), std::string("Dungeon 600003"));
    TEST_ASSERT_EQ(map_label(910099), std::string("Dungeon 910099"));
    TEST_ASSERT_EQ(map_label(1010), std::string("Map 1010"));
}

TEST_CASE(MapNames, UnlistedIdsHaveALabel) {
    // Open-world zones have no public ids, so none is listed.
    TEST_ASSERT(map_name(400001).empty());
    TEST_ASSERT_EQ(map_label(400001), std::string("Map 400001"));
    TEST_ASSERT_EQ(map_label(0), std::string("Unknown map"));
    TEST_ASSERT(map_name(0).empty());
}
