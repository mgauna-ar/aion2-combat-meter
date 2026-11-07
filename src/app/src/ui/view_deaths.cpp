#include "app/ui/meter_tabs.hpp"

#include "app/display_format.hpp"
#include "app/ui/icons.hpp"
#include "app/ui/table.hpp"
#include "app/ui/theme.hpp"
#include "app/ui/widgets.hpp"
#include "common/time_format.hpp"
#include "game/npc_names.hpp"
#include "meter/fight_detail.hpp"

#ifdef HAVE_IMGUI
#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#endif

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

namespace {

bool is_selected(const AppViewState& state, const meter::DeathRecord& death) {
    return state.death_time_s >= 0.0 && death.player == state.death_player &&
           std::abs(death.time_s - state.death_time_s) < 1e-6;
}

std::string source_of(const meter::RecapEvent& e) {
    if (!e.damage()) return e.healer.empty() ? std::string("-") : e.healer;
    return e.source_npc != 0 ? game::npc_label(e.source_npc) : std::string("-");
}

const char* kind_label(meter::RecapKind kind) {
    switch (kind) {
        case meter::RecapKind::Hit: return "Hit";
        case meter::RecapKind::Dot: return "DoT";
        case meter::RecapKind::Heal: return "Heal";
        case meter::RecapKind::HealTick: return "HoT";
    }
    return "?";
}

void render_log(const meter::FightDetail& detail, AppViewState& state, float height) {
    const Column cols[] = {
        {"Time", 52.0f, "Into the fight.", 0},
        {"Player", 0.0f, "Select a death for the last hits and heals before it.", 1},
        {"Killing blow", 0.0f, "The last damage that landed on them.", 2},
        {"Source", 140.0f, nullptr, 4},
        {"Amount", 72.0f, nullptr, 3},
        {"Up after", 72.0f, "Until their HP came back. \"-\" while they were still down when the fight ended.", 5},
    };
    if (!begin_table("##DeathLog", cols, static_cast<int>(std::size(cols)), height)) return;
    for (size_t i = 0; i < detail.deaths.size(); ++i) {
        const meter::DeathRecord& death = detail.deaths[i];
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const std::string label = common::clock_duration(death.time_s) + "##Death" + std::to_string(i);
        const bool selected = is_selected(state, death);
        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
            state.death_player = selected ? 0 : death.player;
            state.death_time_s = selected ? -1.0 : death.time_s;
        }
        player_cell(1, death.name, death.cls);
        const meter::RecapEvent* blow = death.killing_blow();
        if (blow != nullptr) {
            skill_cell(2, blow->skill);
            cell(3, colors::TextMuted, source_of(*blow));
            cell(4, colors::DangerLight, format_amount(blow->amount) + (blow->crit ? " !" : ""));
        } else {
            cell(2, colors::TextFaint, "No hit seen");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Nothing with an amount landed on them in the seconds before. Some mechanics "
                                  "kill with a hit of 0.");
            }
            cell(3, colors::TextFaint, "-");
            cell(4, colors::TextFaint, "-");
        }
        if (death.up()) {
            cell(5, colors::SuccessLight, format_seconds(death.up_after_s));
        } else {
            cell(5, colors::TextFaint, "-");
        }
    }
    ImGui::EndTable();
}

void render_recap(const meter::DeathRecord& death) {
    begin_card("##DeathRecap", ImVec2(0.0f, fill_h(0.0f)));
    const std::string heading = "LAST HITS - " + death.name;
    section_header(ICON_SKULL, heading.c_str(), colors::Danger);
    if (death.recap.empty()) {
        empty_state(ICON_SKULL, "No recap", "Nothing was seen landing on this player before they died.");
        end_card();
        return;
    }
    const Column cols[] = {
        {"When", 64.0f, "Before the death.", 0},
        {"Event", 52.0f, nullptr, 3},
        {"Skill", 0.0f, nullptr, 1},
        {"Source", 150.0f, "The enemy that hit them, or the player who healed them.", 4},
        {"Amount", 90.0f, "\"!\" marks a critical hit.", 2},
    };
    if (begin_table("##DeathRecapTable", cols, static_cast<int>(std::size(cols)), fill_h(0.0f))) {
        const meter::RecapEvent* blow = death.killing_blow();
        for (const meter::RecapEvent& e : death.recap) {
            const bool heal = !e.damage();
            const uint32_t tone = heal ? colors::SuccessLight : colors::DangerLight;
            ImGui::TableNextRow();
            cell(0, colors::TextDim, (e.before_s >= 0.05 ? "-" : "") + format_seconds(e.before_s));
            cell(1, tone, kind_label(e.kind));
            if (&e == blow) ImGui::PushFont(bold_font());
            skill_cell(2, e.skill, &e == blow ? colors::TextPrimary : colors::TextBody);
            if (&e == blow) ImGui::PopFont();
            cell(3, colors::TextMuted, source_of(e));
            cell(4, tone, std::string(heal ? "+" : "-") + format_amount(e.amount) + (e.crit ? " !" : ""));
        }
        ImGui::TableNextRow();
        cell(0, colors::TextDim, format_seconds(0.0));
        cell(1, colors::Danger, ICON_SKULL);
        cell(2, colors::TextPrimary, "Died");
        ImGui::EndTable();
    }
    end_card();
}

} // namespace

void deaths_tab(const meter::FightSummary& f, AppViewState& state) {
    if (f.detail == nullptr) {
        no_detail_state(ICON_SKULL);
        return;
    }
    const meter::FightDetail& detail = *f.detail;
    if (detail.deaths.empty()) {
        empty_state(ICON_SKULL, "No deaths",
                    "A party member's death shows here with what killed them and the hits before it.");
        return;
    }
    const meter::DeathRecord* selected = nullptr;
    for (const meter::DeathRecord& death : detail.deaths) {
        if (is_selected(state, death)) selected = &death;
    }
    // A selection gone from this fight is let go of.
    if (selected == nullptr) {
        state.death_player = 0;
        state.death_time_s = -1.0;
    }
    const float row_h = ImGui::GetTextLineHeight() + m(10.0f);
    const float log_h = selected != nullptr
                            ? std::min(fill_h(0.0f) * 0.45f, (static_cast<float>(detail.deaths.size()) + 1.5f) * row_h)
                            : fill_h(0.0f);
    render_log(detail, state, log_h);
    if (selected == nullptr) return;
    ImGui::Dummy(ImVec2(0.0f, m(4.0f)));
    render_recap(*selected);
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
