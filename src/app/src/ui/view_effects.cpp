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
#include <array>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
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

void view_button(AppViewState& state, const char* label, EffectView view) {
    if (button(label, state.effect_view == view ? ButtonKind::Primary : ButtonKind::Secondary, ButtonSize::Fit)) {
        state.effect_view = view;
    }
}

/// Food or a scroll up this much of the fight counts as kept up: one used as the
/// pull began misses the moments before it.
constexpr double kConsumableFull = 0.95;

/// One item's effect on one player in a fight, its rows joined.
struct Consumable {
    uint32_t buff_id{0};
    uint32_t applications{0};
    double active_s{0.0};
};

/// A player's consumables, by game::ItemEffect (None unused).
struct PlayerConsumables {
    std::array<std::vector<Consumable>, static_cast<size_t>(game::ItemEffect::Scroll) + 1> by_kind;
};

void add_to(std::vector<Consumable>& list, const meter::EffectUptimeRow& row) {
    for (Consumable& c : list) {
        if (c.buff_id == row.buff_id) {
            c.applications += row.applications;
            c.active_s += row.active_s;
            return;
        }
    }
    list.push_back(Consumable{row.buff_id, row.applications, row.active_s});
}

/// Food, scrolls and potions are on the player who took them.
std::vector<PlayerConsumables> consumables_of(const meter::FightSummary& f) {
    std::vector<PlayerConsumables> out(f.combatants.size());
    for (const meter::EffectUptimeRow& row : f.detail->effects) {
        const game::ItemEffect kind = game::item_effect(row.buff_id);
        if (kind == game::ItemEffect::None) continue;
        for (size_t i = 0; i < f.combatants.size(); ++i) {
            const meter::CombatantTotals& c = f.combatants[i];
            if (row.side != meter::EffectSide::PartyBuff || row.target != c.id) continue;
            add_to(out[i].by_kind[static_cast<size_t>(kind)], row);
            break;
        }
    }
    for (PlayerConsumables& p : out) {
        for (auto& list : p.by_kind) {
            std::sort(list.begin(), list.end(), [](const Consumable& a, const Consumable& b) {
                if (a.active_s != b.active_s) return a.active_s > b.active_s;
                return a.buff_id < b.buff_id;
            });
        }
    }
    return out;
}

/// An item's name in its column: the column already says it is a scroll ("Speed").
std::string short_name(game::ItemEffect kind, uint32_t buff_id) {
    std::string name = game::effect_label(buff_id);
    constexpr std::string_view kScroll = " Scroll";
    if (kind == game::ItemEffect::Scroll && name.ends_with(kScroll) && name.size() > kScroll.size()) {
        name.resize(name.size() - kScroll.size());
    }
    return name;
}

/// Each consumable on a line of its own, its icon, then its name wrapped to the
/// column. Food and scrolls kept up all fight show their name alone; one that was
/// not, in amber with how much of the fight it was up. Potions show how many times.
void consumable_cell(int column, game::ItemEffect kind, const std::vector<Consumable>& list, double duration) {
    if (!ImGui::TableSetColumnIndex(column)) return;
    if (list.empty()) {
        text_colored_u32(colors::TextDim, "-");
        return;
    }
    const bool by_count = kind == game::ItemEffect::Potion;
    const float side_px = ImGui::GetTextLineHeight();
    ImGui::PushTextWrapPos(0.0f);
    for (const Consumable& c : list) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        draw_icon(ImGui::GetWindowDrawList(), effect_icon(c.buff_id), p.x, p.y, side_px);
        ImGui::SetCursorScreenPos(ImVec2(p.x + side_px + m(6.0f), p.y));
        const double uptime = std::clamp(c.active_s / duration, 0.0, 1.0);
        const bool short_of_fight = !by_count && uptime < kConsumableFull;
        std::string text = short_name(kind, c.buff_id);
        if (by_count) {
            text += "  x" + std::to_string(c.applications);
        } else if (short_of_fight) {
            text += "  " + std::to_string(static_cast<int>(uptime * 100.0)) + "%";
        }
        text_colored_u32(short_of_fight ? colors::Warning : colors::TextBody, "%s", text.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\nUp %s of the fight (%s), put on %u time%s.",
                              game::effect_label(c.buff_id).c_str(), format_percent(uptime).c_str(),
                              format_seconds(c.active_s).c_str(), c.applications, c.applications == 1 ? "" : "s");
        }
    }
    ImGui::PopTextWrapPos();
}

void consumables_table(const meter::FightSummary& f) {
    if (f.combatants.empty()) {
        empty_state(ICON_FLASK, "No one in this fight", "Each player's food, scrolls and potions show here.");
        return;
    }
    const std::vector<PlayerConsumables> players = consumables_of(f);
    const Column cols[] = {
        {"Player", 120.0f, nullptr, 0},
        {"Food", 0.0f, "Food and drinks. In amber, one that was not up the whole fight, with how much of it.", 1},
        {"Scrolls", 0.0f, "In amber, one that was not up the whole fight, with how much of it.", 2},
        {"Potions", 0.0f, "Potions drunk during the fight.", 3},
    };
    if (!begin_table("##ConsumableTable", cols, static_cast<int>(std::size(cols)), fill_h(0.0f))) return;
    const double duration = std::max(f.duration_s, 1.0);
    constexpr game::ItemEffect kColumns[] = {game::ItemEffect::Food, game::ItemEffect::Scroll,
                                             game::ItemEffect::Potion};
    for (size_t i = 0; i < f.combatants.size(); ++i) {
        const meter::CombatantTotals& c = f.combatants[i];
        ImGui::TableNextRow();
        player_cell(0, c.name, c.cls);
        for (int col = 0; col < static_cast<int>(std::size(kColumns)); ++col) {
            const game::ItemEffect kind = kColumns[col];
            consumable_cell(col + 1, kind, players[i].by_kind[static_cast<size_t>(kind)], duration);
        }
    }
    ImGui::EndTable();
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
    const char* consumables_label = ICON_FLASK "  Consumables##EffectView3";
    view_button(state, ICON_SHIELD "  Buffs on party##EffectView0", EffectView::PartyBuff);
    same_line_if_room(button_width(debuffs_label, ButtonSize::Fit));
    view_button(state, debuffs_label, EffectView::PartyDebuff);
    same_line_if_room(button_width(enemies_label, ButtonSize::Fit));
    view_button(state, enemies_label, EffectView::OnEnemy);
    same_line_if_room(button_width(consumables_label, ButtonSize::Fit));
    view_button(state, consumables_label, EffectView::Consumables);
    ImGui::Dummy(ImVec2(0.0f, m(4.0f)));

    if (state.effect_view == EffectView::Consumables) {
        consumables_table(f);
        return;
    }
    static_assert(static_cast<int>(EffectView::OnEnemy) == static_cast<int>(meter::EffectSide::OnEnemy),
                  "the first three views are the sides, in their order");
    const auto side = static_cast<meter::EffectSide>(state.effect_view);
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
