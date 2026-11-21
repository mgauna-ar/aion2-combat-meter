#include "test_framework.hpp"

#include "game/map_names.hpp"

using namespace aion2;
using namespace aion2::game;

// The ids below are map ids, as 21 36 carries them and maps.tsv lists them: a
// capture's dungeon came as 600072, the tier it was (AGENTS.md, Map load).

TEST_CASE(MapNames, EachTierHasItsOwnRow) {
    TEST_ASSERT_EQ(std::string(map_name(600002)), std::string("Krao Cave (Exploration)"));
    TEST_ASSERT_EQ(std::string(map_name(600001)), std::string("Krao Cave (Conquest)"));
    TEST_ASSERT_EQ(std::string(map_name(610052)), std::string("Deus Research Base (Stage 4)"));
    TEST_ASSERT_EQ(map_label(620021), std::string("Chalice of Muspel (Conquest [Hard])"));
    // A tier aion2.app's pages leave out, named from questlog.gg's record.
    TEST_ASSERT_EQ(std::string(map_name(600023)), std::string("Fire Temple (Conquest [Hard])"));
}

TEST_CASE(MapNames, ARowIsTheMapsIdNotTheInstances) {
    // The captures' arena and PvP zone came as these, their instances' ids being 8 and 21.
    TEST_ASSERT_EQ(std::string(map_name(61)), std::string("Impetusium Arena"));
    TEST_ASSERT_EQ(std::string(map_name(20)), std::string("Chaotic Lower Reshanta"));
    // aion2.app's tier ids where they differ: the instance's, which no map load carries.
    TEST_ASSERT_EQ(std::string(map_name(600145)), std::string("Citadel of the Fallen Daeva (Conquest)"));
    TEST_ASSERT(map_name(600152).empty());
    TEST_ASSERT_EQ(std::string(map_name(156011)), std::string("Beritra Brigade Fortress (Conquest)"));
    TEST_ASSERT(map_name(690012).empty());
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
    TEST_ASSERT_FALSE(is_dungeon(1010));
    TEST_ASSERT_FALSE(is_dungeon(0));
}

TEST_CASE(MapNames, AnInstanceBelowTheDungeonsIdsIsOneByTheTable) {
    TEST_ASSERT(is_dungeon(61));      // Impetusium Arena, the arena capture's
    TEST_ASSERT(is_dungeon(156011));  // Beritra Brigade Fortress (Conquest)
    TEST_ASSERT(is_dungeon(200002));  // Ascension's Dream of Torment
    TEST_ASSERT(is_dungeon(400001));  // a seal dungeon
    TEST_ASSERT(is_dungeon(901));     // a Legion airship
    // Chaotic Reshanta is open PvP, and an event comes in the open world.
    TEST_ASSERT_FALSE(is_dungeon(20));
    TEST_ASSERT_FALSE(is_dungeon(111002)); // Incursion of the Spirits
}

TEST_CASE(MapNames, OnlyTheDungeonsIdsKeepStrangersOut) {
    // Only the group that entered: everyone in view is on the local player's side.
    TEST_ASSERT(is_party_instance(600072));
    TEST_ASSERT(is_party_instance(600003)); // a tier not yet named
    // An arena's other side is inside too, and the open world is anyone's.
    TEST_ASSERT_FALSE(is_party_instance(61));
    TEST_ASSERT_FALSE(is_party_instance(156011));
    TEST_ASSERT_FALSE(is_party_instance(20));
    TEST_ASSERT_FALSE(is_party_instance(1010));
    TEST_ASSERT_FALSE(is_party_instance(0));
}

TEST_CASE(MapNames, AnUnnamedDungeonSaysItIsOne) {
    TEST_ASSERT_EQ(map_label(600003), std::string("Dungeon 600003"));
    TEST_ASSERT_EQ(map_label(910099), std::string("Dungeon 910099"));
    TEST_ASSERT_EQ(map_label(1010), std::string("Map 1010"));
}

TEST_CASE(MapNames, UnlistedIdsHaveALabel) {
    // Open-world zones have no public ids, so none is listed: the second capture's town.
    TEST_ASSERT(map_name(1010).empty());
    TEST_ASSERT_EQ(map_label(1010), std::string("Map 1010"));
    // An instance below the dungeons' ids is named all the same.
    TEST_ASSERT_EQ(map_label(400001), std::string("Ruins of the Ancient City of Ru"));
    TEST_ASSERT_EQ(map_label(0), std::string("Unknown map"));
    TEST_ASSERT(map_name(0).empty());
}
