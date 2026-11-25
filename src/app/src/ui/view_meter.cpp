#include "app/ui/views.hpp"

#include "app/display_format.hpp"
#include "app/game_icons.hpp"
#include "app/history_list.hpp"
#include "app/ui/icons.hpp"
#include "app/ui/meter_tabs.hpp"
#include "app/ui/page.hpp"
#include "app/ui/table.hpp"
#include "app/ui/widgets.hpp"
#include "common/time_format.hpp"
#include "game/map_names.hpp"
#include "game/npc_names.hpp"
#include "game/skill_names.hpp"
#include "meter/fight_detail.hpp"
#include "meter/pull_grouping.hpp"

#ifdef HAVE_IMGUI
#include "imgui_internal.h"
#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <vector>
#endif

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

namespace {

/// " · ", between two facts on one line. Escaped: the source stays ASCII.
const char* const kDot = " \xC2\xB7 ";

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

} // namespace

// ------------------------------------------------------------------ tables ---
// The row and breakdown helpers below are shared with the tabs in files of their
// own (meter_tabs.hpp).

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

namespace {

const char* const kTipHits = "HIT: direct hits. DoT ticks are counted apart and carry no crit or direction.";
const char* const kTipCrit = "CRI: critical hits, of the direct hits.";
const char* const kTipPerfect = "PERF: perfect hits, of the direct hits.";
const char* const kTipDouble = "DOUB: double hits, of the direct hits.";
const char* const kTipBack = "BACK: hits from behind the target, of the direct hits.";
const char* const kTipFront = "FRNT: hits from the target's front, of the direct hits.";
const char* const kTipMulti = "MULT: multi-hits, of the direct hits.";
const char* const kTipBlock = "BLOC: hits the target blocked, of the direct hits.";

} // namespace

namespace {

/// A table of `rows` whole: the header and the rows, with a little slack so a table
/// that fits never scrolls.
float fit_table_h(size_t rows) {
    const float row_h = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
    return static_cast<float>(rows + 1) * row_h + m(6.0f);
}

} // namespace

float table_height(size_t rows, bool breakdown_open) {
    return std::min(fit_table_h(rows), breakdown_open ? fill_h(0.0f) * 0.5f : fill_h(0.0f));
}

float begin_fit_card(const char* id) {
    const float room = fill_h(0.0f);
    CardOptions opts{};
    opts.auto_height = true;
    begin_card(id, ImVec2(0.0f, 0.0f), opts);
    return room;
}

float fit_card_table_h(size_t rows, float room) {
    // What the card has left under what it holds already, less its bottom padding.
    const float left = room - ImGui::GetCursorPosY() - m(metrics::CardPad);
    return std::min(fit_table_h(rows), std::max(left, fit_table_h(2)));
}

void select_hint(const char* what) {
    if (ImGui::GetContentRegionAvail().y < m(120.0f)) return;
    empty_state(ICON_CROSSHAIR, "Select a player", what);
}

