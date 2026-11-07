// The Combat Meter page's Casts tab: what each player pressed, and how often.

#include "app/ui/meter_tabs.hpp"

#include "app/display_format.hpp"
#include "app/ui/icons.hpp"
#include "app/ui/table.hpp"
#include "app/ui/theme.hpp"
#include "app/ui/widgets.hpp"
#include "common/time_format.hpp"

#ifdef HAVE_IMGUI
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>
#endif

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

namespace {

const char* const kTipCasts =
    "Skills the player pressed: their own, not their summons', and no passive going off or mantra pulse.";
const char* const kTipCpm = "Casts a minute, over the fight's duration: its first hit to its last.";

std::string per_minute(uint64_t casts, double duration_s) {
    if (duration_s <= 0.0) return "-";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", 60.0 * static_cast<double>(casts) / duration_s);
    return buf;
}

/// The direct hits the player landed with a skill, from their damage breakdown.
uint64_t hits_of(const meter::CombatantTotals& c, uint32_t code) {
    for (const auto& s : c.skills) {
        if (s.code == code) return s.hits.hits;
    }
    return 0;
}

void casts_breakdown(const meter::FightSummary& f, const meter::CombatantTotals& c, meter::EntityId& selected) {
    if (!begin_breakdown("##CastsBreakdown", "CASTS", c, selected)) return;
    if (c.cast_skills.empty()) {
        empty_state(ICON_BOLT, "No casts recorded", "This player has not pressed a skill in this fight.");
        end_card();
        return;
    }
    const Column cols[] = {
        {"Skill", 0.0f, nullptr, 0},
        {"Casts", 56.0f, kTipCasts, 1},
        {"Share", 52.0f, "Of the player's casts.", 2},
        {"CPM", 50.0f, kTipCpm, 3},
        {"Avg gap", 64.0f, "The average time between two presses of the skill, from its first to its last.", 4},
        {"HIT", 44.0f, "Direct hits landed with the skill.", 5},
        {"HIT/cast", 64.0f, "Direct hits a press: above 1 for a skill that strikes several times or several targets.",
         6},
    };
    if (begin_table("##CastSkillTable", cols, static_cast<int>(std::size(cols)), fill_h(0.0f))) {
        const uint64_t top = std::max<uint64_t>(c.cast_skills.front().casts, 1);
        for (const auto& s : c.cast_skills) {
            ImGui::TableNextRow();
            row_progress_bar(static_cast<float>(static_cast<double>(s.casts) / static_cast<double>(top)),
                             row_color_u32(c));
            skill_cell(0, s.code);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Skill code %u", s.code);
            cell(1, colors::TextPrimary, number(s.casts));
            cell(2, colors::TextMuted,
                 c.casts == 0 ? std::string("-")
                              : format_percent(static_cast<double>(s.casts) / static_cast<double>(c.casts)));
            cell(3, colors::TextBody, per_minute(s.casts, f.duration_s));
            cell(4, colors::TextMuted, s.casts < 2 ? std::string("-") : format_seconds(s.mean_gap_s()));
            if (s.casts >= 2 && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("First at %s, last at %s", common::clock_duration(std::max(s.first_s, 0.0)).c_str(),
                                  common::clock_duration(std::max(s.last_s, 0.0)).c_str());
            }
            const uint64_t hits = hits_of(c, s.code);
            cell(5, colors::TextMuted, hits == 0 ? std::string("-") : number(hits));
            char ratio[32];
            std::snprintf(ratio, sizeof(ratio), "%.2f", static_cast<double>(hits) / static_cast<double>(s.casts));
            cell(6, colors::TextDim, hits == 0 || s.casts == 0 ? std::string("-") : std::string(ratio));
        }
        ImGui::EndTable();
    }
    end_card();
}

} // namespace

void casts_tab(const meter::FightSummary& f, AppViewState& state) {
    if (!f.has_casts) {
        empty_state(ICON_BOLT, "Not counted for this fight", "It was saved before the meter counted casts.");
        return;
    }
    std::vector<const meter::CombatantTotals*> rows;
    for (const auto& c : f.combatants) {
        if (c.casts > 0) rows.push_back(&c);
    }
    // Most casts first; the damage ranking breaks a tie.
    std::stable_sort(rows.begin(), rows.end(), [](const auto* a, const auto* b) { return a->casts > b->casts; });
    if (rows.empty()) {
        empty_state(ICON_BOLT, "No casts yet", "Each skill a party member presses shows here.");
        return;
    }
    const meter::CombatantTotals* open = find_row(f, state.casts_player);
    const Column cols[] = {
        {"#", 24.0f, nullptr, 7},
        {"Class", 64.0f, "From the skills the player used.", 5},
        {"Player", 0.0f, "Select a player for their skills.", 0},
        {"CPM", 50.0f, kTipCpm, 1},
        {"Casts", 56.0f, kTipCasts, 2},
        {"Skills", 52.0f, "Different skills pressed.", 3},
        {"Active", 54.0f,
         "Time spent hitting: the gaps of 3 s or less between the player's own direct hits, of the fight.", 4},
    };
    if (begin_table("##CastsTable", cols, static_cast<int>(std::size(cols)), table_height(open != nullptr))) {
        const double top = std::max(rows.front()->casts_per_minute(f.duration_s), 1.0);
        int rank = 0;
        for (const auto* c : rows) {
            ImGui::TableNextRow();
            row_progress_bar(static_cast<float>(c->casts_per_minute(f.duration_s) / top), row_color_u32(*c));
            cell(0, colors::TextMuted, std::to_string(++rank));
            if (ImGui::TableSetColumnIndex(1)) class_badge(c->cls);
            name_cell(2, *c, state.casts_player, "C");
            if (ImGui::TableSetColumnIndex(3)) {
                ImGui::PushFont(bold_font());
                text_colored_u32(row_color_u32(*c), "%s", per_minute(c->casts, f.duration_s).c_str());
                ImGui::PopFont();
            }
            cell(4, colors::TextBody, number(c->casts));
            cell(5, colors::TextMuted, number(c->cast_skills.size()));
            cell(6, colors::TextMuted, f.detail != nullptr ? format_percent(c->active_share(f.duration_s)) : "-");
        }
        ImGui::EndTable();
    }
    if (open != nullptr) casts_breakdown(f, *open, state.casts_player);
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
