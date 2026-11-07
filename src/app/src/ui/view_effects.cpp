#include "app/ui/meter_tabs.hpp"

#include "app/display_format.hpp"
#include "app/game_icons.hpp"
#include "app/ui/icons.hpp"
#include "app/ui/table.hpp"
#include "app/ui/theme.hpp"
#include "app/ui/widgets.hpp"
#include "game/skill_names.hpp"
#include "meter/fight_detail.hpp"

#ifdef HAVE_IMGUI
#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>
#endif

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

namespace {

/// Every row of one effect in the current view.
struct EffectGroup {
    uint32_t key{0};
    uint32_t buff_id{0};
    uint32_t source_skill{0};
    std::vector<const meter::EffectUptimeRow*> rows;
    uint32_t applications{0};
    double active_s{0.0};
    std::set<std::string> targets;
    std::set<std::string> casters;
    /// 0..1.
    double uptime{0.0};
};

std::vector<EffectGroup> group_rows(const meter::FightSummary& f, meter::EffectSide side) {
    std::map<uint32_t, EffectGroup> groups;
    for (const meter::EffectUptimeRow& row : f.detail->effects) {
        if (row.side != side) continue;
        EffectGroup& g = groups[row.key];
        if (g.rows.empty()) {
            g.key = row.key;
            g.buff_id = row.buff_id;
            g.source_skill = row.source_skill;
        }
        g.rows.push_back(&row);
        g.applications += row.applications;
        g.active_s += row.active_s;
        g.targets.insert(row.target_name);
        g.casters.insert(row.caster.empty() ? std::string("?") : row.caster);
    }
    const double duration = std::max(f.duration_s, 1.0);
    std::vector<EffectGroup> sorted;
    sorted.reserve(groups.size());
    for (auto& [key, g] : groups) {
        double best = 0.0;
        for (const meter::EffectUptimeRow* row : g.rows) best = std::max(best, row->uptime(duration));
        // On the party, how long an affected member had it on average; on an enemy,
        // the best kept by one caster, which is what a DoT's uptime means.
        g.uptime = side == meter::EffectSide::OnEnemy
                       ? best
                       : std::min(g.active_s / (static_cast<double>(g.targets.size()) * duration), 1.0);
        std::sort(g.rows.begin(), g.rows.end(), [](const meter::EffectUptimeRow* a, const meter::EffectUptimeRow* b) {
            return a->active_s > b->active_s;
        });
        sorted.push_back(std::move(g));
    }
    // Debuffs on the party are mechanics to avoid: the most frequent first.
    std::sort(sorted.begin(), sorted.end(), [side](const EffectGroup& a, const EffectGroup& b) {
        if (side == meter::EffectSide::PartyDebuff && a.applications != b.applications) {
            return a.applications > b.applications;
        }
        if (a.uptime != b.uptime) return a.uptime > b.uptime;
        return a.key < b.key;
    });
    return sorted;
}

/// "Lumiel", or "Lumiel +2".
std::string first_and_more(const std::set<std::string>& names) {
    if (names.empty()) return "-";
    std::string out = *names.begin();
    if (names.size() > 1) out += " +" + std::to_string(names.size() - 1);
    return out;
}

void view_button(AppViewState& state, const char* label, meter::EffectSide side) {
    if (button(label, state.effect_view == side ? ButtonKind::Primary : ButtonKind::Secondary, ButtonSize::Fit)) {
        state.effect_view = side;
    }
}

void render_group(const EffectGroup& group, const meter::FightSummary& f, meter::EffectSide side) {
    ImGui::TableNextRow();
    row_progress_bar(static_cast<float>(group.uptime),
                     side == meter::EffectSide::PartyBuff ? colors::Success
                     : side == meter::EffectSide::OnEnemy ? colors::Accent
                                                          : colors::Danger);
    ImGui::TableSetColumnIndex(0);
    const std::string id = "##Effect" + std::to_string(group.key);
    const bool open = ImGui::TreeNodeEx(id.c_str(), ImGuiTreeNodeFlags_SpanAllColumns);
    ImGui::SameLine(0.0f, m(2.0f));
    const float side_px = ImGui::GetTextLineHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    draw_icon(ImGui::GetWindowDrawList(), effect_icon(group.buff_id, group.source_skill), p.x, p.y, side_px);
    ImGui::SetCursorScreenPos(ImVec2(p.x + side_px + m(6.0f), p.y));
    text_colored_u32(colors::TextPrimary, "%s", game::effect_label(group.buff_id, group.source_skill).c_str());

    cell(1, colors::TextMuted, first_and_more(group.casters));
    cell(2, colors::TextMuted,
         side == meter::EffectSide::OnEnemy ? first_and_more(group.targets) : std::to_string(group.targets.size()));
    cell(3, colors::TextBody, number(group.applications));
    cell(4, colors::TextMuted, format_seconds(group.active_s));
    cell(5, colors::TextPrimary, format_percent(group.uptime));

    if (!open) return;
    const double duration = std::max(f.duration_s, 1.0);
    for (const meter::EffectUptimeRow* row : group.rows) {
        ImGui::TableNextRow();
        if (ImGui::TableSetColumnIndex(0)) {
            ImGui::Indent(m(18.0f));
            text_colored_u32(class_or(row->target_cls, colors::TextBody), "%s", row->target_name.c_str());
            ImGui::Unindent(m(18.0f));
        }
        if (ImGui::TableSetColumnIndex(1)) {
            text_colored_u32(class_or(row->caster_cls, colors::TextDim), "%s",
                             row->caster.empty() ? "?" : row->caster.c_str());
        }
        cell(2, colors::TextDim, "");
        cell(3, colors::TextMuted, number(row->applications));
        cell(4, colors::TextMuted, format_seconds(row->active_s));
        cell(5, colors::TextBody, format_percent(row->uptime(duration)));
    }
    ImGui::TreePop();
}

} // namespace