const meter::CombatantTotals* find_row(const meter::FightSummary& f, meter::EntityId id) {
    if (id == 0) return nullptr;
    for (const auto& c : f.combatants) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

/// The card a breakdown sits in, titled with the player, with a Close button.
float begin_breakdown(const char* id, const char* title, const meter::CombatantTotals& c, meter::EntityId& selected,
                      size_t rows) {
    ImGui::Dummy(ImVec2(0.0f, m(4.0f)));
    // With no rows it holds an empty state, centred in the pane's rest.
    float room = 0.0f;
    if (rows > 0) {
        room = begin_fit_card(id);
    } else {
        begin_card(id, ImVec2(0.0f, fill_h(0.0f)));
    }
    char header[160];
    std::snprintf(header, sizeof(header), "%s - %s", title, c.name.c_str());
    const char* close_label = ICON_CIRCLE_X "  Close";
    begin_section_header(ICON_CROSSHAIR, header, button_width(close_label, ButtonSize::Small), row_color_u32(c));
    const bool close = button(close_label, ButtonKind::Secondary, ButtonSize::Small);
    end_section_header();
    if (close) {
        selected = 0;
        end_card();
        return 0.0f;
    }
    return rows > 0 ? fit_card_table_h(rows, room) : fill_h(0.0f);
}

namespace {

// ------------------------------------------------------------------ damage ---

void damage_breakdown(const meter::CombatantTotals& c, meter::EntityId& selected) {
    const float table_h = begin_breakdown("##DamageBreakdown", "SKILLS", c, selected, c.skills.size());
    if (table_h <= 0.0f) return;
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
        {"MULT", 50.0f, kTipMulti, 13},
        {"BLOC", 50.0f, kTipBlock, 14},
        {"DoT", 80.0f, "Damage from the skill's ticks, and how many.", 12},
    };
    if (begin_table("##SkillTable", cols, static_cast<int>(std::size(cols)), table_h)) {
        const uint64_t top = std::max<uint64_t>(c.skills.front().damage, 1);
        for (const auto& s : c.skills) {
            ImGui::TableNextRow();
            row_progress_bar(static_cast<float>(static_cast<double>(s.damage) / static_cast<double>(top)),
                             row_color_u32(c));
            const uint64_t direct = s.damage - std::min(s.damage, s.tick_damage);
            skill_cell(0, s.code);
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
            cell(12, colors::TextMuted, rate(s.hits, &meter::HitCounts::multi));
            cell(13, colors::TextMuted, rate(s.hits, &meter::HitCounts::blocks));
            cell(14, colors::TextDim, s.ticks == 0 ? "-" : format_amount(s.tick_damage) + " (" + number(s.ticks) + ")");
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
        {"#", 24.0f, nullptr, 13},
        {"Class", 64.0f, "From the skills the player used.", 8},
        {"Player", 0.0f, "Select a player for their skills.", 0},
        {"DPS", 60.0f, "Damage per second, from the fight's first hit to its last.", 1},
        {"Damage", 64.0f, "Direct hits and DoT ticks.", 2},
        {"Share", 52.0f, "Of the party's damage.", 3},
        {"Active", 54.0f,
         "Time spent hitting: the gaps of 3 s or less between the player's own direct hits, of the fight. "
         "Summons and DoTs do not count, so healers and summoners read lower.",
         6},
        {"HIT", 40.0f, kTipHits, 7},
        {"CRI", 50.0f, kTipCrit, 4},
        {"PERF", 50.0f, kTipPerfect, 9},
        {"DOUB", 50.0f, kTipDouble, 10},
        {"BACK", 50.0f, kTipBack, 5},
        {"FRNT", 50.0f, kTipFront, 11},
        {"MULT", 50.0f, kTipMulti, 14},
        {"BLOC", 50.0f, kTipBlock, 15},
        {"Max", 58.0f, "The player's biggest hit.", 12},
    };
    if (begin_table("##DamageTable", cols, static_cast<int>(std::size(cols)),
                    table_height(rows.size(), open != nullptr))) {
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
            // A record saved before the meter kept active time has none to show.
            cell(6, colors::TextMuted, f.detail != nullptr ? format_percent(c->active_share(f.duration_s)) : "-");
            if (f.detail != nullptr && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s of %s spent hitting", common::clock_duration(c->active_s).c_str(),
                                  common::clock_duration(f.duration_s).c_str());
            }
            cell(7, colors::TextMuted, number(c->hits.hits));
            cell(8, colors::TextMuted, rate(c->hits, &meter::HitCounts::crits));
            cell(9, colors::TextMuted, rate(c->hits, &meter::HitCounts::perfect));
            cell(10, colors::TextMuted, rate(c->hits, &meter::HitCounts::doubles));
            cell(11, colors::TextMuted, rate(c->hits, &meter::HitCounts::back));
            cell(12, colors::TextMuted, rate(c->hits, &meter::HitCounts::front));
            cell(13, colors::TextMuted, rate(c->hits, &meter::HitCounts::multi));
            cell(14, colors::TextMuted, rate(c->hits, &meter::HitCounts::blocks));
            cell(15, colors::TextMuted, format_amount(c->max_hit));
        }
        ImGui::EndTable();
    }
    if (open == nullptr) {
        select_hint("Click a row above for that player's skills.");
        return;
    }
    damage_breakdown(*open, state.damage_player);
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
        {"Class", 64.0f, nullptr, 5},
        {"Player", 0.0f, "Select a player for their heals by skill.", 0},
        {"HPS", 60.0f, "Healing per second, over the fight's length.", 1},
        {"Healing", 64.0f, "All of it, as the game's own window counts it: no overheal split.", 2},
        {"Share", 52.0f, "Of the party's healing.", 3},
        {"Heals", 50.0f, "Heal records and heal ticks.", 4},
    };
    if (begin_table("##HealTable", cols, static_cast<int>(std::size(cols)),
                    table_height(rows.size(), open != nullptr))) {
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
    if (open == nullptr) {
        select_hint("Click a row above for that player's heals, skill by skill.");
        return;
    }
    const float table_h =
        begin_breakdown("##HealBreakdown", "HEALS", *open, state.heal_player, open->heal_skills.size());
    if (table_h <= 0.0f) return;
    const Column skill_cols[] = {
        {"Skill", 0.0f, nullptr, 0},
        {"Healing", 64.0f, nullptr, 1},
        {"Share", 52.0f, "Of the player's healing.", 2},
        {"Heals", 50.0f, nullptr, 3},
        {"Max", 58.0f, "The biggest heal.", 4},
    };
    if (begin_table("##HealSkills", skill_cols, static_cast<int>(std::size(skill_cols)), table_h)) {
        const uint64_t top = open->heal_skills.empty() ? 1 : std::max<uint64_t>(open->heal_skills.front().amount, 1);
        for (const auto& s : open->heal_skills) {
            ImGui::TableNextRow();
            row_progress_bar(static_cast<float>(static_cast<double>(s.amount) / static_cast<double>(top)),
                             colors::Success);
            skill_cell(0, s.code);
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
        {"Class", 64.0f, nullptr, 6},
        {"Player", 0.0f, "Select a player for what hit them.", 0},
        {"Taken", 64.0f, "Enemy hits and ticks that landed on the player.", 1},
        {"HIT", 40.0f, "Direct hits taken.", 2},
        {"CRI", 50.0f, "Critical hits, of the hits taken.", 3},
        {"PERF", 50.0f, "Perfect hits, of the hits taken.", 4},
        {"FRNT", 50.0f, "Hits to the player's front, of the hits taken.", 5},
    };
    if (begin_table("##TakenTable", cols, static_cast<int>(std::size(cols)),
                    table_height(rows.size(), open != nullptr))) {
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
    if (open == nullptr) {
        select_hint("Click a row above for what hit that player.");
        return;
    }
    const float table_h =
        begin_breakdown("##TakenBreakdown", "HIT BY", *open, state.taken_player, open->taken_by.size());
    if (table_h <= 0.0f) return;
    const Column skill_cols[] = {
        {"Skill", 0.0f, "The enemy's skill. Enemy skills have no names in the table: their codes show.", 0},
        {"Source", 100.0f, nullptr, 3},
        {"Damage", 64.0f, nullptr, 1},
        {"Share", 52.0f, "Of the player's damage taken.", 2},
        {"Hits", 40.0f, nullptr, 4},
        {"Max", 58.0f, "The biggest hit.", 5},
    };
    if (begin_table("##TakenSkills", skill_cols, static_cast<int>(std::size(skill_cols)), table_h)) {
        const uint64_t top = open->taken_by.empty() ? 1 : std::max<uint64_t>(open->taken_by.front().amount, 1);
        for (const auto& s : open->taken_by) {
            ImGui::TableNextRow();
            row_progress_bar(static_cast<float>(static_cast<double>(s.amount) / static_cast<double>(top)),
                             colors::Danger);
            cell(0, colors::TextBody, game::skill_label(s.code));
            cell(1, colors::TextMuted, s.source_npc != 0 ? game::npc_label(s.source_npc) : std::string("-"));
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

/// A saved fight's row: two lines of text.
float pull_row_height() { return ImGui::GetTextLineHeight() * 2.0f + m(10.0f); }

/// How a saved fight ended, in words: the list's rows say it, not only their dots.
struct PullOutcome {
    std::string text;
    uint32_t color;
};

PullOutcome pull_outcome(const meter::HistoryEntry& e) {
    // Whole percents: the row has little room, and the tooltip has the decimal.
    std::string left;
    if (e.boss_hp_known) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.0f%%", e.boss_hp_left * 100.0);
        left = buf;
    }
    if (e.killed) return {"Killed", colors::Gold};
    if (e.end_reason == meter::EndReason::Wipe) return {left.empty() ? "Wipe" : "Wipe " + left, colors::DangerLight};
    if (!left.empty()) return {left + " left", colors::TextMuted};
    return {"Ended", colors::TextDim};
}

/// The last line of a saved fight's tooltip: what else a row does.
const char* const kRowHint = "Right-click to keep or delete it. Ctrl- or Shift-click selects several.";

/// One saved fight, on two lines: what was fought and how it ended, then its number
/// in the visit, its length and when it started, with a lock before the time when
/// it is kept. A picked fight is tinted, with a check where its outcome's dot was.
/// The tooltip carries all of it. The caller pushes the row's id.
bool pull_row(const meter::HistoryEntry& e, int number_in_visit, bool selected, bool picked) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float right = pos.x + ImGui::GetContentRegionAvail().x;
    const float row_h = pull_row_height();
    const bool clicked = ImGui::Selectable("##pull", selected, ImGuiSelectableFlags_None, ImVec2(0.0f, row_h));
    // A long visit lists every fight; only the rows on screen are worth formatting.
    if (!ImGui::IsItemVisible()) return clicked;

    const Badge o = outcome(false, e.killed, e.end_reason);
    const PullOutcome ended = pull_outcome(e);
    const std::string num = "#" + std::to_string(number_in_visit);
    const std::string duration = common::clock_duration(e.duration_s);
    const std::string clock = common::local_clock(e.start_ts);
    if (ImGui::IsItemHovered()) {
        std::string tip = "Fight " + num + " - " + o.label;
        if (!e.boss_label.empty()) tip += "\n" + std::string(e.is_boss ? "Boss " : "") + e.boss_label;
        if (e.boss_hp_known) tip += " at " + format_percent(e.boss_hp_left);
        tip += "\nStarted " + clock + ", " + duration + ", " + format_rate(e.total_dps) + " party DPS";
        tip += "\nEnded by " + std::string(reason_text(e.end_reason));
        if (e.partial) tip += "\nPartial data: the server sent only some players' hits";
        if (e.kept) tip += "\nKept: pruning, Clear and deleting its run leave it";
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tip.c_str());
        text_dim("%s", kRowHint);
        ImGui::EndTooltip();
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (picked) {
        dl->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), colors::with_alpha(colors::Accent, 0.14f),
                          m(4.0f));
    }
    const float line_h = ImGui::GetTextLineHeight();
    const float top_y = pos.y + m(4.0f);
    const float bottom_y = top_y + line_h + m(2.0f);
    const float gap = m(8.0f);
    const float dot_r = m(3.0f);
    const float pad = m(6.0f);
    if (picked) {
        const ImVec2 check = ImGui::CalcTextSize(ICON_CHECK);
        dl->AddText(ImVec2(pos.x + pad + dot_r - check.x * 0.5f, top_y + (line_h - check.y) * 0.5f),
                    colors::AccentHover, ICON_CHECK);
    } else {
        dl->AddCircleFilled(ImVec2(pos.x + pad + dot_r, top_y + line_h * 0.5f), dot_r, o.color);
    }
    const float text_x = pos.x + pad + dot_r * 2.0f + m(7.0f);

    // Line one: the outcome keeps the right edge, the name takes what is left.
    const float ended_w = ImGui::CalcTextSize(ended.text.c_str()).x;
    const float ended_x = right - pad - ended_w;
    const bool ended_fits = ended_x > text_x + m(40.0f);
    if (ended_fits) dl->AddText(ImVec2(ended_x, top_y), ended.color, ended.text.c_str());
    const std::string name = e.boss_label.empty() ? "Fight " + num : e.boss_label;
    const float name_w = (ended_fits ? ended_x - gap : right - pad) - text_x;
    if (name_w > 0.0f) {
        dl->AddText(ImVec2(text_x, top_y), selected ? colors::TextPrimary : colors::TextBody,
                    fit_text(name, name_w).c_str());
    }

    // Line two, dimmer: which pull, how long, when, and a lock when it is kept.
    std::string detail = num + kDot + duration;
    if (e.partial) detail += std::string(kDot) + "partial";
    const float clock_w = ImGui::CalcTextSize(clock.c_str()).x;
    const float clock_x = right - pad - clock_w;
    const float lock_w = e.kept ? ImGui::CalcTextSize(ICON_LOCK).x + m(4.0f) : 0.0f;
    const bool clock_fits = clock_x - lock_w > text_x + m(40.0f);
    if (clock_fits) {
        dl->AddText(ImVec2(clock_x, bottom_y), colors::TextDim, clock.c_str());
        if (e.kept) dl->AddText(ImVec2(clock_x - lock_w, bottom_y), colors::AccentHover, ICON_LOCK);
    }
    const float detail_w = (clock_fits ? clock_x - lock_w - gap : right - pad) - text_x;
    if (detail_w > 0.0f) {
        dl->AddText(ImVec2(text_x, bottom_y), e.partial ? colors::WarningLight : colors::TextDim,
                    fit_text(detail, detail_w).c_str());
    }
    return clicked;
}

/// A map visit's folding row, two lines tall as its fights' rows are: the map's name
/// across the row, then its tier and how many fights it holds, with the time the visit
/// began at the right edge, where the fights show theirs below it (runs of one dungeon
/// share a name, and the time tells them apart). The tree node draws only its arrow,
/// between the two lines. A name too long for the row is cut; the tooltip holds it.
bool visit_header(const RunTitle& title, const std::string& clock) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float right = pos.x + ImGui::GetContentRegionAvail().x;
    // The node's frame padding makes it the fights' rows' height.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x,
                                                           (pull_row_height() - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(colors::TextMuted));
    const bool open =
        ImGui::TreeNodeEx("##visit", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!ImGui::IsItemVisible()) return open;

    // Laid out as a fight's row is (pull_row).
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line_h = ImGui::GetTextLineHeight();
    const float text_x = pos.x + ImGui::GetTreeNodeToLabelSpacing();
    const float top_y = ImGui::GetItemRectMin().y + m(4.0f);
    const float bottom_y = top_y + line_h + m(2.0f);
    if (right - text_x > 0.0f) {
        dl->AddText(ImVec2(text_x, top_y), colors::TextMuted, fit_text(title.name, right - text_x).c_str());
    }
    // Line two: the time keeps the right edge, the tier and the count take the rest.
    const float clock_w = ImGui::CalcTextSize(clock.c_str()).x;
    dl->AddText(ImVec2(right - clock_w, bottom_y), colors::TextDim, clock.c_str());
    const float detail_w = right - clock_w - m(8.0f) - text_x;
    if (detail_w > 0.0f) {
        dl->AddText(ImVec2(text_x, bottom_y), colors::TextFaint, fit_text(title.detail, detail_w).c_str());
    }
    return open;
}

