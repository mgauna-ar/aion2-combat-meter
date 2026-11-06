// The frame every plugin page opens with: its header and master switch, and the
// panel that stands in for the page while the plugin is off. Ported from
// ffxiv-hub's plugin pages.

#include "app/ui/icons.hpp"
#include "app/ui/theme.hpp"
#include "app/ui/views.hpp"
#include "app/ui/widgets.hpp"

#ifdef HAVE_IMGUI
#include <algorithm>
#include <cstdio>
#include <string>
#endif

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

namespace {

/// The settings, saved now, and the engine and the overlays told.
void set_enabled(const AppFrameInput& in, AppActions& out, plugins::PluginId id, bool on) {
    in.settings->plugins.set(id, on);
    out.settings_changed = true;
    out.save_settings = true;
    out.engine_changed = true;
    out.overlay_changed = true;
}

/// The caption and the switch. Measured against the longer caption, so the slot
/// does not change width as the switch flips.
float header_action_width() { return ImGui::CalcTextSize("Disabled").x + m(8.0f) + ImGui::GetFrameHeight() * 1.52f; }

} // namespace

const char* plugin_icon(plugins::PluginId id) noexcept {
    return id == plugins::PluginId::CombatMeter ? ICON_SWORDS : ICON_SPARKLE;
}

AppPage plugin_page(plugins::PluginId id) noexcept {
    return id == plugins::PluginId::CombatMeter ? AppPage::Meter : AppPage::Auras;
}

void render_plugin_header(const AppFrameInput& in, AppActions& out, plugins::PluginId id, const char* icon,
                          const char* subtitle) {
    const plugins::PluginDescriptor& d = plugins::find(id);
    const bool enabled = in.settings != nullptr && in.settings->plugins.enabled(id);
    begin_page_header(icon, d.name, subtitle, in.settings != nullptr ? header_action_width() : 0.0f);
    if (in.settings != nullptr) {
        // The caption centred against the switch, which is taller than a line of text.
        const float switch_h = ImGui::GetFrameHeight() * 0.82f;
        const float switch_y = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(switch_y + std::max((switch_h - ImGui::GetTextLineHeight()) * 0.5f, 0.0f));
        text_colored_u32(enabled ? colors::TextMuted : colors::TextDim, enabled ? "Enabled" : "Disabled");
        ImGui::SameLine(0.0f, m(8.0f));
        ImGui::SetCursorPosY(switch_y);
        bool value = enabled;
        char toggle_id[32];
        std::snprintf(toggle_id, sizeof(toggle_id), "##plugin_switch_%u", static_cast<unsigned>(id));
        if (toggle(toggle_id, &value)) set_enabled(in, out, id, value);
    }
    end_page_header();
    ImGui::Dummy(ImVec2(0.0f, m(4.0f)));
}

bool render_plugin_disabled_gate(const AppFrameInput& in, AppActions& out, plugins::PluginId id) {
    if (in.settings == nullptr || in.settings->plugins.enabled(id)) return false;
    const plugins::PluginDescriptor& d = plugins::find(id);
    begin_card("##PluginDisabledPanel", ImVec2(0.0f, fill_h(0.0f)));
    const std::string hint = std::string(d.name) + " is switched off: it reads nothing and draws no overlay.";
    empty_state(ICON_POWER, "Plugin disabled", hint.c_str());
    // Centred under the empty state, so the way back is where the eye already is.
    const std::string label = std::string(ICON_PLAY "  Enable ") + d.name;
    const float button_w = button_width(label.c_str(), ButtonSize::Large);
    ImGui::Dummy(ImVec2(0.0f, m(8.0f)));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max((ImGui::GetContentRegionAvail().x - button_w) * 0.5f, 0.0f));
    if (button(label.c_str(), ButtonKind::Primary, ButtonSize::Large)) set_enabled(in, out, id, true);
    end_card();
    return true;
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
