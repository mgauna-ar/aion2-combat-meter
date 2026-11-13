#include "app/power_view.hpp"

#include "app/display_format.hpp"
#include "app/imgui_guard.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace aion2::app {

std::string format_power(uint32_t combat_power) {
    if (combat_power == 0) return "-";
    std::string digits = std::to_string(combat_power);
    for (size_t at = digits.size(); at > 3;) {
        at -= 3;
        digits.insert(at, ",");
    }
    return digits;
}

std::string format_power_short(uint32_t combat_power) {
    if (combat_power == 0) return "-";
    if (combat_power < 1000) return std::to_string(combat_power);
    // Three figures, the decimals chosen by how the value rounds: 9,996 reads 10.0k,
    // and 999,600 reads 1.00M, never 10.00k or 1000k.
    const auto three = [](double v, const char* unit) -> std::string {
        char buf[16];
        if (std::round(v * 100.0) < 1000.0) std::snprintf(buf, sizeof(buf), "%.2f%s", v, unit);
        else if (std::round(v * 10.0) < 1000.0) std::snprintf(buf, sizeof(buf), "%.1f%s", v, unit);
        else std::snprintf(buf, sizeof(buf), "%.0f%s", v, unit);
        return buf;
    };
    const double thousands = static_cast<double>(combat_power) / 1e3;
    if (std::round(thousands) < 1000.0) return three(thousands, "k");
    return three(static_cast<double>(combat_power) / 1e6, "M");
}

std::string format_distance(float metres) {
    if (metres < 0.0f) return "-";
    // Whole metres; in the thousands, grouped as the combat power is.
    const auto whole = static_cast<uint32_t>(std::min(std::lround(metres), 9'999'999L));
    return (whole == 0 ? std::string("0") : format_power(whole)) + " m";
}

PowerLayout power_layout(const combat_power::PowerSnapshot& power, const PowerSettings& settings) {
    PowerLayout layout;
    layout.distances = settings.sort_by_distance && power.has_local_position;
    layout.stale = layout.distances && power.local_position_stale;
    // An order by distance from where the local player stood a while ago is no order.
    const bool by_distance = layout.distances && !layout.stale;
    std::vector<combat_power::PowerRow> source = by_distance ? power.nearest : power.players;
    if (!settings.enemies_first) {
        // The snapshot's lists lead with the enemies: back to one order for all.
        std::stable_sort(source.begin(), source.end(), [&](const auto& a, const auto& b) {
            if (!by_distance) return a.combat_power > b.combat_power;
            if (a.has_distance() != b.has_distance()) return a.has_distance();
            return a.has_distance() && a.distance_m < b.distance_m;
        });
    }
    if (settings.show_list) {
        const size_t shown = std::min(source.size(), static_cast<size_t>(std::max(settings.list_max_players, 0)));
        layout.rows.assign(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(shown));
        layout.more = power.in_view > shown ? power.in_view - static_cast<uint32_t>(shown) : 0;
    }
    const bool listed = std::any_of(layout.rows.begin(), layout.rows.end(),
                                    [&](const combat_power::PowerRow& row) { return row.id == power.target.id; });
    if (listed) layout.target = power.target.id;
    else layout.target_line = power.target;
    // The bars stay combat power, against the strongest drawn, whatever the order.
    layout.top = layout.target_line.combat_power;
    for (const combat_power::PowerRow& row : layout.rows) layout.top = std::max(layout.top, row.combat_power);
    return layout;
}

} // namespace aion2::app

#ifdef HAVE_IMGUI

#include "app/game_icons.hpp"
#include "app/overlay_chrome.hpp"
#include "game/classes.hpp"

namespace aion2::app {

using chrome::rgba;
using chrome::u32;

namespace {

/// One player's row across the content width: a bar against the strongest, the
/// class emblem, the name, and the combat power at the right edge, with the
/// distance in a column right of the bar where the layout draws distances.
void draw_row(const combat_power::PowerRow& row, const PowerLayout& layout, bool target, float scale) {
    const uint32_t top = layout.top;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line = ImGui::GetTextLineHeight();
    const float row_h = line + 8.0f * scale;
    const float pad = 6.0f * scale;
    const float emblem = line + 2.0f * scale;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(width, row_h));
    if (width <= 0.0f) return;