/// Puts a delete up for its confirmation. The list opens the dialog on its next
/// pass: a menu or the top bar sits in another id scope than the dialog.
void ask_delete(AppViewState& state, DeletePrompt prompt) {
    if (prompt.files.empty() && !prompt.everything) return;
    state.pending_delete = std::move(prompt);
    state.open_delete_prompt = true;
}

/// The dialog every delete waits in, Clear's too. Enter deletes, Esc cancels.
void delete_dialog(const std::vector<meter::HistoryEntry>& history, AppViewState& state, AppActions& out) {
    if (state.open_delete_prompt) {
        state.open_delete_prompt = false;
        ImGui::OpenPopup("##ConfirmDelete");
    }
    if (!ImGui::BeginPopupModal("##ConfirmDelete", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        return;
    }
    if (!state.pending_delete) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const DeletePrompt& prompt = *state.pending_delete;
    icon_chip(ICON_WARNING, colors::Danger, metrics::ChipSizeLg);
    ImGui::SameLine(0.0f, m(10.0f));
    ImGui::BeginGroup();
    ImGui::PushFont(bold_font());
    text_colored_u32(colors::TextPrimary, "%s", prompt.title.c_str());
    ImGui::PopFont();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + m(340.0f));
    text_dim("%s", prompt.body.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0.0f, m(8.0f)));
    bool confirm = button(ICON_TRASH "  Delete", ButtonKind::Danger, ButtonSize::Medium);
    ImGui::SameLine(0.0f, m(8.0f));
    bool cancel = button("Cancel", ButtonKind::Secondary, ButtonSize::Small);
    confirm = confirm || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    cancel = cancel || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (confirm) {
        // The fight on view goes back to live now, not once the engine is done.
        bool viewed_goes = false;
        if (!state.selected_file.empty()) {
            if (prompt.everything) {
                const auto it = std::find_if(history.begin(), history.end(), [&](const meter::HistoryEntry& e) {
                    return e.file == state.selected_file;
                });
                viewed_goes = it == history.end() || !it->kept;
            } else {
                viewed_goes =
                    std::find(prompt.files.begin(), prompt.files.end(), state.selected_file) != prompt.files.end();
            }
        }
        if (prompt.everything) {
            out.clear_history = true;
        } else {
            out.delete_files = prompt.files;
        }
        if (viewed_goes) {
            state.selected_file.clear();
            state.selected_fight.reset();
        }
        state.picks.marked.clear();
    }
    if (confirm || cancel) {
        state.pending_delete.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

/// A saved fight's right-click menu: keep it or stop, delete it. On a picked row it
/// acts on every pick.
void fight_menu(const std::vector<meter::HistoryEntry>& history, const meter::HistoryEntry& e, AppViewState& state,
                AppActions& out) {
    if (!begin_context_menu("##FightMenu")) return;
    const bool on_picks = state.picks.marked.size() > 1 && state.picks.marked.count(e.file) != 0;
    const std::set<std::string> files = on_picks ? state.picks.marked : std::set<std::string>{e.file};
    size_t count = 0;
    size_t kept = 0;
    for (const auto& h : history) {
        if (files.count(h.file) == 0) continue;
        ++count;
        if (h.kept) ++kept;
    }
    const std::string these = "these " + std::to_string(count) + " fights";
    if (kept < count && menu_item(ICON_LOCK, on_picks ? "Keep " + these : std::string("Keep this fight"))) {
        out.keep_files.assign(files.begin(), files.end());
    }
    if (kept > 0 && menu_item(ICON_UNLOCK, on_picks ? "Stop keeping " + these : std::string("Stop keeping it"))) {
        out.unkeep_files.assign(files.begin(), files.end());
    }
    ImGui::Separator();
    if (menu_item(ICON_TRASH, on_picks ? "Delete " + these + "..." : std::string("Delete this fight..."), true)) {
        ask_delete(state, delete_picked(history, files));
    }
    end_context_menu();
}

/// A run's right-click menu, on its header: keep its fights or stop, delete those
/// not kept. `where` names the run in the dialog.
void run_menu(const std::vector<meter::HistoryEntry>& history, const meter::PullGroup& g, const std::string& where,
              AppViewState& state, AppActions& out) {
    if (!begin_context_menu("##RunMenu")) return;
    std::vector<std::string> files;
    size_t kept = 0;
    for (const auto& p : g.pulls) {
        files.push_back(history[p.index].file);
        if (history[p.index].kept) ++kept;
    }
    if (kept < files.size() && menu_item(ICON_LOCK, "Keep this run")) out.keep_files = files;
    if (kept > 0 && menu_item(ICON_UNLOCK, "Stop keeping this run")) out.unkeep_files = files;
    ImGui::Separator();
    const DeletePrompt prompt = delete_run(history, g, where);
    if (menu_item(ICON_TRASH, "Delete this run...", true, !prompt.files.empty())) ask_delete(state, prompt);
    if (prompt.files.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", files.size() == 1 ? "Its fight is kept" : "Every fight in it is kept");
    }
    end_context_menu();
}

/// A day's caption over the runs that began on it, in capitals like the list's own.
void day_header(const std::string& label, bool first) {
    if (!first) ImGui::Dummy(ImVec2(0.0f, m(2.0f)));
    std::string caps = label;
    for (char& c : caps) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    text_dim("%s", caps.c_str());
}

/// The list's caption and its buttons, on one line: Clear, or, while fights are
/// selected, their count, Delete and a button that clears the selection. Spelled out while the rail has room
/// beside the caption; icons alone, with their tooltips, on a narrow one.
void caption_row(const std::vector<meter::HistoryEntry>& history, AppViewState& state) {
    const size_t picked = state.picks.marked.size();
    const size_t kept = kept_count(history);
    const size_t unkept = history.size() - kept;
    const std::string caption = picked > 0 ? std::to_string(picked) + " SELECTED" : std::string("SAVED FIGHTS");
    const char* main_text = picked > 0 ? ICON_TRASH "  Delete##DeletePicked" : ICON_TRASH "  Clear##ClearHistory";
    const char* main_icon = picked > 0 ? ICON_TRASH "##DeletePicked" : ICON_TRASH "##ClearHistory";
    // Clear has nothing to do while every fight is kept.
    const bool has_main = picked > 0 || unkept > 0;
    const float icon_w = ImGui::GetFrameHeight();
    const float gap = m(6.0f);
    const float unpick_w = picked > 0 ? gap + icon_w : 0.0f;
    const bool spelled = ImGui::CalcTextSize(caption.c_str()).x + m(10.0f) + button_width(main_text, ButtonSize::Fit) +
                             unpick_w <=
                         ImGui::GetContentRegionAvail().x;
    const float main_w = spelled ? button_width(main_text, ButtonSize::Fit) : icon_w;
    const float row_h = spelled ? m(metrics::ButtonH) : icon_w;
    const float start_y = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(start_y + (row_h - ImGui::GetTextLineHeight()) * 0.5f);
    text_colored_u32(picked > 0 ? colors::AccentHover : colors::TextDim, "%s", caption.c_str());
    if (!has_main) {
        ImGui::SetCursorPosY(start_y + row_h);
        return;
    }
    ImGui::SameLine();
    right_align(main_w + unpick_w);
    ImGui::SetCursorPosY(start_y);
    const bool pressed = spelled ? button(main_text, ButtonKind::Danger, ButtonSize::Fit)
                                 : button(main_icon, ButtonKind::Danger, ButtonSize::Icon);
    if (ImGui::IsItemHovered()) {
        std::string tip;
        if (picked > 0) {
            tip = picked == 1 ? std::string("Delete the selected fight (Del)")
                              : "Delete the " + std::to_string(picked) + " selected fights (Del)";
        } else if (kept == 0) {
            tip = unkept == 1 ? std::string("Delete the saved fight")
                              : "Delete all " + std::to_string(unkept) + " saved fights";
        } else {
            tip = unkept == 1 ? std::string("Delete the fight you don't keep")
                              : "Delete the " + std::to_string(unkept) + " fights you don't keep";
        }
        ImGui::SetTooltip("%s", tip.c_str());
    }
    if (pressed) ask_delete(state, picked > 0 ? delete_picked(history, state.picks.marked) : delete_all(history));
    if (picked > 0) {
        ImGui::SameLine(0.0f, gap);
        ImGui::SetCursorPosY(start_y + (row_h - icon_w) * 0.5f);
        if (button(ICON_CIRCLE_X "##PickNone", ButtonKind::Secondary, ButtonSize::Icon)) state.picks.marked.clear();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear the selection (Esc)");
    }
}

/// The list's keys, while the rail has the focus: Delete asks about the picks, or
/// the fight on view; Esc drops the picks; Ctrl+A picks every fight.
void list_keys(const std::vector<meter::HistoryEntry>& history, AppViewState& state) {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || ImGui::GetIO().WantTextInput) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (!state.picks.marked.empty()) {
            ask_delete(state, delete_picked(history, state.picks.marked));
        } else if (!state.selected_file.empty()) {
            ask_delete(state, delete_picked(history, {state.selected_file}));
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) state.picks.marked.clear();
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) select_all(state.picks, history);
}

