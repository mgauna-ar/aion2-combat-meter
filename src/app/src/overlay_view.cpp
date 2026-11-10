#include "app/overlay_view.hpp"

#include "app/display_format.hpp"
#include "app/game_icons.hpp"
#include "app/imgui_guard.hpp"
#include "app/overlay_chrome.hpp"
#include "common/time_format.hpp"
#include "game/map_names.hpp"

#ifdef HAVE_IMGUI

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace aion2::app {

using chrome::fit;
using chrome::rgba;
using chrome::text_right;
using chrome::u32;

namespace {

struct Column {
    const char* header;
    float width;
};

/// The ranked rows: one bar per player, scaled to the top DPS.
void draw_rows(const meter::FightSummary& f, const OverlaySettings& s, float scale, bool interactive) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line = ImGui::GetTextLineHeight();
    // A bar, and the gap under it.
    const float gap = 2.0f * scale;
    const float row_h = line + 8.0f * scale + gap;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;

    std::vector<Column> cols;
    cols.push_back({"DPS", 60.0f * scale});
    cols.push_back({"Damage", 66.0f * scale});
    if (s.show_share) cols.push_back({"Share", 54.0f * scale});
    if (s.show_hits) cols.push_back({"Hits", 44.0f * scale});
    if (s.show_crit) cols.push_back({"Crit", 54.0f * scale});
    if (s.show_perfect) cols.push_back({"Perf", 54.0f * scale});
    if (s.show_double) cols.push_back({"Dbl", 54.0f * scale});
    if (s.show_back) cols.push_back({"Back", 54.0f * scale});
    if (s.show_front) cols.push_back({"Front", 54.0f * scale});
    if (s.show_multi) cols.push_back({"Multi", 54.0f * scale});
    if (s.show_block) cols.push_back({"Block", 54.0f * scale});
    if (s.show_max_hit) cols.push_back({"Max", 58.0f * scale});
    if (s.show_heal) cols.push_back({"Heal", 60.0f * scale});
    const float rank_w = 22.0f * scale;
    float numbers_w = 0;
    for (const auto& c : cols) numbers_w += c.width;
    const float name_w = std::max(40.0f * scale, width - rank_w - numbers_w);
    // The class emblem starts the name column; every row keeps its place, so the
    // names line up.
    const float emblem = kMeterEmblemPx * scale;
    const float name_x = origin.x + rank_w + emblem + 6.0f * scale;
    const float pad = 6.0f * scale;

    // The captions.
    float y = origin.y;
    const ImU32 dim = u32(colors::TextDim, 0.85f);
    dl->AddText(ImVec2(origin.x + pad, y), dim, "#");
    dl->AddText(ImVec2(name_x, y), dim, "Player");
    float x = origin.x + rank_w + name_w;
    for (const auto& c : cols) {
        x += c.width;
        text_right(dl, x - pad, y, dim, c.header);
    }
    y += line + 4.0f * scale;

    const double top = f.combatants.empty() ? 1.0 : std::max(f.combatants.front().dps, 1.0);
    for (const ShownRow& shown : shown_rows(f.combatants, s.max_rows, s.show_heal)) {
        const meter::CombatantTotals& row = f.combatants[shown.index];
        const ImVec2 row_min(origin.x, y);
        const ImVec2 row_max(origin.x + width, y + row_h - gap);
        const float fraction = static_cast<float>(std::clamp(row.dps / top, 0.0, 1.0));
        chrome::draw_bar(dl, row_min, row_max, fraction, s.class_bars ? row_color(row) : colors::Accent, scale);
        // The local player's row: a stripe down its left edge.
        if (row.is_local) {
            dl->AddRectFilled(row_min, ImVec2(row_min.x + 3.0f * scale, row_max.y), u32(colors::Accent),
                              3.0f * scale, ImDrawFlags_RoundCornersLeft);
        }

        const float ty = y + (row_h - gap - line) * 0.5f - 0.5f * scale;
        char rank_buf[8];
        std::snprintf(rank_buf, sizeof(rank_buf), "%d", shown.rank);
        dl->AddText(ImVec2(origin.x + pad, ty), u32(colors::Text, 0.60f), rank_buf);

        const int icon = class_icon(row.cls);
        draw_icon(dl, icon, origin.x + rank_w, y + (row_h - gap - emblem) * 0.5f, emblem);
        // A class with no emblem (the Brawler) keeps its letters.
        std::string label = row.name;
        if (icon == kNoIcon && row.cls != game::AionClass::Unknown) {
            label = std::string(game::class_short(row.cls)) + " " + label;
        }
        const float label_w = origin.x + rank_w + name_w - name_x - pad;
        if (row.is_local) {
            const chrome::BoldScope bold;
            dl->AddText(ImVec2(name_x, ty), u32(colors::Text), fit(label, label_w).c_str());
        } else {
            dl->AddText(ImVec2(name_x, ty), u32(colors::Text), fit(label, label_w).c_str());
        }

        x = origin.x + rank_w + name_w;
        const ImU32 text = u32(colors::Text, 0.86f);
        size_t ci = 0;
        // The next column's value, right-aligned in it.
        const auto put = [&](const std::string& v) {
            x += cols[ci++].width;
            text_right(dl, x - pad, ty, text, v);
        };
        const auto rate = [&](uint64_t meter::HitCounts::*count) { return format_percent(row.hits.rate(count)); };
        {
            // The figure the rows are ranked by.
            const chrome::BoldScope bold;
            x += cols[ci++].width;
            text_right(dl, x - pad, ty, u32(colors::Text), format_rate(row.dps));
        }
        put(format_amount(row.damage));
        if (s.show_share) put(format_percent(row.share));
        if (s.show_hits) put(std::to_string(row.hits.hits));
        if (s.show_crit) put(rate(&meter::HitCounts::crits));
        if (s.show_perfect) put(rate(&meter::HitCounts::perfect));
        if (s.show_double) put(rate(&meter::HitCounts::doubles));
        if (s.show_back) put(rate(&meter::HitCounts::back));
        if (s.show_front) put(rate(&meter::HitCounts::front));
        if (s.show_multi) put(rate(&meter::HitCounts::multi));
        if (s.show_block) put(rate(&meter::HitCounts::blocks));
        if (s.show_max_hit) put(format_amount(row.max_hit));
        if (s.show_heal) put(format_amount(row.healing));

        if (interactive && ImGui::IsMouseHoveringRect(row_min, row_max)) {
            ImGui::SetTooltip("%s\n%llu damage (%llu from DoTs), %llu healing\n%llu hits: %s crit, %s perfect, "
                              "%s double, %s multi, %s blocked\n%s back, %s front; max hit %llu",
                              row.name.c_str(), static_cast<unsigned long long>(row.damage),
                              static_cast<unsigned long long>(row.dot_damage),
                              static_cast<unsigned long long>(row.healing),
                              static_cast<unsigned long long>(row.hits.hits), rate(&meter::HitCounts::crits).c_str(),
                              rate(&meter::HitCounts::perfect).c_str(), rate(&meter::HitCounts::doubles).c_str(),
                              rate(&meter::HitCounts::multi).c_str(), rate(&meter::HitCounts::blocks).c_str(),
                              rate(&meter::HitCounts::back).c_str(), rate(&meter::HitCounts::front).c_str(),
                              static_cast<unsigned long long>(row.max_hit));
        }
        y += row_h;
    }
    ImGui::Dummy(ImVec2(width, y - origin.y));
}

/// The target's name and HP bar, and the data-quality warnings. Tooltips only while
/// the window takes the mouse: once locked, ImGui's mouse position is stale.
void draw_boss(const meter::FightSummary& f, float scale, bool interactive) {
    if (!f.boss.present()) return;
    const auto& b = f.boss;
    if (b.is_boss) {
        ImGui::TextColored(rgba(colors::TextDim), "Boss");
        ImGui::SameLine(0.0f, 5.0f * scale);
    }
    {
        const chrome::BoldScope bold;
        ImGui::TextUnformatted(b.label.c_str());
    }
    if (b.killed) {
        ImGui::SameLine();
        ImGui::TextColored(rgba(colors::TextDim), "killed");
    }
    if (b.hp_known) {
        // The bar, with what is left written in it and its share at the right.
        const bool estimate = !(b.killed || b.hp_exact);
        const float line = ImGui::GetTextLineHeight();
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + ImGui::GetContentRegionAvail().x, min.y + line + 7.0f * scale);
        ImGui::Dummy(ImVec2(max.x - min.x, max.y - min.y));
        const bool hovered = interactive && ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const auto fraction = static_cast<float>(b.hp_fraction());
        chrome::draw_bar(dl, min, max, fraction, colors::BossHp, scale, chrome::BarStyle{3.0f, 0.90f, true});
        const float ty = min.y + (max.y - min.y - line) * 0.5f - 0.5f * scale;
        const std::string left =
            std::string(estimate ? "~" : "") + format_amount(b.hp) + " / " + format_amount(b.max_hp);
        chrome::shadowed_text(dl, ImVec2(min.x + 7.0f * scale, ty), left, scale);
        {
            const chrome::BoldScope bold;
            const std::string share = format_percent(static_cast<double>(fraction));
            chrome::shadowed_text(dl, ImVec2(max.x - 7.0f * scale - ImGui::CalcTextSize(share.c_str()).x, ty), share,
                                  scale);
        }
        if (estimate && hovered) {
            ImGui::SetTooltip("An estimate (~): spawn HP less the damage the meter saw, until\n"
                              "the server sends the boss's HP.");
        }
    }
    if (f.quality.partial) {
        ImGui::TextColored(rgba(colors::Warn), "Partial data");
        if (interactive && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Much of the boss's HP went down with no hit the meter saw.\n"
                              "On crowded world bosses the server sends only some players' hits.");
        }
    } else if (!f.quality.clean()) {
        ImGui::TextColored(rgba(colors::Warn), "Capture lost some packets");
    }
}

