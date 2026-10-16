#include "app/overlay_view.hpp"

#include "app/display_format.hpp"
#include "app/imgui_guard.hpp"
#include "common/time_format.hpp"

#ifdef HAVE_IMGUI

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace aion2::app {

namespace {

ImVec4 rgba(Rgb c, float a = 1.0f) { return ImVec4(c.r, c.g, c.b, a); }

ImU32 u32(Rgb c, float a = 1.0f) { return ImGui::GetColorU32(rgba(c, a)); }

/// Text ending at `x_right`.
void text_right(ImDrawList* dl, float x_right, float y, ImU32 col, const std::string& s) {
    const ImVec2 size = ImGui::CalcTextSize(s.c_str());
    dl->AddText(ImVec2(x_right - size.x, y), col, s.c_str());
}

/// `s` cut with "..." to fit `width`.
std::string fit(const std::string& s, float width) {
    if (ImGui::CalcTextSize(s.c_str()).x <= width) return s;
    std::string out = s;
    while (!out.empty() && ImGui::CalcTextSize((out + "...").c_str()).x > width) out.pop_back();
    return out + "...";
}

/// A small button's width, to lay the header's buttons out from the right.
float button_width(const char* label) {
    return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

struct Column {
    const char* header;
    float width;
};

/// The ranked rows: one bar per player, scaled to the top DPS.
void draw_rows(const meter::FightSummary& f, const OverlaySettings& s, float scale, bool interactive) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line = ImGui::GetTextLineHeight();
    const float row_h = line + 5.0f * scale;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;

    std::vector<Column> cols;
    cols.push_back({"DPS", 60.0f * scale});
    cols.push_back({"Damage", 66.0f * scale});
    if (s.show_share) cols.push_back({"Share", 54.0f * scale});
    if (s.show_crit) cols.push_back({"Crit", 54.0f * scale});
    if (s.show_back) cols.push_back({"Back", 54.0f * scale});
    if (s.show_max_hit) cols.push_back({"Max", 58.0f * scale});
    const float rank_w = 18.0f * scale;
    float numbers_w = 0;
    for (const auto& c : cols) numbers_w += c.width;
    const float name_w = std::max(40.0f * scale, width - rank_w - numbers_w);

    // Header.
    float y = origin.y;
    const ImU32 dim = u32(colors::TextDim);
    dl->AddText(ImVec2(origin.x, y), dim, "#");
    dl->AddText(ImVec2(origin.x + rank_w, y), dim, "Player");
    float x = origin.x + rank_w + name_w;
    for (const auto& c : cols) {
        x += c.width;
        text_right(dl, x - 4.0f * scale, y, dim, c.header);
    }
    y += row_h;

    const double top = f.combatants.empty() ? 1.0 : std::max(f.combatants.front().dps, 1.0);
    int rank = 0;
    for (const auto& row : f.combatants) {
        ++rank;
        const ImVec2 row_min(origin.x, y);
        const ImVec2 row_max(origin.x + width, y + row_h - 1.0f * scale);
        if (rank % 2 == 0) dl->AddRectFilled(row_min, row_max, u32(colors::Text, 0.03f));
        const float fraction = static_cast<float>(std::clamp(row.dps / top, 0.0, 1.0));
        dl->AddRectFilled(row_min, ImVec2(row_min.x + width * fraction, row_max.y), u32(row_color(row), 0.38f),
                          2.0f * scale);

        const float ty = y + (row_h - line) * 0.5f - 0.5f * scale;
        char rank_buf[8];
        std::snprintf(rank_buf, sizeof(rank_buf), "%d", rank);
        dl->AddText(ImVec2(origin.x + 2.0f * scale, ty), dim, rank_buf);

        std::string label = row.name;
        if (row.cls != meter::AionClass::Unknown) label = std::string(meter::class_short(row.cls)) + " " + label;
        dl->AddText(ImVec2(origin.x + rank_w, ty), u32(row.is_local ? colors::Accent : colors::Text),
                    fit(label, name_w - 6.0f * scale).c_str());

        x = origin.x + rank_w + name_w;
        const ImU32 text = u32(colors::Text);
        size_t ci = 0;
        // The next column's value, right-aligned in it.
        const auto put = [&](const std::string& v) {
            x += cols[ci++].width;
            text_right(dl, x - 4.0f * scale, ty, text, v);
        };
        put(format_rate(row.dps));
        put(format_amount(row.damage));
        if (s.show_share) put(format_percent(row.share));
        if (s.show_crit) put(format_percent(row.crit_rate()));
        if (s.show_back) put(format_percent(row.back_rate()));
        if (s.show_max_hit) put(format_amount(row.max_hit));

        if (interactive && ImGui::IsMouseHoveringRect(row_min, row_max)) {
            ImGui::SetTooltip("%s\n%llu damage (%llu from DoTs)\n%llu hits, %s crit, %s back attacks\n"
                              "max hit %llu\nclass code %u",
                              row.name.c_str(), static_cast<unsigned long long>(row.damage),
                              static_cast<unsigned long long>(row.dot_damage),
                              static_cast<unsigned long long>(row.hits.hits), format_percent(row.crit_rate()).c_str(),
                              format_percent(row.back_rate()).c_str(), static_cast<unsigned long long>(row.max_hit),
                              row.class_code);
        }
        y += row_h;
    }
    ImGui::Dummy(ImVec2(width, y - origin.y));
}

/// The target's name and HP bar, and the data-quality warnings.
void draw_boss(const meter::FightSummary& f, float scale) {
    if (!f.boss.present()) return;
    const auto& b = f.boss;
    std::string label = b.label;
    if (b.is_boss) label = "Boss " + label;
    if (b.killed) label += "  (killed)";
    ImGui::TextUnformatted(label.c_str());
    if (b.hp_known) {
        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "%s%s / %s", b.killed || b.hp_exact ? "" : "~", format_amount(b.hp).c_str(),
                      format_amount(b.max_hp).c_str());
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, rgba(colors::BossHp, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, rgba(colors::Background, 0.9f));
        ImGui::ProgressBar(static_cast<float>(b.hp_fraction()), ImVec2(-1.0f, 6.0f * scale + ImGui::GetTextLineHeight()),
                           overlay);
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Spawn HP less the damage the meter saw: an estimate (~) until the\n"
                              "game's own HP packet is decoded.");
        }
    }
    if (f.quality.partial) {
        ImGui::TextColored(rgba(colors::Warn), "Partial data");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The boss died having taken far less damage than it had HP.\n"
                              "On crowded world bosses the server sends only some players' hits.");
        }
    } else if (!f.quality.clean()) {
        ImGui::TextColored(rgba(colors::Warn), "Capture lost some packets");
    }
    (void)scale;
}

