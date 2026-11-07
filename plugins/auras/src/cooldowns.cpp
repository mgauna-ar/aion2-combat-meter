#include "auras/cooldowns.hpp"

#include "game/skill_names.hpp"

#include <algorithm>
#include <iterator>

namespace aion2::auras {

namespace {

const SkillCooldown kCooldowns[] = {
#include "cooldown_table.inc"
};

/// The sum of `per` over the specialties a cast code puts in play: an active skill's
/// picks (the variant's thousands, hundreds and tens digits), or a stigma's first n
/// (its tens digit).
uint32_t in_play(const SkillCooldown& row, uint32_t code, const std::array<uint32_t, 5>& per) noexcept {
    const uint32_t variant = code % 10'000;
    uint32_t sum = 0;
    if (row.stigma) {
        const uint32_t unlocked = std::min<uint32_t>(variant / 10 % 10, static_cast<uint32_t>(per.size()));
        for (uint32_t i = 0; i < unlocked; ++i) sum += per[i];
    } else {
        for (const uint32_t pick : {variant / 1000, variant / 100 % 10, variant / 10 % 10}) {
            if (pick >= 1 && pick <= per.size()) sum += per[pick - 1];
        }
    }
    return sum;
}

} // namespace

const SkillCooldown* find_cooldown(uint32_t code) noexcept {
    const uint32_t base = game::skill_base(code);
    const auto* it = std::lower_bound(std::begin(kCooldowns), std::end(kCooldowns), base,
                                      [](const SkillCooldown& row, uint32_t c) { return row.code < c; });
    return it != std::end(kCooldowns) && it->code == base ? it : nullptr;
}

uint32_t listed_cooldown_ms(uint32_t code) noexcept {
    const SkillCooldown* row = find_cooldown(code);
    if (row == nullptr) return 0;
    const uint32_t cut = in_play(*row, code, row->cut_ms);
    return cut >= row->cooldown_ms ? 0 : row->cooldown_ms - cut;
}

uint32_t skill_charges(uint32_t code) noexcept {
    const SkillCooldown* row = find_cooldown(code);
    return row == nullptr ? 1 : 1 + std::min<uint32_t>(in_play(*row, code, row->extra_uses), 8);
}

void CooldownTracker::on_used(uint32_t code, Micros ts) {
    const uint32_t listed = listed_cooldown_ms(code);
    if (listed == 0) return;
    const uint32_t charges = skill_charges(code);
    State& s = m_skills[game::skill_base(code)];
    if (s.listed_ms != listed || s.ready_at.size() != charges) {
        // Other specialties picked: what was learned was for the old figure.
        s.listed_ms = listed;
        s.learned_ms = 0;
        s.ready_at.assign(charges, 0);
    } else if (charges == 1 && s.used_at != 0 && !s.set_since_use && ts > s.used_at) {
        const uint64_t gap = (ts - s.used_at) / kMicrosPerMilli;
        const uint64_t current = s.learned_ms != 0 ? s.learned_ms : listed;
        if (gap < current && static_cast<double>(gap) >= kMinLearnShare * listed) s.learned_ms = gap;
    }
    const uint64_t estimate = s.learned_ms != 0 ? s.learned_ms : listed;
    s.used_at = ts;
    *std::min_element(s.ready_at.begin(), s.ready_at.end()) = ts + estimate * kMicrosPerMilli;
    s.duration_ms = estimate;
    s.exact = false;
    s.set_since_use = false;
}

void CooldownTracker::on_set(uint32_t skill, uint32_t remaining_ms, Micros ts) {
    State& s = m_skills[game::skill_base(skill)];
    if (s.ready_at.empty()) s.ready_at.assign(1, 0);
    // The use that shows: the one ready soonest.
    *std::min_element(s.ready_at.begin(), s.ready_at.end()) = ts + uint64_t{remaining_ms} * kMicrosPerMilli;
    s.exact = true;
    s.set_since_use = true;
    // Never seen used (the plugin started mid-cooldown): the bar starts full.
    if (s.duration_ms < remaining_ms) s.duration_ms = remaining_ms;
}

std::vector<CooldownEntry> CooldownTracker::snapshot(Micros) const {
    std::vector<CooldownEntry> out;
    for (const auto& [skill, s] : m_skills) {
        if (s.ready_at.empty()) continue;
        const Micros ready = *std::min_element(s.ready_at.begin(), s.ready_at.end());
        out.push_back(CooldownEntry{skill, game::skill_label(skill), ready, s.duration_ms, s.exact});
    }
    const auto soonest = [](const CooldownEntry& a, const CooldownEntry& b) {
        return a.ready_at != b.ready_at ? a.ready_at < b.ready_at : a.skill < b.skill;
    };
    // Past the cap, the skills ready the longest go: they are the least use to show.
    if (out.size() > kMaxListed) {
        std::nth_element(out.begin(), out.begin() + kMaxListed, out.end(),
                         [&](const CooldownEntry& a, const CooldownEntry& b) { return soonest(b, a); });
        out.resize(kMaxListed);
    }
    std::sort(out.begin(), out.end(), soonest);
    return out;
}

uint64_t CooldownTracker::estimate_ms(uint32_t code) const {
    const uint32_t listed = listed_cooldown_ms(code);
    const auto it = m_skills.find(game::skill_base(code));
    if (it == m_skills.end() || it->second.listed_ms != listed || it->second.learned_ms == 0) return listed;
    return it->second.learned_ms;
}

} // namespace aion2::auras