void rail(const AppFrameInput& in, AppViewState& state, AppActions& out, const std::vector<meter::PullGroup>& groups,
          float width) {
    const std::vector<meter::HistoryEntry> none;
    const auto& history = in.history != nullptr ? *in.history : none;
    begin_card("##Rail", ImVec2(width, fill_h(0.0f)));

    // Live first: a row that looks like the button it is.
    const bool live = in.snapshot != nullptr && in.snapshot->fight.live;
    const bool on_live = state.selected_file.empty();
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float row_w = ImGui::GetContentRegionAvail().x;
        const float row_h = m(metrics::ButtonH + 4.0f);
        if (ImGui::InvisibleButton("##LiveRow", ImVec2(row_w, row_h))) {
            state.selected_file.clear();
            state.selected_fight.reset();
        }
        const bool hovered = ImGui::IsItemHovered();
        if (hovered && !on_live) ImGui::SetTooltip("Back to the fight going on now");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pos_max(pos.x + row_w, pos.y + row_h);
        const float rounding = m(6.0f);
        if (on_live) {
            dl->AddRectFilled(pos, pos_max, colors::with_alpha(colors::Accent, 0.18f), rounding);
            dl->AddRect(pos, pos_max, colors::with_alpha(colors::Accent, 0.55f), rounding);
        } else {
            dl->AddRectFilled(pos, pos_max, hovered ? colors::SurfaceRaised : colors::SurfaceLow, rounding);
            dl->AddRect(pos, pos_max, colors::BorderStrong, rounding);
        }
        const float text_y = pos.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f;
        const uint32_t ink = on_live ? colors::TextPrimary : colors::TextBody;
        dl->AddText(ImVec2(pos.x + m(10.0f), text_y), on_live ? colors::AccentHover : colors::TextMuted, ICON_ACTIVITY);
        dl->AddText(ImVec2(pos.x + m(34.0f), text_y), ink, on_live ? "Live fight" : "Back to live");
        const Badge live_badge = live ? Badge{"In combat", colors::SuccessLight} : Badge{"Idle", colors::TextDim};
        const float badge_w = pill_width(live_badge.label);
        if (row_w > m(34.0f) + ImGui::CalcTextSize("Back to live").x + badge_w + m(16.0f)) {
            const ImVec2 after = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(pos_max.x - badge_w - m(8.0f), pos.y + (row_h - pill_height()) * 0.5f));
            pill(live_badge.label, live_badge.color);
            ImGui::SetCursorScreenPos(after);
        }
    }

    // The saved fights, by day and visit, under their caption's line.
    ImGui::Dummy(ImVec2(0.0f, m(8.0f)));
    caption_row(history, state);
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
        std::optional<common::LocalDay> today;
        if (in.now != 0) today = common::local_day(in.now);
        std::optional<int64_t> last_day;
        std::string day_text;
        for (size_t gi = 0; gi < groups.size(); ++gi) {
            const auto& g = groups[gi];
            const Micros started = g.visit_ts != 0 ? g.visit_ts : history[g.pulls.back().index].start_ts;
            // Each day the runs began on, over them.
            const common::LocalDay day = common::local_day(started);
            if (!last_day || *last_day != day.serial) {
                day_text = day_label(day, today);
                day_header(day_text, !last_day);
                last_day = day.serial;
            }
            const bool holds_selection = std::any_of(g.pulls.begin(), g.pulls.end(), [&](const meter::PullRef& p) {
                return history[p.index].file == state.selected_file;
            });
            // By the visit, not its place in the list: a new run on top, or one deleted,
            // leaves every other run folded as it was. A version 1 record has no visit,
            // and its run goes by its first fight.
            const std::string key = g.visit_ts != 0 ? std::to_string(g.visit_ts) + "/" + std::to_string(g.map_id)
                                                    : history[g.pulls.back().index].file;
            ImGui::PushID(key.c_str());
            // The newest visit, and the one holding the selection, start open; after
            // that the user's own folding wins.
            ImGui::SetNextItemOpen(gi == 0 || holds_selection, ImGuiCond_Once);
            const std::string map = game::map_label(g.map_id);
            const std::string clock = common::local_clock(started);
            const bool open = visit_header(run_title(map, g.pulls.size()), clock);
            if (ImGui::IsItemHovered()) {
                const size_t n = g.pulls.size();
                const size_t kept = static_cast<size_t>(std::count_if(
                    g.pulls.begin(), g.pulls.end(), [&](const meter::PullRef& p) { return history[p.index].kept; }));
                std::string tip = map + ", from " + clock + "\n" + std::to_string(n) +
                                  (n == 1 ? " fight saved" : " fights saved");
                if (kept > 0) tip += ", " + std::to_string(kept) + " kept";
                tip += "\nRight-click to keep or delete the run.";
                ImGui::SetTooltip("%s", tip.c_str());
            }
            run_menu(history, g, map + ", " + day_text + " " + clock, state, out);
            if (open) {
                const ImGuiIO& io = ImGui::GetIO();
                for (const auto& p : g.pulls) {
                    const auto& e = history[p.index];
                    ImGui::PushID(e.file.c_str());
                    const bool picked = state.picks.marked.count(e.file) != 0;
                    if (pull_row(e, p.number, e.file == state.selected_file, picked)) {
                        // The order only a Shift-click's range needs.
                        const std::vector<std::string> order =
                            io.KeyShift ? list_order(history, groups) : std::vector<std::string>{};
                        if (click(state.picks, order, e.file, io.KeyCtrl, io.KeyShift, state.selected_file)) {
                            state.selected_file = e.file;
                            out.load_file = e.file;
                        }
                    }
                    fight_menu(history, e, state, out);
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    list_keys(history, state);
    delete_dialog(history, state, out);
    end_card();
}

// ----------------------------------------------------------------- top bar ---

void top_bar(const Shown& shown, const std::vector<meter::HistoryEntry>& history, AppViewState& state, AppActions& out) {
    CardOptions opts{};
    opts.auto_height = true;
    begin_card("##TopBar", ImVec2(0.0f, 0.0f), opts);
    const meter::FightSummary& f = *shown.fight;
    // The boss first: it is what tells one fight from the next. Where and which
    // pull follow it, dimmer.
    std::string title = f.live ? "Live fight" : "Last fight";
    std::string where;
    if (shown.entry != nullptr) where = game::map_label(f.map_id) + kDot + "Fight #" + std::to_string(shown.number);
    if (f.boss.present()) {
        if (shown.entry == nullptr) where = title;
        title = f.boss.label;
    } else if (shown.entry != nullptr) {
        title = "Fight #" + std::to_string(shown.number);
        where = game::map_label(f.map_id);
    }
    ImGui::PushFont(bold_font());
    text_colored_u32(colors::TextPrimary, "%s", title.c_str());
    ImGui::PopFont();
    if (!where.empty() && same_line_if_room(ImGui::CalcTextSize(where.c_str()).x, 10.0f)) {
        text_colored_u32(colors::TextDim, "%s", where.c_str());
    }
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
    if (f.detail != nullptr) {
        const size_t deaths = f.detail->deaths.size();
        stat("DEATHS", std::to_string(deaths), deaths > 0 ? colors::DangerLight : colors::TextPrimary,
             "Party members whose HP reached 0 during the fight. The Deaths tab has what hit them.");
    }
    // A killed boss needs no HP figure: the pill at the right says so.
    if (f.boss.present() && !f.boss.killed && f.boss.hp_known) {
        const bool estimate = !f.boss.hp_exact;
        stat("BOSS HP", (estimate ? "~" : "") + format_percent(f.boss.hp_fraction()), colors::TextPrimary,
             estimate ? "An estimate (~): spawn HP less the damage the meter saw, until the server sends the "
                        "boss's HP."
                      : nullptr);
    }

    const Badge o = outcome(f.live, f.boss.killed, f.end_reason);
    const char* end_label = ICON_TIMER "  End fight";
    const bool can_end = f.live && shown.entry == nullptr;
    // A saved fight on view: Keep, lit while it is kept, and Delete.
    const bool saved = shown.entry != nullptr;
    const bool kept = saved && shown.entry->kept;
    const char* keep_label = kept ? ICON_LOCK "  Kept##KeepFight" : ICON_LOCK "  Keep##KeepFight";
    const float icon_w = ImGui::GetFrameHeight();
    float actions_w = pill_width(o.label) + (can_end ? m(10.0f) + button_width(end_label) : 0.0f);
    if (saved) actions_w += m(10.0f) + button_width(keep_label, ButtonSize::Fit) + m(6.0f) + icon_w;
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
    if (saved) {
        ImGui::SameLine(0.0f, m(10.0f));
        ImGui::SetCursorPosY(actions_y);
        if (button(keep_label, kept ? ButtonKind::Primary : ButtonKind::Secondary, ButtonSize::Fit)) {
            (kept ? out.unkeep_files : out.keep_files).push_back(shown.entry->file);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", kept ? "Kept: pruning, Clear and deleting its run leave it. Click to stop keeping it."
                                         : "Keep this fight: pruning, Clear and deleting its run will leave it.");
        }
        ImGui::SameLine(0.0f, m(6.0f));
        ImGui::SetCursorPosY(actions_y + (m(metrics::ButtonH) - icon_w) * 0.5f);
        if (button(ICON_TRASH "##DeleteFight", ButtonKind::Danger, ButtonSize::Icon)) {
            ask_delete(state, delete_picked(history, {shown.entry->file}));
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete this fight (Del)");
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
    OverlaySettings& o = in.settings->overlay;
    const OverlaySettings def{};
    OverlayControls c;
    c.id = "##set_overlay";
    c.locked = &o.locked;
    c.opacity = &o.opacity;
    c.default_opacity = def.opacity;
    c.background_opacity = &o.background_opacity;
    c.default_background_opacity = def.background_opacity;
    c.scale = &o.scale;
    c.hide_when_game_unfocused = &o.hide_when_game_unfocused;
    c.show_in_dungeons_only = &o.show_in_dungeons_only;
    c.reset_position = &out.reset_overlay_position;
    c.position_help = "Back to the top-left corner of the main monitor.";
    c.before = [&] {
        setting_switch("Show the overlay", "Ctrl+Alt+H hides or shows it.", &o.visible, out, Applies::Overlay);
    };
    c.conditions = [&] {
        setting_switch("Only in combat", "Hidden outside a fight.", &o.show_in_combat_only, out);
        if (!o.show_in_combat_only) ImGui::BeginDisabled();
        setting_slider("Hide after combat", "How long it stays up once a fight ends, to read the result.",
                       &o.hide_after_combat_s, 0.0f, 60.0f, def.hide_after_combat_s, "%.0f s", out);
        if (!o.show_in_combat_only) ImGui::EndDisabled();
    };
    overlay_settings_card(c, out);
}

void columns_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##set_columns", ICON_SLIDERS, "OVERLAY COLUMNS", colors::Accent);
    OverlaySettings& o = in.settings->overlay;
    ImGui::PushTextWrapPos(0.0f);
    text_dim("What each player's row shows, after their DPS and damage, and a skull for each death.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.0f, m(2.0f)));
    struct Chip {
        const char* label;
        const char* tip;
        bool* value;
    };
    const Chip chips[] = {
        {"Share", "Each player's share of the party's damage.", &o.show_share},
        {"Hits", "HIT: direct hits.", &o.show_hits},
        {"Crit", "CRI: critical hits.", &o.show_crit},
        {"Perfect", "PERF: perfect hits.", &o.show_perfect},
        {"Double", "DOUB: double hits.", &o.show_double},
        {"Back", "BACK: hits from behind.", &o.show_back},
        {"Front", "FRNT: hits from the front.", &o.show_front},
        {"Multi", "MULT: multi-hits.", &o.show_multi},
        {"Block", "BLOC: hits the target blocked.", &o.show_block},
        {"Max hit", "The player's biggest hit.", &o.show_max_hit},
        {"Healing", "Each player's healing.", &o.show_heal},
        {"Deaths", "A skull after the name of each player who died, bright while they are down.", &o.show_deaths},
    };
    bool first = true;
    for (const Chip& chip : chips) {
        if (!first) same_line_if_room(toggle_chip_width(chip.label), 6.0f);
        first = false;
        if (toggle_chip(chip.label, chip.value)) {
            out.settings_changed = true;
            out.save_settings = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", chip.tip);
    }
    ImGui::Dummy(ImVec2(0.0f, m(6.0f)));
    setting_switch("Class colours", "Bars in each player's class colour.", &o.class_bars, out);
    const OverlaySettings def{};
    setting_slider("Rows", "Players shown, best first.", &o.max_rows, 1, 24, def.max_rows, "%d", out);
    end_settings_card();
}

void tracking_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##set_tracking", ICON_TIMER, "FIGHT TRACKING", colors::Warning);
    meter::MeterSettings& s = in.settings->meter;
    const meter::MeterSettings def{};
    setting_slider("Idle end", "No party hit for this long ends a fight.", &s.idle_timeout_s, 2.0, 120.0,
                   def.idle_timeout_s, "%.0f s", out, Applies::Engine);
    setting_slider("Shortest fight saved", "A fight with no boss is saved if it lasts this long.",
                   &s.min_saved_duration_s, 0.0, 600.0, def.min_saved_duration_s, "%.0f s", out, Applies::Engine);
    setting_switch("Save only dungeon fights",
                   "Keeps the open world out. A new dungeon counts before the meter can name it.",
                   &s.save_only_dungeons, out, Applies::Engine);

    if (advanced_section("##set_tracking_adv")) {
        setting_slider("Idle end with a boss up", "The same while a boss still stands: phases it can't be hit.",
                       &s.boss_idle_timeout_s, s.idle_timeout_s, 300.0, def.boss_idle_timeout_s, "%.0f s", out,
                       Applies::Engine);
        setting_slider("Kill grace", "Hits this long after the boss dies still count.", &s.kill_grace_s, 0.0, 10.0,
                       def.kill_grace_s, "%.1f s", out, Applies::Engine);

        begin_setting_row("Boss HP", "Max HP that makes an enemy a boss. A guess until captures calibrate it.");
        uint64_t hp = s.boss_min_max_hp;
        if (hp != def.boss_min_max_hp) {
            const float control_w = ImGui::CalcItemWidth();
            if (button(ICON_RESET "##boss_hp_reset", ButtonKind::Secondary, ButtonSize::Icon)) {
                s.boss_min_max_hp = hp = def.boss_min_max_hp;
                out.settings_changed = true;
                out.save_settings = true;
                out.engine_changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Back to the default, %llu", static_cast<unsigned long long>(def.boss_min_max_hp));
            }
            ImGui::SameLine(0.0f, m(6.0f));
            ImGui::SetNextItemWidth(control_w - ImGui::GetFrameHeight() - m(6.0f));
        }
        if (ImGui::InputScalar("##boss_hp", ImGuiDataType_U64, &hp, nullptr, nullptr, "%llu")) {
            s.boss_min_max_hp = std::max<uint64_t>(hp, 1);
            out.settings_changed = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            out.save_settings = true;
            out.engine_changed = true;
        }
        end_setting_row();

        setting_slider("Partial data under",
                       "A killed boss that lost less than this share of its HP with hits seen.",
                       &s.partial_damage_ratio, 0.0, 1.0, def.partial_damage_ratio, "%.2f", out, Applies::Engine);
        setting_switch("Wipe ends the fight",
                       "When every party member in it is dead at once. Off: a party can all go down and get "
                       "back up mid-fight.",
                       &s.wipe_detection, out, Applies::Engine);
    }
    end_settings_card();
}

void history_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##set_history", ICON_HISTORY, "SAVED FIGHTS", colors::Success);
    const HistorySettings def{};
    setting_slider("Fights saved", "The oldest are deleted first; those you keep never are, and don't count.",
                   &in.settings->history.max_fights, 10, 5000, def.max_fights, "%d", out, Applies::Engine);
    if (button(ICON_FOLDER "  Open folder")) out.open = OpenTarget::History;
    end_settings_card();
}

void settings_tab(const AppFrameInput& in, AppActions& out) {
    if (in.settings == nullptr) return;
    const SettingsSection sections[] = {
        {[&] { overlay_card(in, out); }},
        {[&] { tracking_card(in, out); }},
        {[&] { columns_card(in, out); }},
        {[&] { history_card(in, out); }},
    };
    render_settings_grid(sections, std::size(sections));
}

// -------------------------------------------------------------------- page ---

/// The narrowest the saved-fights list can be dragged to, and the room the fight
/// beside it keeps, unscaled.
constexpr float kListMinW = 190.0f;
constexpr float kFightMinW = 420.0f;

/// The gap between the list and the fight, as a handle. Dragged, it sets the list's
/// width, saved unscaled so it holds through a change of DPI; double-clicked, the list
/// goes back to fitting the window. `list_w` is the list's width now.
void list_edge(const AppFrameInput& in, AppActions& out, float list_w, float min_w, float max_w) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float w = m(metrics::Gutter);
    const float h = fill_h(0.0f);
    ImGui::InvisibleButton("##ListEdge", ImVec2(w, h));
    if (in.settings == nullptr) return;
    int& saved = in.settings->window.fight_list_width;
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    // The width a drag starts from, kept by ImGui under the handle's id: the edge then
    // moves with the pointer from wherever on it the pointer took hold.
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetItemID();
    if (ImGui::IsItemActivated()) storage->SetFloat(id, list_w);
    if (hovered || active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        const float x = std::floor(pos.x + (w - m(2.0f)) * 0.5f);
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, pos.y), ImVec2(x + m(2.0f), pos.y + h),
                                                  colors::with_alpha(colors::Accent, active ? 0.90f : 0.45f), m(1.0f));
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (saved != 0) {
            saved = 0;
            out.settings_changed = true;
            out.save_settings = true;
        }
        return;
    }
    // Past the drag threshold only, so a click (or the second of a double-click)
    // that wobbles leaves the width alone.
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const float from = storage->GetFloat(id, list_w);
        const float want = std::clamp(from + ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x, min_w, max_w);
        const int px = static_cast<int>(std::lround(want / ui_scale()));
        if (px != saved) {
            saved = px;
            out.settings_changed = true;
            ImGui::MarkItemEdited(id);
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) out.save_settings = true;
    if (hovered && !active) ImGui::SetTooltip("Drag to resize the list. Double-click: fit it to the window.");
}