const char* status_label(const pipeline::LiveSnapshot* snap, const meter::FightSummary* f, bool history) {
    if (history) return "SAVED";
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

OverlayActions draw_overlay(const OverlayFrameInput& in, OverlayViewState& state) {
    OverlayActions out;
    const OverlaySettings defaults{};
    const OverlaySettings& s = in.settings != nullptr ? *in.settings : defaults;
    const float scale = in.scale;
    const bool interactive = !s.locked;
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * scale, 6.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, rgba(colors::Background, s.opacity));
    ImGui::PushStyleColor(ImGuiCol_Border, rgba(colors::Border, std::min(1.0f, s.opacity + 0.2f)));
    ImGui::PushStyleColor(ImGuiCol_Text, rgba(colors::Text));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("##aion2meter", nullptr, flags)) {
        const bool history_tab = state.tab == OverlayViewState::Tab::History;
        const meter::FightSummary* fight = nullptr;
        if (history_tab) {
            if (state.selected_fight) fight = &*state.selected_fight;
        } else if (in.snapshot != nullptr && in.snapshot->fight.id != 0) {
            fight = &in.snapshot->fight;
        }

        // Header: status, timer, totals; buttons on the right.
        const float header_top = ImGui::GetCursorPosY();
        const char* status = status_label(in.snapshot, fight, history_tab);
        ImGui::TextColored(rgba(status_color(status)), "%s", status);
        if (fight != nullptr) {
            ImGui::SameLine();
            ImGui::TextUnformatted(common::clock_duration(fight->duration_s).c_str());
            ImGui::SameLine();
            ImGui::TextColored(rgba(colors::Dps), "%s DPS", format_rate(fight->total_dps).c_str());
            ImGui::SameLine();
            ImGui::TextColored(rgba(colors::TextDim), "%s", format_amount(fight->total_damage).c_str());
        }
        const float text_end_x = ImGui::GetItemRectMax().x;

        const char* tab_label = history_tab ? "Live" : "History";
        const char* lock_label = "Lock";
        const char* rec_label = in.recording ? "Stop rec" : "Rec";
        const bool show_end = !history_tab && fight != nullptr && fight->live;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        float buttons_w = button_width(tab_label) + button_width(lock_label) + button_width(rec_label) + 2 * spacing;
        if (show_end) buttons_w += button_width("End") + spacing;
        const float buttons_x = ImGui::GetWindowContentRegionMax().x - buttons_w;
        if (interactive && buttons_x > text_end_x + spacing) {
            ImGui::SameLine(buttons_x);
            if (show_end && ImGui::SmallButton("End")) out.end_fight = true;
            if (show_end) ImGui::SameLine();
            if (in.recording) ImGui::PushStyleColor(ImGuiCol_Button, rgba(colors::Live, 0.7f));
            if (ImGui::SmallButton(rec_label)) out.toggle_recording = true;
            if (in.recording) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Record the game's traffic to a .pcapng for aion2meter-replay.\n"
                                  "Recordings hold character names: share them with care.");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(tab_label)) {
                state.tab = history_tab ? OverlayViewState::Tab::Live : OverlayViewState::Tab::History;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(lock_label)) out.lock = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click-through and fixed in place. Ctrl+Alt+L unlocks.");
        }
        const float header_bottom = ImGui::GetCursorPosY();
        out.drag_area = ClientRect{0.0f, 0.0f, interactive ? std::max(buttons_x, text_end_x) : 0.0f,
                                   header_bottom};
        (void)header_top;
        ImGui::Separator();

        const float footer_h = ImGui::GetTextLineHeightWithSpacing();
        if (history_tab) {
            const std::vector<meter::HistoryEntry> empty;
            const auto& history = in.history != nullptr ? *in.history : empty;
            const float list_h = std::max(3.0f * ImGui::GetTextLineHeightWithSpacing(),
                                          (ImGui::GetContentRegionAvail().y - footer_h) * 0.4f);
            if (ImGui::BeginChild("##history", ImVec2(0.0f, list_h), ImGuiChildFlags_None)) {
                if (history.empty()) {
                    ImGui::TextColored(rgba(colors::TextDim), "No saved fights yet.");
                }
                for (auto it = history.rbegin(); it != history.rend(); ++it) {
                    char line[160];
                    std::snprintf(line, sizeof(line), "%s  %-14s %6s  %8s dps%s%s##%s",
                                  common::local_clock(it->start_ts).c_str(),
                                  it->boss_label.empty() ? "-" : it->boss_label.c_str(),
                                  common::clock_duration(it->duration_s).c_str(), format_rate(it->total_dps).c_str(),
                                  it->killed ? "  killed" : "", it->partial ? "  partial" : "", it->file.c_str());
                    if (ImGui::Selectable(line, state.selected_file == it->file)) out.load_file = it->file;
                }
            }
            ImGui::EndChild();
            if (fight != nullptr) {
                draw_boss(*fight, scale);
                draw_rows(*fight, s, scale, interactive);
            }
        } else if (fight != nullptr) {
            draw_boss(*fight, scale);
            if (ImGui::BeginChild("##rows", ImVec2(0.0f, -footer_h), ImGuiChildFlags_None,
                                  ImGuiWindowFlags_NoBackground)) {
                draw_rows(*fight, s, scale, interactive);
            }
            ImGui::EndChild();
        } else {
            ImGui::TextColored(rgba(colors::TextDim), "No fight yet: hit something.");
        }

        // Footer: who and where, or what the capture is waiting for.
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - footer_h - ImGui::GetStyle().WindowPadding.y + 2.0f);
        std::string footer;
        if (in.snapshot != nullptr && in.snapshot->capture != pipeline::CaptureState::Waiting) {
            if (!in.snapshot->local_name.empty()) footer = in.snapshot->local_name;
            if (in.snapshot->map_id != 0) {
                footer += (footer.empty() ? "" : "  ") + std::string("map ") + std::to_string(in.snapshot->map_id);
            }
            if (in.snapshot->capture == pipeline::CaptureState::Quiet) footer += "  (no game traffic)";
        } else {
            footer = in.source_status.empty() ? "Starting capture..." : in.source_status;
        }
        if (in.recording) ImGui::TextColored(rgba(colors::Live), "REC");
        if (in.recording) ImGui::SameLine();
        ImGui::TextColored(rgba(colors::TextDim), "%s",
                           fit(footer, ImGui::GetContentRegionAvail().x).c_str());

        // Resize grip.
        if (interactive) {
            const float g = 12.0f * scale;
            const ImVec2 br(io.DisplaySize.x - 2.0f, io.DisplaySize.y - 2.0f);
            ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(br.x - g, br.y), br, ImVec2(br.x, br.y - g),
                                                          u32(colors::TextDim, 0.6f));
            out.resize_grip = ClientRect{br.x - g, br.y - g, br.x + 2.0f, br.y + 2.0f};
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
    return out;
}

} // namespace aion2::app

#else // !HAVE_IMGUI

namespace aion2::app {

OverlayActions draw_overlay(const OverlayFrameInput&, OverlayViewState&) { return {}; }

} // namespace aion2::app

#endif