    // A dead player's text fades: listed until they leave view, a threat no more.
    const float fade = row.dead ? 0.45f : 1.0f;
    // The distance has a column of its own, right of the bar, on the window's dark
    // ground where it reads plainly: as wide as a town's farthest, so the rows line
    // up. A "?", faded, while the local player's position is too old to measure
    // from: a number there would be a guess drawn as a fact.
    float bar_width = width;
    if (layout.distances) {
        const std::string distance = layout.stale ? std::string("?") : format_distance(row.distance_m);
        const float text_w = ImGui::CalcTextSize(distance.c_str()).x;
        const float inner = 6.0f * scale;
        const float column = std::max(ImGui::CalcTextSize("999 m").x, text_w) + 2.0f * inner;
        const float gap = 6.0f * scale;
        if (width - column - gap > 8.0f * line) {
            bar_width = width - column - gap;
            // A yellow badge as tall as the bar: a colour of its own, apart from the
            // names (white, an enemy's red) and the figures.
            const float alpha = (layout.stale ? 0.55f : 1.0f) * fade;
            const ImVec2 box_min(origin.x + width - column, origin.y);
            const ImVec2 box_max(origin.x + width, origin.y + row_h);
            const float rounding = 4.0f * scale;
            dl->AddRectFilled(box_min, box_max, u32(colors::Warn, 0.10f * alpha), rounding);
            dl->AddRect(box_min, box_max, u32(colors::Warn, 0.80f * alpha), rounding, 0, 1.25f * scale);
            const float dy = origin.y + (row_h - line) * 0.5f - 0.5f * scale;
            dl->AddText(ImVec2(box_min.x + (column - text_w) * 0.5f, dy), u32(chrome::lighten(colors::Warn, 0.15f), alpha),
                        distance.c_str());
        }
    }
    const ImVec2 row_min = origin;
    const ImVec2 row_max(origin.x + bar_width, origin.y + row_h);
    const float fraction =
        top != 0 ? std::clamp(static_cast<float>(row.combat_power) / static_cast<float>(top), 0.0f, 1.0f) : 0.0f;
    chrome::draw_bar(dl, row_min, row_max, fraction, class_color(row.cls), scale);
    // The target's row: a stripe down its left edge.
    if (target) {
        dl->AddRectFilled(row_min, ImVec2(row_min.x + 3.0f * scale, row_max.y), u32(colors::Live), 3.0f * scale,
                          ImDrawFlags_RoundCornersLeft);
    }

    const float ty = origin.y + (row_h - line) * 0.5f - 0.5f * scale;
    const int icon = class_icon(row.cls);
    float name_x = origin.x + pad;
    if (icon != kNoIcon) {
        draw_icon(dl, icon, name_x, origin.y + (row_h - emblem) * 0.5f, emblem);
        name_x += emblem + pad;
    }
    // A class with no emblem (the Brawler) keeps its letters.
    std::string label = row.name;
    if (icon == kNoIcon && row.cls != game::AionClass::Unknown) {
        label = std::string(game::class_short(row.cls)) + " " + label;
    }

    const std::string power = format_power_short(row.combat_power);
    float power_w = 0.0f;
    {
        const chrome::BoldScope bold;
        power_w = ImGui::CalcTextSize(power.c_str()).x;
        chrome::text_right(dl, row_max.x - pad, ty, u32(row.combat_power != 0 ? colors::Text : colors::TextDim, fade), power);
    }
    const float name_w = row_max.x - pad - power_w - pad - name_x;
    if (name_w <= 0.0f) return;
    // An enemy's name is red, lightened to read over a class's bar.
    const Rgb name_color = row.hostile ? chrome::lighten(colors::Live, 0.25f) : colors::Text;
    if (target) {
        const chrome::BoldScope bold;
        dl->AddText(ImVec2(name_x, ty), u32(name_color, fade), chrome::fit(label, name_w).c_str());
    } else {
        dl->AddText(ImVec2(name_x, ty), u32(name_color, (row.hostile ? 1.0f : 0.90f) * fade), chrome::fit(label, name_w).c_str());
    }
}

} // namespace

OverlayActions draw_power(const PowerFrameInput& in) {
    OverlayActions out;
    const PowerSettings defaults{};
    const PowerSettings& s = in.settings != nullptr ? *in.settings : defaults;
    const float scale = in.scale;
    const bool interactive = !s.locked;

    if (chrome::begin("##aion2power", s.opacity, scale, interactive)) {
        // Header, while it can be moved: a title and the buttons. Locked, the window
        // is only its rows.
        if (interactive) {
            ImGui::TextColored(rgba(colors::TextDim), "COMBAT POWER");
            chrome::HeaderButtons buttons;
            buttons.scale = scale;
            buttons.app_tooltip = "Open the app: the combat power's settings are on its page.";
            buttons.lock_tooltip = "Click-through and fixed in place. Unlock it in the app.";
            chrome::finish_header(buttons, out);
        }

        const combat_power::PowerSnapshot none{};
        const combat_power::PowerSnapshot& power = in.snapshot != nullptr ? in.snapshot->power : none;
        const PowerLayout layout = power_layout(power, s);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(chrome::item_spacing(scale).x, 3.0f * scale));
        if (layout.target_line.id != 0) {
            draw_row(layout.target_line, layout, true, scale);
        }
        for (const combat_power::PowerRow& row : layout.rows) {
            draw_row(row, layout, row.id == layout.target, scale);
        }
        ImGui::PopStyleVar();
        if (layout.more != 0) {
            ImGui::TextColored(rgba(colors::TextDim), "+%u more", layout.more);
        }
        // Nothing to show is said only while it can be moved: locked, an empty
        // overlay is out of the way.
        if (layout.empty() && interactive) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(rgba(colors::TextDim), "%s",
                               power.in_view == 0 ? "No players in view yet."
                                                  : "Use a skill on a player to see their combat power.");
            ImGui::PopTextWrapPos();
        }

        if (interactive) out.resize_grip = chrome::resize_grip(scale);
    }
    chrome::end();
    return out;
}

} // namespace aion2::app

#else // !HAVE_IMGUI

namespace aion2::app {

OverlayActions draw_power(const PowerFrameInput&) { return {}; }

} // namespace aion2::app

#endif