const char* status_label(const pipeline::LiveSnapshot* snap, const meter::FightSummary* f) {
    if (snap == nullptr || snap->capture == pipeline::CaptureState::Waiting) return "WAIT";
    if (f != nullptr && f->live) return "LIVE";
    if (snap->capture == pipeline::CaptureState::Quiet) return "QUIET";
    return "DONE";
}

Rgb status_color(const char* label) {
    const std::string s = label;
    if (s == "LIVE") return colors::Live;
    if (s == "WAIT" || s == "QUIET") return colors::Warn;
    return colors::TextDim;
}

} // namespace

OverlayActions draw_overlay(const OverlayFrameInput& in) {
    OverlayActions out;
    const OverlaySettings defaults{};
    const OverlaySettings& s = in.settings != nullptr ? *in.settings : defaults;
    const float scale = in.scale;
    const bool interactive = !s.locked;

    // The frame keeps the opacity's own border, locked or not: this overlay is never
    // empty, so it needs no brighter one to be found by.
    if (chrome::begin("##aion2meter", s.opacity, scale, false)) {
        const meter::FightSummary* fight =
            in.snapshot != nullptr && in.snapshot->fight.id != 0 ? &in.snapshot->fight : nullptr;

        // Header: status, timer, totals; buttons on the right.
        const char* status = status_label(in.snapshot, fight);
        chrome::dot(status_color(status), scale);
        ImGui::SameLine(0.0f, 5.0f * scale);
        {
            const chrome::BoldScope bold;
            ImGui::TextColored(rgba(status_color(status)), "%s", status);
            if (fight != nullptr) {
                ImGui::SameLine();
                ImGui::TextUnformatted(common::clock_duration(fight->duration_s).c_str());
                ImGui::SameLine();
                ImGui::TextColored(rgba(colors::Dps), "%s DPS", format_rate(fight->total_dps).c_str());
            }
        }
        if (fight != nullptr) {
            ImGui::SameLine();
            ImGui::TextColored(rgba(colors::TextDim), "%s", format_amount(fight->total_damage).c_str());
        }
        chrome::HeaderButtons buttons;
        buttons.scale = scale;
        buttons.interactive = interactive;
        buttons.app_tooltip = "Open the app: saved fights, skills and settings.";
        buttons.lock_tooltip = "Click-through and fixed in place. Ctrl+Alt+L unlocks.";
        chrome::finish_header(buttons, out);

        const float footer_h = ImGui::GetTextLineHeightWithSpacing();
        if (fight != nullptr) {
            draw_boss(*fight, scale, interactive);
            if (ImGui::BeginChild("##rows", ImVec2(0.0f, -footer_h), ImGuiChildFlags_None,
                                  ImGuiWindowFlags_NoBackground)) {
                draw_rows(*fight, s, scale, interactive);
            }
            ImGui::EndChild();
        } else {
            ImGui::TextColored(rgba(colors::TextDim), "No fight yet: hit something.");
        }

        // Footer: who and where, or what the capture is waiting for.
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - footer_h - ImGui::GetStyle().WindowPadding.y + 3.0f * scale);
        std::string footer;
        if (in.snapshot != nullptr && in.snapshot->capture != pipeline::CaptureState::Waiting) {
            if (!in.snapshot->local_name.empty()) footer = in.snapshot->local_name;
            if (in.snapshot->map_id != 0) {
                footer += (footer.empty() ? "" : "  ") + game::map_label(in.snapshot->map_id);
            }
            if (in.snapshot->capture == pipeline::CaptureState::Quiet) footer += "  (no game traffic)";
        } else {
            footer = in.source_status.empty() ? "Starting capture..." : in.source_status;
        }
        if (in.recording) {
            chrome::dot(colors::Live, scale);
            ImGui::SameLine(0.0f, 5.0f * scale);
            ImGui::TextColored(rgba(colors::Live), "REC");
            ImGui::SameLine();
        }
        ImGui::TextColored(rgba(colors::TextDim), "%s", fit(footer, ImGui::GetContentRegionAvail().x).c_str());

        if (interactive) out.resize_grip = chrome::resize_grip(scale);
    }
    chrome::end();
    return out;
}

} // namespace aion2::app

#else // !HAVE_IMGUI

namespace aion2::app {

OverlayActions draw_overlay(const OverlayFrameInput&) { return {}; }

} // namespace aion2::app

#endif