void effects_tab(const meter::FightSummary& f, AppViewState& state) {
    if (f.detail == nullptr) {
        no_detail_state(ICON_SPARKLE);
        return;
    }
    const char* debuffs_label = ICON_WARNING "  Debuffs on party##EffectView1";
    const char* enemies_label = ICON_TARGET "  On enemies##EffectView2";
    view_button(state, ICON_SHIELD "  Buffs on party##EffectView0", meter::EffectSide::PartyBuff);
    same_line_if_room(button_width(debuffs_label, ButtonSize::Fit));
    view_button(state, debuffs_label, meter::EffectSide::PartyDebuff);
    same_line_if_room(button_width(enemies_label, ButtonSize::Fit));
    view_button(state, enemies_label, meter::EffectSide::OnEnemy);
    ImGui::Dummy(ImVec2(0.0f, m(4.0f)));

    const meter::EffectSide side = state.effect_view;
    const std::vector<EffectGroup> groups = group_rows(f, side);
    if (groups.empty()) {
        const char* title = side == meter::EffectSide::PartyBuff     ? "No buffs on the party"
                            : side == meter::EffectSide::PartyDebuff ? "No debuffs on the party"
                                                                     : "Nothing on the enemies";
        empty_state(ICON_SPARKLE, title, "Effects are tracked while a fight runs, with how long each stayed up.");
        return;
    }
    const bool on_enemy = side == meter::EffectSide::OnEnemy;
    const Column cols[] = {
        {"Effect", 0.0f, "Select one for each target and caster.", 0},
        {"From", 150.0f, "Who put it on.", 3},
        {on_enemy ? "On" : "Players", on_enemy ? 140.0f : 60.0f, on_enemy ? "The enemies it was on." : nullptr, 4},
        {"Applied", 64.0f, "Times it was put on, and those already running as the fight began.", 5},
        {"Time", 70.0f, "How long it was up; stacks count once.", 2},
        {"Uptime", 64.0f,
         on_enemy ? "Of the fight, by the caster who kept it up best."
                  : "Of the fight, on average over the party members who had it.",
         1},
    };
    if (!begin_table("##EffectTable", cols, static_cast<int>(std::size(cols)), fill_h(0.0f))) return;
    for (const EffectGroup& group : groups) render_group(group, f, side);
    ImGui::EndTable();
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
