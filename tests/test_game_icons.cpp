#include "test_framework.hpp"

#include "app/game_icons.hpp"
#include "game/skill_names.hpp"

#include <cstdint>
#include <vector>

using namespace aion2;
using namespace aion2::app;

TEST_CASE(GameIcons, EachClassOnGlobalHasItsEmblem) {
    std::vector<int> seen;
    for (const auto cls : {game::AionClass::Gladiator, game::AionClass::Templar, game::AionClass::Assassin,
                           game::AionClass::Ranger, game::AionClass::Sorcerer, game::AionClass::Spiritmaster,
                           game::AionClass::Cleric, game::AionClass::Chanter}) {
        const int slot = class_icon(cls);
        TEST_ASSERT(slot != kNoIcon);
        for (const int other : seen) TEST_ASSERT(slot != other);
        seen.push_back(slot);
    }
    // The Brawler is not on Global, and its emblem is not in the party style.
    TEST_ASSERT_EQ(class_icon(game::AionClass::Brawler), kNoIcon);
    TEST_ASSERT_EQ(class_icon(game::AionClass::Unknown), kNoIcon);
}

TEST_CASE(GameIcons, ASkillsIconFollowsTheRowThatNamesIt) {
    const int judgment = skill_icon(12240000);
    TEST_ASSERT(judgment != kNoIcon);
    // A variant with no row of its own shows its skill's icon.
    TEST_ASSERT_EQ(skill_icon(12240347), judgment);
    // A variant with its own row keeps that row's: Use Spirit Summon Skill has no
    // icon, and does not borrow Summon: Fire Spirit's (16100000).
    TEST_ASSERT(skill_icon(16100000) != kNoIcon);
    TEST_ASSERT_EQ(skill_icon(16107000), kNoIcon);
    // Two skills can share one picture.
    TEST_ASSERT_EQ(skill_icon(16290002), skill_icon(16290000));
}

TEST_CASE(GameIcons, CodesWithNoIconGetNone) {
    // A spirit's attack, a monster's skill, a system code.
    TEST_ASSERT_EQ(skill_icon(16990004), kNoIcon);
    TEST_ASSERT_EQ(skill_icon(2920360), kNoIcon);
    TEST_ASSERT_EQ(skill_icon(100032), kNoIcon);
    TEST_ASSERT_EQ(skill_icon(0), kNoIcon);
}

TEST_CASE(GameIcons, AnEffectShowsTheIconOfTheSkillThatNamesIt) {
    // Undefeated Mantra's effect id is its code times ten plus a digit.
    TEST_ASSERT_EQ(game::effect_skill(181900411), uint32_t{18190041});
    TEST_ASSERT_EQ(effect_icon(181900411), skill_icon(18190000));
    TEST_ASSERT(effect_icon(181900411) != kNoIcon);
    // The applying skill named by the record wins over the id.
    TEST_ASSERT_EQ(effect_icon(181900411, 12350130), skill_icon(12350000));
    // A monster's effect.
    TEST_ASSERT_EQ(effect_icon(29203601), kNoIcon);
}

TEST_CASE(GameIcons, TheSheetDecodes) {
    const int px = icon_sheet_px();
    TEST_ASSERT_EQ(px, 40);
    TEST_ASSERT(icon_slot_count() > 300);
    const size_t slot_bytes = static_cast<size_t>(px) * static_cast<size_t>(px) * 4;
    for (int slot = 0; slot < icon_slot_count(); ++slot) {
        TEST_ASSERT_EQ(icon_pixels(slot).size(), slot_bytes);
    }
    TEST_ASSERT(icon_pixels(kNoIcon).empty());
    TEST_ASSERT(icon_pixels(icon_slot_count()).empty());
    // An emblem is drawn on transparency: some pixels are, most of its middle not.
    const auto emblem = icon_pixels(class_icon(game::AionClass::Assassin));
    size_t opaque = 0;
    size_t clear = 0;
    for (size_t i = 3; i < emblem.size(); i += 4) {
        if (emblem[i] > 200) ++opaque;
        if (emblem[i] == 0) ++clear;
    }
    TEST_ASSERT(opaque > slot_bytes / 4 / 4);
    TEST_ASSERT(clear > 0);
}

TEST_CASE(GameIcons, ResamplingAveragesByArea) {
    // A 2x2 icon: red, green, blue and a transparent pixel, halved to one pixel. The
    // transparent one lends no colour, only its share of the alpha.
    const std::vector<uint8_t> src{
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 9, 9, 9, 0,
    };
    uint8_t out[4]{};
    resample_icon(src, 2, out, 1, 4);
    TEST_ASSERT_EQ(int{out[0]}, 85);
    TEST_ASSERT_EQ(int{out[1]}, 85);
    TEST_ASSERT_EQ(int{out[2]}, 85);
    TEST_ASSERT_EQ(int{out[3]}, 191);

    // The same size copies, rows written at the stride asked for.
    uint8_t copy[2 * 12]{};
    resample_icon(src, 2, copy, 2, 12);
    TEST_ASSERT_EQ(int{copy[0]}, 255);
    TEST_ASSERT_EQ(int{copy[4 + 1]}, 255);
    TEST_ASSERT_EQ(int{copy[12 + 2]}, 255);
    TEST_ASSERT_EQ(int{copy[12 + 7]}, 0);
    TEST_ASSERT_EQ(int{copy[8]}, 0);
}
