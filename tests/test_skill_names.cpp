#include "test_framework.hpp"

#include "game/skill_names.hpp"

using namespace aion2;
using namespace aion2::game;

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

TEST_CASE(SkillNames, DodgeIsNotItsClassWeapon) {
    // A class's common block holds its weapon (XX0000xx) and Dodge (XX0001xx): a
    // Dodge variant from the dungeon capture once read as the Sorcerer's weapon.
    TEST_ASSERT_EQ(skill_base(12000101), uint32_t{12000100});
    TEST_ASSERT_EQ(skill_key(15000101), uint32_t{15000100});
    TEST_ASSERT_EQ(std::string(skill_name(15000101)), std::string("Dodge"));
    TEST_ASSERT_EQ(std::string(skill_name(15000003)), std::string("Equip Sorcerer Weapon"));
    TEST_ASSERT_EQ(skill_base(16009999), uint32_t{16009900});
}

TEST_CASE(SkillNames, AVariantWithItsOwnRowKeepsIt) {
    // 16290000 is Dismiss: Fire Spirit; 16290002, its own row, is the earth spirit.
    TEST_ASSERT_EQ(std::string(skill_name(16290002)), std::string("Dismiss: Earth Spirit"));
    TEST_ASSERT_EQ(std::string(skill_name(16290000)), std::string("Dismiss: Fire Spirit"));
}

TEST_CASE(SkillNames, BreakdownsFileVariantsUnderTheirSkill) {
    TEST_ASSERT_EQ(skill_key(12240347), uint32_t{12240000});
    TEST_ASSERT_EQ(skill_key(16290002), uint32_t{16290002});
    TEST_ASSERT_EQ(skill_key(16990004), uint32_t{16990000});
    TEST_ASSERT_EQ(skill_key(100032), uint32_t{100032});
}

TEST_CASE(SkillNames, UnlistedCodesHaveALabel) {
    // A spirit's own attacks are not in the skill data.
    TEST_ASSERT(skill_name(16990004).empty());
    TEST_ASSERT_EQ(skill_label(16990004), std::string("Skill 16990004"));
    TEST_ASSERT_EQ(skill_label(100032), std::string("Skill 100032"));
    TEST_ASSERT_EQ(skill_label(12060230), std::string("Punishing Strike"));
}

