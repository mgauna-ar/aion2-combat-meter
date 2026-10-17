#include "meter/skill_names.hpp"

#include <algorithm>
#include <iterator>

namespace aion2::meter {

namespace {

struct SkillRow {
    uint32_t code;
    const char* name;
};

constexpr SkillRow kSkills[] = {
#include "skill_table.inc"
};

constexpr uint32_t kClassCodeMin = 10'000'000;
constexpr uint32_t kClassCodeMax = 19'999'999;

const char* find(uint32_t code) noexcept {
    const auto* it = std::lower_bound(std::begin(kSkills), std::end(kSkills), code,
                                      [](const SkillRow& row, uint32_t c) { return row.code < c; });
    return it != std::end(kSkills) && it->code == code ? it->name : nullptr;
}

} // namespace

uint32_t skill_base(uint32_t code) noexcept {
    if (code < kClassCodeMin || code > kClassCodeMax) return code;
    return code - code % 10'000;
}

std::string_view skill_name(uint32_t code) noexcept {
    if (const char* name = find(code)) return name;
    if (const char* name = find(skill_base(code))) return name;
    return {};
}

AionClass class_of_skill(uint32_t code) noexcept {
    if (code < kClassCodeMin || code > kClassCodeMax) return AionClass::Unknown;
    switch (code / 1'000'000) {
        case 11: return AionClass::Gladiator;
        case 12: return AionClass::Templar;
        case 13: return AionClass::Assassin;
        case 14: return AionClass::Ranger;
        case 15: return AionClass::Sorcerer;
        case 16: return AionClass::Spiritmaster;
        case 17: return AionClass::Cleric;
        case 18: return AionClass::Chanter;
        case 19: return AionClass::Brawler;
        default: return AionClass::Unknown;
    }
}

std::string skill_label(uint32_t code) {
    const std::string_view name = skill_name(code);
    if (!name.empty()) return std::string(name);
    return "Skill " + std::to_string(code);
}

} // namespace aion2::meter