/// The rail on the left, the fight's top bar and a tab's table on the right.
template <typename Body>
void pull_view(const AppFrameInput& in, AppViewState& state, AppActions& out, const std::vector<meter::PullGroup>& groups,
               const Shown& shown, const char* id, Body body) {
    const float avail = ImGui::GetContentRegionAvail().x;
    // The list as its edge was last dragged, the fight keeping its room; else fitted
    // to the window.
    const float min_w = m(kListMinW);
    const float max_w = std::max(min_w, avail - m(metrics::Gutter) - m(kFightMinW));
    const int saved = in.settings != nullptr ? in.settings->window.fight_list_width : 0;
    const float rail_w = saved > 0         ? std::clamp(m(static_cast<float>(saved)), min_w, max_w)
                         : avail < m(900.0f) ? m(210.0f)
                                             : std::clamp(avail * 0.22f, m(230.0f), m(metrics::RailW));
    rail(in, state, out, groups, rail_w);
    ImGui::SameLine(0.0f, 0.0f);
    list_edge(in, out, rail_w, min_w, max_w);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild(id, ImVec2(0.0f, fill_h(0.0f)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    if (shown.fight == nullptr) {
        if (!state.selected_file.empty()) {
            empty_state(ICON_WARNING, "That fight could not be read", "Its file is missing or damaged.");
        } else {
            // What is missing: the game itself, or only a fight.
            const bool connected = in.snapshot != nullptr && in.snapshot->capture == pipeline::CaptureState::Live;
            if (connected) {
                empty_state(ICON_SWORDS, "No fight yet",
                            "Hit something in game: the meter starts on the party's first hit.");
            } else {
                empty_state(ICON_GAMEPAD, "Waiting for the game",
                            "Start AION 2 and log in: the meter finds the game by itself. The Dashboard shows what "
                            "the capture sees.");
            }
        }
    } else {
        const std::vector<meter::HistoryEntry> none;
        top_bar(shown, in.history != nullptr ? *in.history : none, state, out);
        ImGui::Dummy(ImVec2(0.0f, m(4.0f)));
        body(*shown.fight);
    }
    ImGui::EndChild();
}

} // namespace

void render_meter_page(const AppFrameInput& in, AppViewState& state, AppActions& out) {
    render_plugin_header(in, out, plugins::PluginId::CombatMeter, plugin_icon(plugins::PluginId::CombatMeter),
                         "Every fight, live and saved, for the whole party");
    if (render_plugin_disabled_gate(in, out, plugins::PluginId::CombatMeter)) return;

    const std::vector<meter::HistoryEntry> none;
    const auto& history = in.history != nullptr ? *in.history : none;
    const std::vector<meter::PullGroup> groups = meter::group_by_visit(history);
    drop_missing(state.picks, history);

    // A selection that left the history (deleted, or pruned) falls back to live.
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
        {ICON_SPARKLE, "Buffs & Debuffs",
         [&] { pull_view(in, state, out, groups, shown, "##EffectsPane", [&](const auto& f) { effects_tab(f, state); }); }},
        {ICON_BOLT, "Casts",
         [&] { pull_view(in, state, out, groups, shown, "##CastsPane", [&](const auto& f) { casts_tab(f, state); }); }},
        {ICON_TRENDING, "Timeline",
         [&] {
             pull_view(in, state, out, groups, shown, "##TimelinePane", [&](const auto& f) { timeline_tab(f, state); });
         }},
        {ICON_SKULL, "Deaths",
         [&] { pull_view(in, state, out, groups, shown, "##DeathsPane", [&](const auto& f) { deaths_tab(f, state); }); }},
        {ICON_SLIDERS, "Settings", [&] { settings_tab(in, out); }},
    };
    static_assert(std::size(tabs) == static_cast<size_t>(MeterTab::Settings) + 1, "a tab for every MeterTab");
    render_tabs("##MeterTabs", tabs, std::size(tabs), state.open_meter_tab ? static_cast<int>(*state.open_meter_tab) : -1);
    state.open_meter_tab.reset();
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