TEST_CASE(SkillNames, ADamageTheostonesHitIsNamedAfterIt) {
    // 3000TNN: the tier, then the item's number plus 6 (Ulgorn's Will is 513850013).
    TEST_ASSERT_EQ(skill_label(3'000'019), std::string("Theostone: Ulgorn's Will"));
    TEST_ASSERT_EQ(skill_label(3'000'117), std::string("Theostone: Traufnir's Courage"));
    TEST_ASSERT_EQ(skill_label(3'000'224), std::string("Theostone: Beritra's Selfishness"));
    // Gear, not a class's skill: its wearer's class comes from their own.
    TEST_ASSERT(class_of_skill(3'000'019) == AionClass::Unknown);
    TEST_ASSERT_EQ(skill_key(3'000'019), uint32_t{3'000'019});
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
    // aion2.app lists the Brawler's skills unnamed: these are questlog.gg's.
    TEST_ASSERT_EQ(std::string(skill_name(19010000)), std::string("Flurry"));
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

TEST_CASE(SkillNames, EffectsTakeTheirSkillsName) {
    // The applying skill first, then the skill the effect's id points to (id / 10).
    TEST_ASSERT_EQ(std::string(effect_name(181'900'211, 18'190'021)), std::string("Undefeated Mantra"));
    TEST_ASSERT_EQ(std::string(effect_name(181'900'211)), std::string("Undefeated Mantra"));
    TEST_ASSERT_EQ(std::string(effect_name(111'000'099, 11'190'137)), std::string("Leaping Slam"));
    // The effect's own name first of all: the one Leaping Slam puts on.
    TEST_ASSERT_EQ(std::string(effect_name(111'000'071, 11'190'137)), std::string("Prepare for Battle"));
    // An item's effect has its name, and says it is an item's.
    TEST_ASSERT_EQ(std::string(effect_name(20'111'011, 2'011'101)), std::string("Life Potion"));
    TEST_ASSERT(effect_kind(20'111'011) == EffectKind::Item);
    TEST_ASSERT_EQ(std::string(effect_name(22'101'031)), std::string("Speed Scroll"));
    // A boss's and a system mark: no name, and a label with the id.
    TEST_ASSERT(effect_name(16'044'202, 1'604'420).empty());
    TEST_ASSERT(effect_name(200).empty());
    TEST_ASSERT_EQ(effect_label(200), std::string("Effect 200"));
}

TEST_CASE(SkillNames, AKeyIsNamedAsEffectKeyMadeIt) {
    // A watch list's key, with no effect of it at hand: a skill's when the skill table
    // lists that very code, else an effect id.
    TEST_ASSERT(is_skill_key(effect_key(181'900'211, 18'190'021)));
    TEST_ASSERT_EQ(effect_key_label(effect_key(181'900'211, 18'190'021)), std::string("Undefeated Mantra"));
    TEST_ASSERT_EQ(effect_key_label(effect_key(22'101'031)), std::string("Speed Scroll"));
    // By its ten thousands the monsters' Burn reads as Dodge: no skill's key.
    TEST_ASSERT_EQ(effect_key(12'000'121), uint32_t{12'000'121});
    TEST_ASSERT(!is_skill_key(12'000'121));
    TEST_ASSERT_EQ(effect_key_label(12'000'121), std::string("Burn"));
    TEST_ASSERT_EQ(effect_key_label(200), std::string("Effect 200"));
}

TEST_CASE(SkillNames, AnEffectsOwnNameComesBeforeItsSkills) {
    // src/game/data/effects.tsv: what Punishment puts on is Executor, as the game
    // shows it.
    TEST_ASSERT_EQ(std::string(effect_name(120'900'001, 12'090'233)), std::string("Executor"));
    TEST_ASSERT_EQ(effect_label(120'900'001), std::string("Executor"));
    TEST_ASSERT(effect_kind(120'900'001) == EffectKind::Skill);
    // Still one effect to a watch list, whatever it is called.
    TEST_ASSERT_EQ(effect_key(120'900'001, 12'090'233), uint32_t{12'090'000});

    // Not listed: a skill's by its skill, no one's otherwise.
    TEST_ASSERT(effect_kind(181'900'211, 18'190'021) == EffectKind::Skill);
    TEST_ASSERT(effect_kind(200) == EffectKind::Unknown);
}

TEST_CASE(SkillNames, ItemEffectsSortByTheirIdsFirstDigits) {
    TEST_ASSERT(item_effect(22'101'031) == ItemEffect::Scroll);    // Speed Scroll
    TEST_ASSERT(item_effect(22'104'021) == ItemEffect::Scroll);    // Benediction Scroll
    TEST_ASSERT(item_effect(21'604'011) == ItemEffect::Food);      // Fruit Salad
    TEST_ASSERT(item_effect(21'101'011) == ItemEffect::Food);      // Mela Juice
    TEST_ASSERT(item_effect(27'003'131) == ItemEffect::Food);      // Artisan's Roasted Fogus
    TEST_ASSERT(item_effect(20'111'011) == ItemEffect::Potion);    // Life Potion
    // A Theostone is gear, not a consumable.
    TEST_ASSERT(item_effect(30'010'141) == ItemEffect::None);      // Notun's Craft
    // Items the view leaves out, and what is no item's.
    TEST_ASSERT(item_effect(29'001'011) == ItemEffect::None);      // EXP Boost Talisman
    TEST_ASSERT(item_effect(42'000'101) == ItemEffect::None);      // a transformation candy
    TEST_ASSERT(item_effect(181'900'411) == ItemEffect::None);     // Undefeated Mantra
    TEST_ASSERT(item_effect(12'000'161) == ItemEffect::None);      // the bosses' Burn
    TEST_ASSERT(item_effect(22'999'999) == ItemEffect::None);      // not listed
}
