#include "app/ui/views.hpp"

#include "app/display_format.hpp"
#include "app/ui/icons.hpp"
#include "app/ui/page.hpp"
#include "app/ui/widgets.hpp"
#include "common/time_format.hpp"
#include "meter/map_names.hpp"
#include "meter/npc_names.hpp"
#include "meter/pull_grouping.hpp"
#include "meter/skill_names.hpp"

#ifdef HAVE_IMGUI
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>
#endif

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

namespace {

/// Every table on the page: a table that does not fit scrolls rather than crushing
/// its columns (table_sizing). PadOuterX keeps the edge columns off the sides.
constexpr ImGuiTableFlags kTableFlags =
    ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX;

struct Badge {
    const char* label;
    uint32_t color;
};

/// What the tables show: the fight, and where it sits in the history.
struct Shown {
    const meter::FightSummary* fight{nullptr};
    /// Null for the live fight.
    const meter::HistoryEntry* entry{nullptr};
    int number{0};
};

uint32_t row_color_u32(const meter::CombatantTotals& c) {
    if (c.cls != meter::AionClass::Unknown) return class_color_u32(c.cls);
    return c.is_local ? colors::Accent : colors::TextMuted;
}

Badge outcome(bool live, bool killed, meter::EndReason reason) {
    if (live) return {"In combat", colors::SuccessLight};
    if (killed) return {"Killed", colors::Gold};
    if (reason == meter::EndReason::Wipe) return {"Wipe", colors::DangerLight};
    return {"Ended", colors::TextDim};
}

const char* reason_text(meter::EndReason r) {
    switch (r) {
        case meter::EndReason::BossKilled: return "the boss died";
        case meter::EndReason::MapChange: return "a map change";
        case meter::EndReason::Wipe: return "a wipe";
        case meter::EndReason::Idle: return "no party hit for a while";
        case meter::EndReason::Manual: return "ended by hand";
        case meter::EndReason::CaptureLost: return "the game's traffic stopped";
        case meter::EndReason::None: break;
    }
    return "still going";
}

/// A share of the hits, or "-" with no direct hits to rate.
std::string rate(const meter::HitCounts& h, uint64_t meter::HitCounts::*count) {
    return h.hits == 0 ? std::string("-") : format_percent(h.rate(count));
}

std::string share(uint64_t part, uint64_t whole) {
    return whole == 0 ? std::string("-") : format_percent(static_cast<double>(part) / static_cast<double>(whole));
}

std::string number(uint64_t n) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%" PRIu64, n);
    return buf;
}

// ------------------------------------------------------------------ tables ---

struct Column {
    const char* name;
    /// Unscaled pixels; 0 stretches.
    float width;
    const char* tip;
    /// 0 is shown first; the highest go when the table is narrow.
    int priority;
};

