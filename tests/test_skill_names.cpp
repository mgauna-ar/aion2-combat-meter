#include "test_framework.hpp"

#include "meter/skill_names.hpp"

using namespace aion2;
using namespace aion2::meter;

// The codes below are from the first real capture: a Templar and an Elementalist
// with a spirit.

TEST_CASE(SkillNames, VariantsTakeTheirSkillsName) {
    TEST_ASSERT_EQ(skill_base(12240347), uint32_t{12240000});
    TEST_ASSERT_EQ(std::string(skill_name(12240347)), std::string("Judgment"));
    TEST_ASSERT_EQ(std::string(skill_name(12040230)), std::string("Pummel"));
    // A heal-over-time tick carries the skill that put it up.
    TEST_ASSERT_EQ(std::string(skill_name(12350030)), std::string("Warding Strike"));
    TEST_ASSERT_EQ(std::string(skill_name(16330000)), std::string("Dimensional Control"));
}

TEST_CASE(SkillNames, AVariantWithItsOwnRowKeepsIt) {
    // 16290000 is Dismiss: Fire Spirit; 16290002, its own row, is the earth spirit.
    TEST_ASSERT_EQ(std::string(skill_name(16290002)), std::string("Dismiss: Earth Spirit"));
    TEST_ASSERT_EQ(std::string(skill_name(16290000)), std::string("Dismiss: Fire Spirit"));
}

TEST_CASE(SkillNames, UnlistedCodesHaveALabel) {
    // A spirit's own attacks are not in the skill data.
    TEST_ASSERT(skill_name(16990004).empty());
    TEST_ASSERT_EQ(skill_label(16990004), std::string("Skill 16990004"));
    TEST_ASSERT_EQ(skill_label(100032), std::string("Skill 100032"));
    TEST_ASSERT_EQ(skill_label(12060230), std::string("Punishing Strike"));
}

TEST_CASE(SkillNames, ClassComesFromTheFirstTwoDigits) {
    TEST_ASSERT(class_of_skill(11020000) == AionClass::Gladiator);
    TEST_ASSERT(class_of_skill(12240347) == AionClass::Templar);
    TEST_ASSERT(class_of_skill(13010000) == AionClass::Assassin);
    TEST_ASSERT(class_of_skill(14010000) == AionClass::Ranger);
    TEST_ASSERT(class_of_skill(15280000) == AionClass::Sorcerer);
    TEST_ASSERT(class_of_skill(16990004) == AionClass::Spiritmaster);
    TEST_ASSERT(class_of_skill(17080000) == AionClass::Cleric);
    TEST_ASSERT(class_of_skill(18710000) == AionClass::Chanter);
    TEST_ASSERT(class_of_skill(19090000) == AionClass::Brawler);
    // Shared skills (items, flight, system) and short codes have no class.
    TEST_ASSERT(class_of_skill(100032) == AionClass::Unknown);
    TEST_ASSERT(class_of_skill(4900202) == AionClass::Unknown);
    TEST_ASSERT(class_of_skill(10000000) == AionClass::Unknown);
    TEST_ASSERT(class_of_skill(20000000) == AionClass::Unknown);
    TEST_ASSERT_EQ(skill_base(4900202), uint32_t{4900202});
}

TEST_CASE(SkillNames, ClassNamesReadBothSpellings) {
    TEST_ASSERT_EQ(std::string(class_name(AionClass::Spiritmaster)), std::string("Elementalist"));
    TEST_ASSERT(class_from_name("Elementalist") == AionClass::Spiritmaster);
    TEST_ASSERT(class_from_name("Spiritmaster") == AionClass::Spiritmaster);
    TEST_ASSERT(class_from_name("Brawler") == AionClass::Brawler);
}
