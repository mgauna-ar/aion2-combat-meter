#include "app/power_view.hpp"

#include "app/display_format.hpp"
#include "app/imgui_guard.hpp"

#include <algorithm>
#include <cstddef>

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

PowerLayout power_layout(const combat_power::PowerSnapshot& power, const PowerSettings& settings) {
    PowerLayout layout;
    if (settings.show_list) {
        const size_t shown = std::min(power.players.size(), static_cast<size_t>(std::max(settings.list_max_players, 0)));
        layout.rows.assign(power.players.begin(), power.players.begin() + static_cast<std::ptrdiff_t>(shown));
        layout.more = power.in_view > shown ? power.in_view - static_cast<uint32_t>(shown) : 0;
    }
    const bool listed = std::any_of(layout.rows.begin(), layout.rows.end(),
                                    [&](const combat_power::PowerRow& row) { return row.id == power.target.id; });
    if (listed) layout.target = power.target.id;
    else layout.target_line = power.target;
    // Strongest first, as the tracker sorts them; the target may be stronger still.
    layout.top = std::max(layout.rows.empty() ? 0u : layout.rows.front().combat_power, layout.target_line.combat_power);
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
/// class emblem, the name, and the combat power at the right edge.
void draw_row(const combat_power::PowerRow& row, uint32_t top, bool target, float scale) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line = ImGui::GetTextLineHeight();
    const float row_h = line + 8.0f * scale;
    const float pad = 6.0f * scale;
    const float emblem = line + 2.0f * scale;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(width, row_h));
    if (width <= 0.0f) return;

    const ImVec2 row_min = origin;
    const ImVec2 row_max(origin.x + width, origin.y + row_h);
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

    const std::string power = format_power(row.combat_power);
    float power_w = 0.0f;
    {
        const chrome::BoldScope bold;
        power_w = ImGui::CalcTextSize(power.c_str()).x;
        chrome::text_right(dl, row_max.x - pad, ty, u32(row.combat_power != 0 ? colors::Text : colors::TextDim), power);
    }
    const float name_w = row_max.x - pad - power_w - pad - name_x;
    if (name_w <= 0.0f) return;
    if (target) {
        const chrome::BoldScope bold;
        dl->AddText(ImVec2(name_x, ty), u32(colors::Text), chrome::fit(label, name_w).c_str());
    } else {
        dl->AddText(ImVec2(name_x, ty), u32(colors::Text, 0.90f), chrome::fit(label, name_w).c_str());
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
            draw_row(layout.target_line, layout.top, true, scale);
        }
        for (const combat_power::PowerRow& row : layout.rows) {
            draw_row(row, layout.top, row.id == layout.target, scale);
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
