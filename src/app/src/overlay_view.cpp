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

/// `s` cut with "..." to fit `width`, by whole UTF-8 code points.
std::string fit(const std::string& s, float width) {
    if (ImGui::CalcTextSize(s.c_str()).x <= width) return s;
    std::string out = s;
    while (!out.empty() && ImGui::CalcTextSize((out + "...").c_str()).x > width) {
        // Continuation bytes (10xxxxxx) go with the lead byte before them.
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) out.pop_back();
        if (!out.empty()) out.pop_back();
    }
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
    if (s.show_hits) cols.push_back({"Hits", 44.0f * scale});
    if (s.show_crit) cols.push_back({"Crit", 54.0f * scale});
    if (s.show_perfect) cols.push_back({"Perf", 54.0f * scale});
    if (s.show_double) cols.push_back({"Dbl", 54.0f * scale});
    if (s.show_back) cols.push_back({"Back", 54.0f * scale});
    if (s.show_front) cols.push_back({"Front", 54.0f * scale});
    if (s.show_max_hit) cols.push_back({"Max", 58.0f * scale});
    if (s.show_heal) cols.push_back({"Heal", 60.0f * scale});
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
        // Players who only healed or were hit rank below everyone who dealt damage.
        if (row.damage == 0 && !s.show_heal) continue;
        if (rank >= s.max_rows) break;
        ++rank;
        const ImVec2 row_min(origin.x, y);
        const ImVec2 row_max(origin.x + width, y + row_h - 1.0f * scale);
        if (rank % 2 == 0) dl->AddRectFilled(row_min, row_max, u32(colors::Text, 0.03f));
        const float fraction = static_cast<float>(std::clamp(row.dps / top, 0.0, 1.0));
        const Rgb bar = s.class_bars ? row_color(row) : colors::Accent;
        dl->AddRectFilled(row_min, ImVec2(row_min.x + width * fraction, row_max.y), u32(bar, s.class_bars ? 0.38f : 0.28f),
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
        const auto rate = [&](uint64_t meter::HitCounts::*count) { return format_percent(row.hits.rate(count)); };
        put(format_rate(row.dps));
        put(format_amount(row.damage));
        if (s.show_share) put(format_percent(row.share));
        if (s.show_hits) put(std::to_string(row.hits.hits));
        if (s.show_crit) put(rate(&meter::HitCounts::crits));
        if (s.show_perfect) put(rate(&meter::HitCounts::perfect));
        if (s.show_double) put(rate(&meter::HitCounts::doubles));
        if (s.show_back) put(rate(&meter::HitCounts::back));
        if (s.show_front) put(rate(&meter::HitCounts::front));
        if (s.show_max_hit) put(format_amount(row.max_hit));
        if (s.show_heal) put(format_amount(row.healing));

        if (interactive && ImGui::IsMouseHoveringRect(row_min, row_max)) {
            ImGui::SetTooltip("%s\n%llu damage (%llu from DoTs), %llu healing\n%llu hits: %s crit, %s perfect, "
                              "%s double\n%s back, %s front; max hit %llu",
                              row.name.c_str(), static_cast<unsigned long long>(row.damage),
                              static_cast<unsigned long long>(row.dot_damage),
                              static_cast<unsigned long long>(row.healing),
                              static_cast<unsigned long long>(row.hits.hits), rate(&meter::HitCounts::crits).c_str(),
                              rate(&meter::HitCounts::perfect).c_str(), rate(&meter::HitCounts::doubles).c_str(),
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
    std::string label = b.label;
    if (b.is_boss) label = "Boss " + label;
    if (b.killed) label += "  (killed)";
    ImGui::TextUnformatted(label.c_str());
    if (b.hp_known) {
        const bool estimate = !(b.killed || b.hp_exact);
        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "%s%s / %s", estimate ? "~" : "", format_amount(b.hp).c_str(),
                      format_amount(b.max_hp).c_str());
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, rgba(colors::BossHp, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, rgba(colors::Background, 0.9f));
        ImGui::ProgressBar(static_cast<float>(b.hp_fraction()), ImVec2(-1.0f, 6.0f * scale + ImGui::GetTextLineHeight()),
                           overlay);
        ImGui::PopStyleColor(2);
        if (estimate && interactive && ImGui::IsItemHovered()) {
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
    (void)scale;
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
        const meter::FightSummary* fight =
            in.snapshot != nullptr && in.snapshot->fight.id != 0 ? &in.snapshot->fight : nullptr;

        // Header: status, timer, totals; buttons on the right.
        const char* status = status_label(in.snapshot, fight);
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

        const char* app_label = "App";
        const char* lock_label = "Lock";
        const bool show_end = fight != nullptr && fight->live;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        float buttons_w = button_width(app_label) + button_width(lock_label) + spacing;
        if (show_end) buttons_w += button_width("End") + spacing;
        const float buttons_x = ImGui::GetWindowContentRegionMax().x - buttons_w;
        if (interactive && buttons_x > text_end_x + spacing) {
            ImGui::SameLine(buttons_x);
            if (show_end && ImGui::SmallButton("End")) out.end_fight = true;
            if (show_end) ImGui::SameLine();
            if (ImGui::SmallButton(app_label)) out.open_app = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open the app: saved fights, skills and settings.");
            ImGui::SameLine();
            if (ImGui::SmallButton(lock_label)) out.lock = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click-through and fixed in place. Ctrl+Alt+L unlocks.");
        }
        const float header_bottom = ImGui::GetCursorPosY();
        out.drag_area = ClientRect{0.0f, 0.0f, interactive ? std::max(buttons_x, text_end_x) : 0.0f, header_bottom};
        ImGui::Separator();

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
        ImGui::TextColored(rgba(colors::TextDim), "%s", fit(footer, ImGui::GetContentRegionAvail().x).c_str());

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

OverlayActions draw_overlay(const OverlayFrameInput&) { return {}; }

} // namespace aion2::app

#endif