/// A ranking or breakdown table: columns, the header row with each column's
/// tooltip, scrolling with the header frozen. A table narrower than its columns
/// drops the least important ones (by priority) rather than scroll sideways; only
/// one too narrow even for those scrolls. False when nothing is to be drawn.
bool begin_table(const char* id, const Column* cols, int count, float height) {
    // What a column costs beyond its content: its cell padding and its border.
    const float chrome = ImGui::GetStyle().CellPadding.x * 2.0f + 1.0f;
    const float avail = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
    std::vector<int> order(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) order[static_cast<size_t>(i)] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return cols[a].priority < cols[b].priority; });
    std::vector<bool> shown(static_cast<size_t>(count), false);
    float used = 0.0f;
    float natural = 0.0f;
    for (const int i : order) {
        const float w = m(cols[i].width > 0.0f ? cols[i].width : 110.0f) + chrome;
        if (used > 0.0f && used + w > avail) continue;
        used += w;
        natural += cols[i].width > 0.0f ? cols[i].width : 110.0f;
        shown[static_cast<size_t>(i)] = true;
    }
    const auto sizing = table_sizing(natural, count, kTableFlags);
    if (!ImGui::BeginTable(id, count, sizing.flags, ImVec2(0.0f, height))) return false;
    for (int i = 0; i < count; ++i) {
        const ImGuiTableColumnFlags hide = shown[static_cast<size_t>(i)] ? 0 : ImGuiTableColumnFlags_Disabled;
        if (cols[i].width > 0.0f) {
            ImGui::TableSetupColumn(cols[i].name, ImGuiTableColumnFlags_WidthFixed | hide, m(cols[i].width));
        } else {
            ImGui::TableSetupColumn(cols[i].name, sizing.flex_flags() | hide, sizing.flex_width(110.0f, 1.0f));
        }
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::PushStyleColor(ImGuiCol_Text, v4(colors::TextDim));
    for (int i = 0; i < count; ++i) {
        if (!ImGui::TableSetColumnIndex(i)) continue;
        ImGui::TableHeader(cols[i].name);
        if (cols[i].tip != nullptr && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", cols[i].tip);
    }
    ImGui::PopStyleColor();
    return true;
}

/// A cell's text; nothing for a column dropped for width.
void cell(int column, uint32_t color, const std::string& text) {
    if (!ImGui::TableSetColumnIndex(column)) return;
    text_colored_u32(color, "%s", text.c_str());
}

/// The row's player: a selectable across the row, which opens their breakdown.
void name_cell(int column, const meter::CombatantTotals& c, meter::EntityId& selected, const char* tab) {
    ImGui::TableSetColumnIndex(column);
    const std::string label = c.name + "##" + tab + std::to_string(c.id);
    const bool is_selected = selected == c.id;
    ImGui::PushStyleColor(ImGuiCol_Text, v4(c.is_local ? colors::AccentHover : colors::TextPrimary));
    if (ImGui::Selectable(label.c_str(), is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
        selected = is_selected ? 0 : c.id;
    }
    ImGui::PopStyleColor();
}

const char* const kTipHits = "HIT: direct hits. DoT ticks are counted apart and carry no crit or direction.";
const char* const kTipCrit = "CRI: critical hits, of the direct hits.";
const char* const kTipPerfect = "PERF: perfect hits, of the direct hits.";
const char* const kTipDouble = "DOUB: double hits, of the direct hits.";
const char* const kTipBack = "BACK: hits from behind the target, of the direct hits.";
const char* const kTipFront = "FRNT: hits from the target's front, of the direct hits.";

/// The table's height when a breakdown shares the space under it.
float table_height(bool breakdown_open) { return breakdown_open ? fill_h(0.0f) * 0.5f : fill_h(0.0f); }

const meter::CombatantTotals* find_row(const meter::FightSummary& f, meter::EntityId id) {
    if (id == 0) return nullptr;
    for (const auto& c : f.combatants) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

/// The card a breakdown sits in, titled with the player, with a Close button.
bool begin_breakdown(const char* id, const char* title, const meter::CombatantTotals& c, meter::EntityId& selected) {
    ImGui::Dummy(ImVec2(0.0f, m(4.0f)));
    begin_card(id, ImVec2(0.0f, fill_h(0.0f)));
    char header[160];
    std::snprintf(header, sizeof(header), "%s - %s", title, c.name.c_str());
    const char* close_label = ICON_CIRCLE_X "  Close";
    begin_section_header(ICON_CROSSHAIR, header, button_width(close_label, ButtonSize::Small), row_color_u32(c));
    const bool close = button(close_label, ButtonKind::Secondary, ButtonSize::Small);
    end_section_header();
    if (close) {
        selected = 0;
        end_card();
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ damage ---

void damage_breakdown(const meter::CombatantTotals& c, meter::EntityId& selected) {
    if (!begin_breakdown("##DamageBreakdown", "SKILLS", c, selected)) return;
    if (c.skills.empty()) {
        empty_state(ICON_CROSSHAIR, "No skills recorded", "This player has not landed anything yet.");
        end_card();
        return;
    }
    const Column cols[] = {
        {"Skill", 0.0f, nullptr, 0},
        {"Damage", 64.0f, "All of the skill's damage: its hits and its ticks.", 1},
        {"Share", 52.0f, "Of the player's damage.", 2},
        {"HIT", 40.0f, kTipHits, 3},
        {"Min", 58.0f, "The smallest direct hit.", 11},
        {"Avg", 58.0f, "The average direct hit.", 5},
        {"Max", 58.0f, "The biggest direct hit.", 6},
        {"CRI", 50.0f, kTipCrit, 4},
        {"PERF", 50.0f, kTipPerfect, 8},
        {"DOUB", 50.0f, kTipDouble, 9},
        {"BACK", 50.0f, kTipBack, 7},
        {"FRNT", 50.0f, kTipFront, 10},
        {"DoT", 80.0f, "Damage from the skill's ticks, and how many.", 12},
    };
    if (begin_table("##SkillTable", cols, static_cast<int>(std::size(cols)), fill_h(0.0f))) {
        const uint64_t top = std::max<uint64_t>(c.skills.front().damage, 1);
        for (const auto& s : c.skills) {
            ImGui::TableNextRow();
            row_progress_bar(static_cast<float>(static_cast<double>(s.damage) / static_cast<double>(top)),
                             row_color_u32(c));
            const uint64_t direct = s.damage - std::min(s.damage, s.tick_damage);
            cell(0, colors::TextBody, meter::skill_label(s.code));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Skill code %u", s.code);
            cell(1, colors::TextPrimary, format_amount(s.damage));
            cell(2, colors::TextMuted, share(s.damage, c.damage));
            cell(3, colors::TextMuted, number(s.hits.hits));
            cell(4, colors::TextMuted, s.hits.hits == 0 ? "-" : format_amount(s.min_hit));
            cell(5, colors::TextBody, s.hits.hits == 0 ? "-" : format_amount(direct / s.hits.hits));
            cell(6, colors::TextMuted, s.hits.hits == 0 ? "-" : format_amount(s.max_hit));
            cell(7, colors::TextMuted, rate(s.hits, &meter::HitCounts::crits));
            cell(8, colors::TextMuted, rate(s.hits, &meter::HitCounts::perfect));
            cell(9, colors::TextMuted, rate(s.hits, &meter::HitCounts::doubles));
            cell(10, colors::TextMuted, rate(s.hits, &meter::HitCounts::back));
            cell(11, colors::TextMuted, rate(s.hits, &meter::HitCounts::front));
            cell(12, colors::TextDim, s.ticks == 0 ? "-" : format_amount(s.tick_damage) + " (" + number(s.ticks) + ")");
        }
        ImGui::EndTable();
    }
    end_card();
}

void damage_tab(const meter::FightSummary& f, AppViewState& state) {
    std::vector<const meter::CombatantTotals*> rows;
    for (const auto& c : f.combatants) {
        if (c.damage > 0) rows.push_back(&c);
    }
    if (rows.empty()) {
        empty_state(ICON_SWORDS, "No damage yet", "The ranking fills in as the party hits.");
        return;
    }
    const meter::CombatantTotals* open = find_row(f, state.damage_player);
    const Column cols[] = {
        {"#", 24.0f, nullptr, 12},
        {"Class", 46.0f, "From the skills the player used.", 7},
        {"Player", 0.0f, "Select a player for their skills.", 0},
        {"DPS", 60.0f, "Damage per second, from the fight's first hit to its last.", 1},
        {"Damage", 64.0f, "Direct hits and DoT ticks.", 2},
        {"Share", 52.0f, "Of the party's damage.", 3},
        {"HIT", 40.0f, kTipHits, 6},
        {"CRI", 50.0f, kTipCrit, 4},
        {"PERF", 50.0f, kTipPerfect, 8},
        {"DOUB", 50.0f, kTipDouble, 9},
        {"BACK", 50.0f, kTipBack, 5},
        {"FRNT", 50.0f, kTipFront, 10},
        {"Max", 58.0f, "The player's biggest hit.", 11},
    };
    if (begin_table("##DamageTable", cols, static_cast<int>(std::size(cols)), table_height(open != nullptr))) {
        const double top = std::max(rows.front()->dps, 1.0);
        int rank = 0;
        for (const auto* c : rows) {
            ImGui::TableNextRow();
            row_progress_bar(static_cast<float>(c->dps / top), row_color_u32(*c));
            cell(0, colors::TextMuted, std::to_string(++rank));
            if (ImGui::TableSetColumnIndex(1)) class_badge(c->cls);
            name_cell(2, *c, state.damage_player, "D");
            if (ImGui::TableSetColumnIndex(3)) {
                ImGui::PushFont(bold_font());
                text_colored_u32(row_color_u32(*c), "%s", format_rate(c->dps).c_str());
                ImGui::PopFont();
            }
            cell(4, colors::TextBody, format_amount(c->damage));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%" PRIu64 " damage, %" PRIu64 " of it from DoTs", c->damage, c->dot_damage);
            }
            cell(5, colors::TextMuted, format_percent(c->share));
            cell(6, colors::TextMuted, number(c->hits.hits));
            cell(7, colors::TextMuted, rate(c->hits, &meter::HitCounts::crits));
            cell(8, colors::TextMuted, rate(c->hits, &meter::HitCounts::perfect));
            cell(9, colors::TextMuted, rate(c->hits, &meter::HitCounts::doubles));
            cell(10, colors::TextMuted, rate(c->hits, &meter::HitCounts::back));
            cell(11, colors::TextMuted, rate(c->hits, &meter::HitCounts::front));
            cell(12, colors::TextMuted, format_amount(c->max_hit));
        }
        ImGui::EndTable();
    }
    if (open != nullptr) damage_breakdown(*open, state.damage_player);
}

// ----------------------------------------------------------------- healing ---

void healing_tab(const meter::FightSummary& f, AppViewState& state) {
    std::vector<const meter::CombatantTotals*> rows;
    for (const auto& c : f.combatants) {
        if (c.healing > 0) rows.push_back(&c);
    }
    std::sort(rows.begin(), rows.end(), [](const auto* a, const auto* b) { return a->healing > b->healing; });
    if (rows.empty()) {
        empty_state(ICON_HEART, "No healing yet", "Heals and heal ticks on the party show here.");
        return;
    }
    const meter::CombatantTotals* open = find_row(f, state.heal_player);
    if (open != nullptr && open->healing == 0) open = nullptr;
    const Column cols[] = {
        {"#", 24.0f, nullptr, 6},
        {"Class", 46.0f, nullptr, 5},
        {"Player", 0.0f, "Select a player for their heals by skill.", 0},
        {"HPS", 60.0f, "Healing per second, over the fight's length.", 1},
        {"Healing", 64.0f, "All of it, as the game's own window counts it: no overheal split.", 2},
        {"Share", 52.0f, "Of the party's healing.", 3},
        {"Heals", 50.0f, "Heal records and heal ticks.", 4},
    };
    if (begin_table("##HealTable", cols, static_cast<int>(std::size(cols)), table_height(open != nullptr))) {
        const double top = std::max(rows.front()->hps, 1.0);
        int rank = 0;
        for (const auto* c : rows) {
            ImGui::TableNextRow();
            row_progress_bar(static_cast<float>(c->hps / top), colors::Success);
            cell(0, colors::TextMuted, std::to_string(++rank));
            if (ImGui::TableSetColumnIndex(1)) class_badge(c->cls);
            name_cell(2, *c, state.heal_player, "H");
            if (ImGui::TableSetColumnIndex(3)) {
                ImGui::PushFont(bold_font());
                text_colored_u32(colors::Heal, "%s", format_rate(c->hps).c_str());
                ImGui::PopFont();
            }
            cell(4, colors::TextBody, format_amount(c->healing));
            cell(5, colors::TextMuted, share(c->healing, f.total_healing));
            cell(6, colors::TextMuted, number(c->heal_hits));
        }
        ImGui::EndTable();
    }
    if (open == nullptr || !begin_breakdown("##HealBreakdown", "HEALS", *open, state.heal_player)) return;
    const Column skill_cols[] = {
        {"Skill", 0.0f, nullptr, 0},
        {"Healing", 64.0f, nullptr, 1},
        {"Share", 52.0f, "Of the player's healing.", 2},
        {"Heals", 50.0f, nullptr, 3},
        {"Max", 58.0f, "The biggest heal.", 4},
    };
    if (begin_table("##HealSkills", skill_cols, static_cast<int>(std::size(skill_cols)), fill_h(0.0f))) {
        const uint64_t top = open->heal_skills.empty() ? 1 : std::max<uint64_t>(open->heal_skills.front().amount, 1);
        for (const auto& s : open->heal_skills) {
            ImGui::TableNextRow();
            row_progress_bar(static_cast<float>(static_cast<double>(s.amount) / static_cast<double>(top)),
                             colors::Success);
            cell(0, colors::TextBody, meter::skill_label(s.code));
            cell(1, colors::TextPrimary, format_amount(s.amount));
            cell(2, colors::TextMuted, share(s.amount, open->healing));
            cell(3, colors::TextMuted, number(s.hits));
            cell(4, colors::TextMuted, format_amount(s.max));
        }
        ImGui::EndTable();
    }
    end_card();
}

// ------------------------------------------------------------ damage taken ---

void taken_tab(const meter::FightSummary& f, AppViewState& state) {
    std::vector<const meter::CombatantTotals*> rows;
    for (const auto& c : f.combatants) {
        if (c.damage_taken > 0) rows.push_back(&c);
    }
    std::sort(rows.begin(), rows.end(), [](const auto* a, const auto* b) { return a->damage_taken > b->damage_taken; });
    if (rows.empty()) {
        empty_state(ICON_SHIELD, "No damage taken yet", "Enemy hits on the party show here.");
        return;
    }
    const meter::CombatantTotals* open = find_row(f, state.taken_player);
    if (open != nullptr && open->damage_taken == 0) open = nullptr;
    const Column cols[] = {
        {"#", 24.0f, nullptr, 7},
        {"Class", 46.0f, nullptr, 6},
        {"Player", 0.0f, "Select a player for what hit them.", 0},
        {"Taken", 64.0f, "Enemy hits and ticks that landed on the player.", 1},
        {"HIT", 40.0f, "Direct hits taken.", 2},
        {"CRI", 50.0f, "Critical hits, of the hits taken.", 3},
        {"PERF", 50.0f, "Perfect hits, of the hits taken.", 4},
        {"FRNT", 50.0f, "Hits to the player's front, of the hits taken.", 5},
    };
    if (begin_table("##TakenTable", cols, static_cast<int>(std::size(cols)), table_height(open != nullptr))) {
        const uint64_t top = std::max<uint64_t>(rows.front()->damage_taken, 1);
        int rank = 0;
        for (const auto* c : rows) {
            ImGui::TableNextRow();
            row_progress_bar(static_cast<float>(static_cast<double>(c->damage_taken) / static_cast<double>(top)),
                             colors::Danger);
            cell(0, colors::TextMuted, std::to_string(++rank));
            if (ImGui::TableSetColumnIndex(1)) class_badge(c->cls);
            name_cell(2, *c, state.taken_player, "T");
            cell(3, colors::TextPrimary, format_amount(c->damage_taken));
            cell(4, colors::TextMuted, number(c->taken.hits));
            cell(5, colors::TextMuted, rate(c->taken, &meter::HitCounts::crits));
            cell(6, colors::TextMuted, rate(c->taken, &meter::HitCounts::perfect));
            cell(7, colors::TextMuted, rate(c->taken, &meter::HitCounts::front));
        }
        ImGui::EndTable();
    }
    if (open == nullptr || !begin_breakdown("##TakenBreakdown", "HIT BY", *open, state.taken_player)) return;
    const Column skill_cols[] = {
        {"Skill", 0.0f, "The enemy's skill. Enemy skills have no names in the table: their codes show.", 0},
        {"Source", 100.0f, nullptr, 3},
        {"Damage", 64.0f, nullptr, 1},
        {"Share", 52.0f, "Of the player's damage taken.", 2},
        {"Hits", 40.0f, nullptr, 4},
        {"Max", 58.0f, "The biggest hit.", 5},
    };
    if (begin_table("##TakenSkills", skill_cols, static_cast<int>(std::size(skill_cols)), fill_h(0.0f))) {
        const uint64_t top = open->taken_by.empty() ? 1 : std::max<uint64_t>(open->taken_by.front().amount, 1);
        for (const auto& s : open->taken_by) {
            ImGui::TableNextRow();
            row_progress_bar(static_cast<float>(static_cast<double>(s.amount) / static_cast<double>(top)),
                             colors::Danger);
            cell(0, colors::TextBody, meter::skill_label(s.code));
            cell(1, colors::TextMuted, s.source_npc != 0 ? meter::npc_label(s.source_npc) : std::string("-"));
            cell(2, colors::TextPrimary, format_amount(s.amount));
            cell(3, colors::TextMuted, share(s.amount, open->damage_taken));
            cell(4, colors::TextMuted, number(s.hits));
            cell(5, colors::TextMuted, format_amount(s.max));
        }
        ImGui::EndTable();
    }
    end_card();
}

// -------------------------------------------------------------------- rail ---

float rail_row_height() { return ImGui::GetTextLineHeight() + m(6.0f); }

/// One saved fight: outcome dot, number and duration, then the boss's HP left and
/// the start time while they fit. The tooltip carries all of it.
bool pull_row(const meter::HistoryEntry& e, int number_in_visit, bool selected, float number_w) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float right = pos.x + ImGui::GetContentRegionAvail().x;
    const float row_h = rail_row_height();
    const bool clicked = ImGui::Selectable(("##pull" + e.file).c_str(), selected, ImGuiSelectableFlags_None,
                                           ImVec2(0.0f, row_h));
    // A long visit lists every fight; only the rows on screen are worth formatting.
    if (!ImGui::IsItemVisible()) return clicked;

    const Badge o = outcome(false, e.killed, e.end_reason);
    const std::string num = "#" + std::to_string(number_in_visit);
    const std::string duration = common::clock_duration(e.duration_s);
    const std::string clock = common::local_clock(e.start_ts);
    const std::string boss_hp = e.boss_hp_known ? format_percent(e.boss_hp_left) : std::string();
    if (ImGui::IsItemHovered()) {
        std::string tip = "Fight " + num + " - " + o.label;
        if (!e.boss_label.empty()) tip += "\n" + std::string(e.is_boss ? "Boss " : "") + e.boss_label;
        if (!boss_hp.empty()) tip += " at " + boss_hp;
        tip += "\nStarted " + clock + ", " + duration + ", " + format_rate(e.total_dps) + " party DPS";
        tip += "\nEnded by " + std::string(reason_text(e.end_reason));
        if (e.partial) tip += "\nPartial data: the server sent only some players' hits";
        ImGui::SetTooltip("%s", tip.c_str());
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line_h = ImGui::GetTextLineHeight();
    const float text_y = pos.y + (row_h - line_h) * 0.5f;
    const float gap = m(8.0f);
    const float dot_r = m(3.0f);
    float x = pos.x;
    dl->AddCircleFilled(ImVec2(x + dot_r, text_y + line_h * 0.5f), dot_r, o.color);
    x += dot_r * 2.0f + m(6.0f);
    dl->AddText(ImVec2(x, text_y), selected ? colors::TextPrimary : colors::TextMuted, num.c_str());
    x += number_w + gap;
    const float duration_w = ImGui::CalcTextSize(duration.c_str()).x;
    if (x + duration_w > right) return clicked;
    dl->AddText(ImVec2(x, text_y), selected ? colors::TextPrimary : colors::TextBody, duration.c_str());
    x += duration_w + gap;
    if (!boss_hp.empty()) {
        const float boss_w = ImGui::CalcTextSize(boss_hp.c_str()).x;
        if (x + boss_w > right) return clicked;
        dl->AddText(ImVec2(x, text_y), colors::TextMuted, boss_hp.c_str());
        x += boss_w + gap;
    }
    if (e.partial) {
        const char* mark = ICON_WARNING;
        const float mark_w = ImGui::CalcTextSize(mark).x;
        if (x + mark_w > right) return clicked;
        dl->AddText(ImVec2(x, text_y), colors::WarningLight, mark);
        x += mark_w + gap;
    }
    const float clock_w = ImGui::CalcTextSize(clock.c_str()).x;
    if (x + clock_w <= right) dl->AddText(ImVec2(right - clock_w, text_y), colors::TextDim, clock.c_str());
    return clicked;
}

/// `text` cut to fit `width`, ending in "..." when cut.
std::string fit_text(const std::string& text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
    std::string cut = text;
    while (!cut.empty()) {
        cut.pop_back();
        while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80) cut.pop_back();
        if (ImGui::CalcTextSize((cut + "...").c_str()).x <= width) break;
    }
    return cut + "...";
}

/// A map visit's folding row. The tree node draws only its arrow. The visit's time keeps
/// the right edge, as the fights' times do below it: runs of one dungeon share a name,
/// and the time tells them apart. A name too long for the rest is cut; the tooltip
/// holds both.
bool visit_header(const std::string& map, const std::string& clock) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float right = pos.x + ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_Text, v4(colors::TextMuted));
    const bool open = ImGui::TreeNodeEx("##visit", ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::PopStyleColor();
    if (!ImGui::IsItemVisible()) return open;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line_h = ImGui::GetTextLineHeight();
    const float text_x = pos.x + ImGui::GetTreeNodeToLabelSpacing();
    const float text_y = ImGui::GetItemRectMin().y + (ImGui::GetItemRectSize().y - line_h) * 0.5f;
    const float clock_w = ImGui::CalcTextSize(clock.c_str()).x;
    const float name_w = right - clock_w - m(8.0f) - text_x;
    dl->AddText(ImVec2(right - clock_w, text_y), colors::TextDim, clock.c_str());
    if (name_w > 0.0f) dl->AddText(ImVec2(text_x, text_y), colors::TextMuted, fit_text(map, name_w).c_str());
    return open;
}

void clear_history_popup(size_t count, AppViewState& state, AppActions& out) {
    if (!ImGui::BeginPopupModal("##ConfirmClear", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        return;
    }
    icon_chip(ICON_WARNING, colors::Danger, metrics::ChipSizeLg);
    ImGui::SameLine(0.0f, m(10.0f));
    ImGui::BeginGroup();
    const std::string title = count == 1 ? std::string("Delete the saved fight?")
                                         : "Delete all " + std::to_string(count) + " saved fights?";
    ImGui::PushFont(bold_font());
    text_colored_u32(colors::TextPrimary, "%s", title.c_str());
    ImGui::PopFont();
    text_dim("Their files go too. The live fight stays.");
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0.0f, m(8.0f)));
    if (button(ICON_TRASH "  Delete", ButtonKind::Danger, ButtonSize::Medium)) {
        out.clear_history = true;
        state.selected_file.clear();
        state.selected_fight.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine(0.0f, m(8.0f));
    if (button("Cancel", ButtonKind::Secondary, ButtonSize::Small)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void rail(const AppFrameInput& in, AppViewState& state, AppActions& out, const std::vector<meter::PullGroup>& groups,
          float width) {
    const std::vector<meter::HistoryEntry> none;
    const auto& history = in.history != nullptr ? *in.history : none;
    begin_card("##Rail", ImVec2(width, fill_h(0.0f)));

    // Live first.
    const bool live = in.snapshot != nullptr && in.snapshot->fight.live;
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    if (ImGui::Selectable("Live##LiveRow", state.selected_file.empty(), ImGuiSelectableFlags_AllowOverlap,
                          ImVec2(0.0f, rail_row_height()))) {
        state.selected_file.clear();
        state.selected_fight.reset();
    }
    ImGui::PopStyleVar();
    ImGui::SameLine();
    const Badge live_badge = live ? Badge{"In combat", colors::SuccessLight} : Badge{"Idle", colors::TextDim};
    right_align(pill_width(live_badge.label));
    pill(live_badge.label, live_badge.color);

    // The saved fights, by visit, with the trash on the header's line.
    ImGui::Dummy(ImVec2(0.0f, m(6.0f)));
    const float icon_w = ImGui::GetFrameHeight();
    const float start_y = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(start_y + (icon_w - ImGui::GetTextLineHeight()) * 0.5f);
    text_dim("SAVED FIGHTS");
    ImGui::SameLine();
    right_align(icon_w);
    ImGui::SetCursorPosY(start_y);
    ImGui::BeginDisabled(history.empty());
    if (button(ICON_TRASH "##ClearHistory", ButtonKind::Danger, ButtonSize::Icon)) ImGui::OpenPopup("##ConfirmClear");
    ImGui::EndDisabled();
    if (!history.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("Delete the saved fights");
    ImGui::Dummy(ImVec2(0.0f, m(2.0f)));

    if (history.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        const bool dungeons_only = in.settings != nullptr && in.settings->meter.save_only_dungeons;
        text_colored_u32(colors::TextFaint, "Boss fights, and fights longer than %.0f s, are saved here%s.",
                         in.settings != nullptr ? in.settings->meter.min_saved_duration_s : 20.0,
                         dungeons_only ? ", in dungeons only" : "");
        ImGui::PopTextWrapPos();
    } else {
        ImGui::BeginChild("##PullList", ImVec2(0.0f, fill_h(0.0f)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground);
        // Sized to the widest number, so every duration starts in one column.
        size_t widest = 1;
        for (const auto& g : groups) widest = std::max(widest, g.pulls.size());
        const float number_w = ImGui::CalcTextSize(("#" + std::to_string(widest)).c_str()).x;
        for (size_t gi = 0; gi < groups.size(); ++gi) {
            const auto& g = groups[gi];
            const bool holds_selection = std::any_of(g.pulls.begin(), g.pulls.end(), [&](const meter::PullRef& p) {
                return history[p.index].file == state.selected_file;
            });
            ImGui::PushID(static_cast<int>(gi));
            // The newest visit, and the one holding the selection, start open; after
            // that the user's own folding wins.
            ImGui::SetNextItemOpen(gi == 0 || holds_selection, ImGuiCond_Once);
            const Micros started = g.visit_ts != 0 ? g.visit_ts : history[g.pulls.back().index].start_ts;
            const std::string map = meter::map_label(g.map_id);
            const std::string clock = common::local_clock(started);
            const bool open = visit_header(map, clock);
            if (ImGui::IsItemHovered()) {
                const size_t n = g.pulls.size();
                ImGui::SetTooltip("%s, from %s\n%zu %s", map.c_str(), clock.c_str(), n,
                                  n == 1 ? "fight saved" : "fights saved");
            }
            if (open) {
                for (const auto& p : g.pulls) {
                    const auto& e = history[p.index];
                    if (pull_row(e, p.number, e.file == state.selected_file, number_w)) {
                        state.selected_file = e.file;
                        out.load_file = e.file;
                    }
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    clear_history_popup(history.size(), state, out);
    end_card();
}

// ----------------------------------------------------------------- top bar ---

void top_bar(const Shown& shown, AppActions& out) {
    CardOptions opts{};
    opts.auto_height = true;
    begin_card("##TopBar", ImVec2(0.0f, 0.0f), opts);
    const meter::FightSummary& f = *shown.fight;
    std::string title = f.live ? "Live fight" : "Last fight";
    if (shown.entry != nullptr) title = meter::map_label(f.map_id) + "  -  Fight #" + std::to_string(shown.number);
    if (f.boss.present()) title += "  -  " + std::string(f.boss.is_boss ? "Boss " : "") + f.boss.label;
    ImGui::PushFont(bold_font());
    text_colored_u32(colors::TextPrimary, "%s", title.c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, m(2.0f)));

    // Label-over-value pairs that wrap to the next line rather than run off the card.
    const float row_y = ImGui::GetCursorPosY();
    const float stat_w = m(92.0f);
    float line_top = row_y;
    bool first = true;
    const auto stat = [&](const char* label, const std::string& value, uint32_t color, const char* tip) {
        if (!first && !same_line_if_room(stat_w, 20.0f)) line_top = ImGui::GetCursorPosY();
        first = false;
        ImGui::BeginGroup();
        ImGui::SetCursorPosY(line_top);
        text_dim("%s", label);
        ImGui::PushFont(bold_font());
        text_colored_u32(color, "%s", value.c_str());
        ImGui::PopFont();
        ImGui::EndGroup();
        if (tip != nullptr && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    };
    stat("DURATION", common::clock_duration(f.duration_s), colors::TextPrimary,
         "From the first hit to the last.");
    stat("PARTY DPS", format_rate(f.total_dps), colors::Dps, nullptr);
    stat("DAMAGE", format_amount(f.total_damage), colors::TextPrimary, nullptr);
    if (f.total_healing > 0) stat("HEALING", format_amount(f.total_healing), colors::Heal, nullptr);
    stat("PLAYERS", std::to_string(f.combatants.size()), colors::TextPrimary, nullptr);
    if (f.boss.present() && f.boss.killed) {
        stat("BOSS HP", "Killed", colors::Gold, nullptr);
    } else if (f.boss.present() && f.boss.hp_known) {
        const bool estimate = !f.boss.hp_exact;
        stat("BOSS HP", (estimate ? "~" : "") + format_percent(f.boss.hp_fraction()), colors::TextPrimary,
             estimate ? "An estimate (~): spawn HP less the damage the meter saw, until the server sends the "
                        "boss's HP."
                      : nullptr);
    }

    const Badge o = outcome(f.live, f.boss.killed, f.end_reason);
    const char* end_label = ICON_TIMER "  End fight";
    const bool can_end = f.live && shown.entry == nullptr;
    const float actions_w = pill_width(o.label) + (can_end ? m(10.0f) + button_width(end_label) : 0.0f);
    if (same_line_if_room(actions_w, metrics::Gutter)) {
        right_align(actions_w);
        ImGui::SetCursorPosY(line_top + m(3.0f));
    }
    const float actions_y = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(actions_y + (m(metrics::ButtonH) - pill_height()) * 0.5f);
    pill(o.label, o.color);
    if (!f.live && ImGui::IsItemHovered()) ImGui::SetTooltip("Ended by %s", reason_text(f.end_reason));
    if (can_end) {
        ImGui::SameLine(0.0f, m(10.0f));
        ImGui::SetCursorPosY(actions_y);
        if (button(end_label, ButtonKind::Danger)) out.end_fight = true;
    }

    if (f.quality.partial) {
        pill("Partial data", colors::WarningLight);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Much of the boss's HP went down with no hit the meter saw. On crowded world bosses "
                              "the server sends only some players' hits.");
        }
    } else if (!f.quality.clean()) {
        pill("The capture lost some packets", colors::WarningLight);
    }
    end_card();
}

// ---------------------------------------------------------------- settings ---

void overlay_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##set_overlay", ICON_MONITOR, "OVERLAY", colors::Accent);
    OverlaySettings& o = in.settings->overlay;
    setting_switch("Show the overlay", "Ctrl+Alt+H hides or shows it.", &o.visible, out, Applies::Overlay);
    setting_switch("Lock", "Click-through and fixed in place. Ctrl+Alt+L.", &o.locked, out, Applies::Overlay);
    setting_slider("Opacity", "Of its background; the text stays solid.", &o.opacity, 0.0f, 1.0f, "%.2f", out,
                   Applies::Overlay);
    setting_slider("Scale", "On top of the monitor's own scaling.", &o.scale, 0.6f, 2.5f, "%.2fx", out,
                   Applies::Overlay);
    begin_setting_row("Position", "Back to the top-left corner of the main monitor.");
    if (button(ICON_RESET "  Reset position", ButtonKind::Secondary, ButtonSize::Fit)) out.reset_overlay_position = true;
    end_setting_row();
    end_settings_card();
}

void visibility_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##set_visibility", ICON_EYE, "OVERLAY VISIBILITY", colors::Violet);
    OverlaySettings& o = in.settings->overlay;
    // Conditions apply only while locked, so the overlay can always be dragged back.
    if (!o.locked) ImGui::BeginDisabled();
    setting_switch("Only in combat", "Hidden outside a fight.", &o.show_in_combat_only, out);
    if (!o.show_in_combat_only) ImGui::BeginDisabled();
    setting_slider("Hide after combat", "How long it stays up once a fight ends, to read the result.",
                   &o.hide_after_combat_s, 0.0f, 60.0f, "%.0f s", out);
    if (!o.show_in_combat_only) ImGui::EndDisabled();
    setting_switch("Hide when the game is not in front", "Hidden while another program has the focus.",
                   &o.hide_when_game_unfocused, out);
    setting_switch("Only in dungeons", "Hidden in the open world, and on maps the meter can't name.",
                   &o.show_in_dungeons_only, out);
    if (!o.locked) ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0.0f, m(2.0f)));
    ImGui::PushTextWrapPos(0.0f);
    if (!o.locked) {
        text_colored_u32(colors::WarningLight, ICON_WARNING "  Lock the overlay for these to apply.");
    } else {
        text_dim("They apply while the overlay is locked; unlocked, it always shows.");
    }
    ImGui::PopTextWrapPos();
    end_settings_card();
}

void columns_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##set_columns", ICON_SLIDERS, "OVERLAY COLUMNS", colors::Accent);
    OverlaySettings& o = in.settings->overlay;
    setting_switch("Share", "Each player's share of the party's damage.", &o.show_share, out);
    setting_switch("Hits", "HIT: direct hits.", &o.show_hits, out);
    setting_switch("Crit", "CRI: critical hits.", &o.show_crit, out);
    setting_switch("Perfect", "PERF: perfect hits.", &o.show_perfect, out);
    setting_switch("Double", "DOUB: double hits.", &o.show_double, out);
    setting_switch("Back", "BACK: hits from behind.", &o.show_back, out);
    setting_switch("Front", "FRNT: hits from the front.", &o.show_front, out);
    setting_switch("Max hit", "The player's biggest hit.", &o.show_max_hit, out);
    setting_switch("Healing", "Each player's healing.", &o.show_heal, out);
    setting_switch("Class colours", "Bars in each player's class colour.", &o.class_bars, out);
    setting_slider("Rows", "Players shown, best first.", &o.max_rows, 1, 24, "%d", out);
    end_settings_card();
}

void tracking_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##set_tracking", ICON_TIMER, "FIGHT TRACKING", colors::Warning);
    meter::MeterSettings& s = in.settings->meter;
    setting_slider("Idle end", "No party hit for this long ends a fight.", &s.idle_timeout_s, 2.0, 120.0, "%.0f s",
                   out, Applies::Meter);
    setting_slider("Idle end with a boss up", "The same while a boss still stands: phases it can't be hit.",
                   &s.boss_idle_timeout_s, s.idle_timeout_s, 300.0, "%.0f s", out, Applies::Meter);
    setting_slider("Kill grace", "Hits this long after the boss dies still count.", &s.kill_grace_s, 0.0, 10.0,
                   "%.1f s", out, Applies::Meter);
    setting_slider("Shortest fight saved", "A fight with no boss is saved if it lasts this long.",
                   &s.min_saved_duration_s, 0.0, 600.0, "%.0f s", out, Applies::Meter);
    setting_switch("Save only dungeon fights",
                   "Keeps the open world out. A dungeon shown as \"Map <number>\" is not one the meter knows.",
                   &s.save_only_dungeons, out, Applies::Meter);

    begin_setting_row("Boss HP", "Max HP that makes an enemy a boss. A guess until captures calibrate it.");
    uint64_t hp = s.boss_min_max_hp;
    if (ImGui::InputScalar("##boss_hp", ImGuiDataType_U64, &hp, nullptr, nullptr, "%llu")) {
        s.boss_min_max_hp = std::max<uint64_t>(hp, 1);
        out.settings_changed = true;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        out.save_settings = true;
        out.meter_changed = true;
    }
    end_setting_row();

    setting_slider("Partial data under", "A killed boss that lost less than this share of its HP with hits seen.",
                   &s.partial_damage_ratio, 0.0, 1.0, "%.2f", out, Applies::Meter);
    setting_switch("Wipe ends the fight", "When every party member is dead. Off until captures show revives.",
                   &s.wipe_detection, out, Applies::Meter);
    end_settings_card();
}

void history_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##set_history", ICON_HISTORY, "SAVED FIGHTS", colors::Success);
    setting_slider("Fights kept", "The oldest are deleted first.", &in.settings->history.max_fights, 10, 5000, "%d", out,
                   Applies::Meter);
    if (button(ICON_FOLDER "  Open folder")) out.open = OpenTarget::History;
    end_settings_card();
}

void settings_tab(const AppFrameInput& in, AppActions& out) {
    if (in.settings == nullptr) return;
    const SettingsSection sections[] = {
        {[&] { overlay_card(in, out); }},
        {[&] { tracking_card(in, out); }},
        {[&] { visibility_card(in, out); }},
        {[&] { history_card(in, out); }},
        {[&] { columns_card(in, out); }},
    };
    render_settings_grid(sections, std::size(sections));
}

// -------------------------------------------------------------------- page ---

/// The rail on the left, the fight's top bar and a tab's table on the right.
template <typename Body>
void pull_view(const AppFrameInput& in, AppViewState& state, AppActions& out, const std::vector<meter::PullGroup>& groups,
               const Shown& shown, const char* id, Body body) {
    const float avail = ImGui::GetContentRegionAvail().x;
    const float rail_w = avail < m(900.0f) ? m(200.0f) : std::clamp(avail * 0.2f, m(220.0f), m(metrics::RailW));
    rail(in, state, out, groups, rail_w);
    ImGui::SameLine(0.0f, m(metrics::Gutter));
    ImGui::BeginChild(id, ImVec2(0.0f, fill_h(0.0f)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    if (shown.fight == nullptr) {
        if (!state.selected_file.empty()) {
            empty_state(ICON_WARNING, "That fight could not be read", "Its file is missing or damaged.");
        } else {
            empty_state(ICON_SWORDS, "No fight yet", "Hit something in game: the meter starts on the party's first hit.");
        }
    } else {
        top_bar(shown, out);
        ImGui::Dummy(ImVec2(0.0f, m(4.0f)));
        body(*shown.fight);
    }
    ImGui::EndChild();
}

} // namespace

void render_meter_page(const AppFrameInput& in, AppViewState& state, AppActions& out) {
    page_header(ICON_SWORDS, "Combat Meter", "Every fight, live and saved, for the whole party");

    const std::vector<meter::HistoryEntry> none;
    const auto& history = in.history != nullptr ? *in.history : none;
    const std::vector<meter::PullGroup> groups = meter::group_by_visit(history);

    // A selection that left the history (cleared, or pruned) falls back to live.
    Shown shown;
    if (!state.selected_file.empty()) {
        for (const auto& g : groups) {
            for (const auto& p : g.pulls) {
                if (history[p.index].file == state.selected_file) {
                    shown.entry = &history[p.index];
                    shown.number = p.number;
                }
            }
        }
        if (shown.entry == nullptr) {
            state.selected_file.clear();
            state.selected_fight.reset();
        }
    }
    if (shown.entry != nullptr) {
        if (state.selected_fight) shown.fight = &*state.selected_fight;
    } else if (in.snapshot != nullptr && in.snapshot->fight.id != 0) {
        shown.fight = &in.snapshot->fight;
    }

    const PageTab tabs[] = {
        {ICON_SWORDS, "Damage",
         [&] { pull_view(in, state, out, groups, shown, "##DamagePane", [&](const auto& f) { damage_tab(f, state); }); }},
        {ICON_HEART, "Healing",
         [&] { pull_view(in, state, out, groups, shown, "##HealPane", [&](const auto& f) { healing_tab(f, state); }); }},
        {ICON_SHIELD, "Damage Taken",
         [&] { pull_view(in, state, out, groups, shown, "##TakenPane", [&](const auto& f) { taken_tab(f, state); }); }},
        {ICON_SLIDERS, "Settings", [&] { settings_tab(in, out); }},
    };
    render_tabs("##MeterTabs", tabs, std::size(tabs), state.open_meter_tab);
    state.open_meter_tab = -1;
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
