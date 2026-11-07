// The Auras plugin's page: what its overlay lists, its watch list, and the overlay
// itself, in ffxiv-hub's order (the plugin's own settings, then its overlay's).

#include "app/ui/views.hpp"

#include "app/ui/icons.hpp"
#include "app/ui/page.hpp"
#include "app/ui/widgets.hpp"
#include "game/skill_names.hpp"

#ifdef HAVE_IMGUI
#include <algorithm>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
#endif

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

namespace {

void effects_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##auras_effects", ICON_SPARKLE, "EFFECTS", colors::Violet);
    AuraSettings& a = in.settings->auras;
    setting_switch("Debuffs on you", "Enemies' effects on you, in red.", &a.show_debuffs, out);
    setting_switch("Yours on your target", "Your effects on the enemy you fight: the biggest one you hit lately.",
                   &a.show_target, out);
    setting_switch("Permanent effects", "Passive skills' effects, which never end.", &a.show_permanent, out);
    setting_slider("Shortest effect", "Shorter ones are left out: a dodge's mark, a cast's state.", &a.min_duration_s,
                   0.0f, 10.0f, "%.1f s", out);
    end_settings_card();
}

void cooldowns_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##auras_cooldowns", ICON_HOURGLASS, "COOLDOWNS", colors::Warning);
    AuraSettings& a = in.settings->auras;
    setting_switch("Your cooldowns", "Estimates (~): the listed cooldown less your specialties, learned as you play.",
                   &a.show_cooldowns, out);
    setting_slider("Shortest cooldown", "Shorter ones are left out: Dodge's, a combo's.", &a.min_cooldown_s, 0.0f, 30.0f,
                   "%.0f s", out);
    end_settings_card();
}

void alerts_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##auras_alerts", ICON_BELL, "ALERTS", colors::Warning);
    AuraSettings& a = in.settings->auras;
    setting_switch("Chain skills", "Lights up a chain skill while the game offers it: Decisive Strike after Vicious Strike.",
                   &a.show_chains, out);
    setting_switch("Ready cooldowns",
                   "A cooldown coming back shows \"Ready\" for a moment; as icons, ready skills stay lit.",
                   &a.show_ready, out);
    setting_switch("Low HP", "Flags your HP under the threshold below, as a share of your max.", &a.low_hp_alert, out);
    if (!a.low_hp_alert) ImGui::BeginDisabled();
    setting_slider("Low HP under", "Your HP as a share of your max.", &a.low_hp_percent, 5, 90, "%d%%", out);
    if (!a.low_hp_alert) ImGui::EndDisabled();
    end_settings_card();
}

void watch_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##auras_watch", ICON_CHECKLIST, "WATCH LIST", colors::Violet);
    AuraSettings& a = in.settings->auras;
    ImGui::PushTextWrapPos(0.0f);
    text_dim("Flagged on the aura overlay while they are not on you: a Chanter's mantra, a buff you keep up.");
    ImGui::PopTextWrapPos();

    // The effects seen on you this session, then any watched one not seen yet.
    std::vector<std::pair<uint32_t, std::string>> choices;
    if (in.snapshot != nullptr) {
        for (const auras::SeenEffect& e : in.snapshot->auras.seen) choices.emplace_back(e.key, e.label);
    }
    for (const uint32_t key : a.watch) {
        const bool listed = std::any_of(choices.begin(), choices.end(), [&](const auto& c) { return c.first == key; });
        if (listed) continue;
        const std::string_view name = game::skill_name(key);
        choices.emplace_back(key, name.empty() ? "Effect " + std::to_string(key) : std::string(name));
    }
    if (choices.empty()) text_dim("Effects show up here once the plugin has seen them on you.");
    for (const auto& [key, label] : choices) {
        const auto it = std::find(a.watch.begin(), a.watch.end(), key);
        bool watched = it != a.watch.end();
        ImGui::PushID(static_cast<int>(key));
        if (setting_switch(label.c_str(), nullptr, &watched, out)) {
            if (!watched) {
                a.watch.erase(std::find(a.watch.begin(), a.watch.end(), key));
            } else if (a.watch.size() < kMaxWatchedEffects) {
                a.watch.push_back(key);
            }
        }
        ImGui::PopID();
    }
    end_settings_card();
}

void overlay_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##auras_overlay", ICON_MONITOR, "IN-GAME OVERLAY", colors::Accent);
    AuraSettings& a = in.settings->auras;
    setting_switch("Lock", "Click-through and fixed in place. Ctrl+Alt+L locks or unlocks every overlay.", &a.locked,
                   out, Applies::Overlay);
    setting_switch("Icons", "Icons in a grid, with their time left, instead of bars.", &a.icons, out, Applies::Overlay);
    if (!a.icons) ImGui::BeginDisabled();
    setting_slider("Icon size", "In pixels, before the scale.", &a.icon_size, kMinAuraIcon, kMaxAuraIcon, "%d px", out,
                   Applies::Overlay);
    if (!a.icons) ImGui::EndDisabled();
    setting_slider("Opacity", "Of its background; the bars stay solid.", &a.opacity, 0.0f, 1.0f, "%.2f", out,
                   Applies::Overlay);
    setting_slider("Scale", "On top of the monitor's own scaling.", &a.scale, 0.6f, 2.5f, "%.2fx", out,
                   Applies::Overlay);
    begin_setting_row("Position", "Back to its spot under the meter overlay's.");
    if (button(ICON_RESET "  Reset position##auras", ButtonKind::Secondary, ButtonSize::Fit)) {
        out.reset_aura_position = true;
    }
    end_setting_row();
    end_settings_card();
}

void visibility_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##auras_visibility", ICON_EYE, "OVERLAY VISIBILITY", colors::Violet);
    AuraSettings& a = in.settings->auras;
    // Conditions apply only while locked, so the overlay can always be dragged back.
    if (!a.locked) ImGui::BeginDisabled();
    setting_switch("Hide when the game is not in front", "Hidden while another program has the focus.",
                   &a.hide_when_game_unfocused, out);
    setting_switch("Only in dungeons", "Hidden in the open world, and on maps the meter can't name.",
                   &a.show_in_dungeons_only, out);
    if (!a.locked) ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0.0f, m(2.0f)));
    ImGui::PushTextWrapPos(0.0f);
    if (!a.locked) {
        text_colored_u32(colors::WarningLight, ICON_WARNING "  Lock the overlay for these to apply.");
    } else {
        text_dim("They apply while the overlay is locked; unlocked, it always shows.");
    }
    ImGui::PopTextWrapPos();
    end_settings_card();
}

} // namespace

void render_auras_page(const AppFrameInput& in, AppViewState&, AppActions& out) {
    render_plugin_header(in, out, plugins::PluginId::Auras, plugin_icon(plugins::PluginId::Auras),
                         "Your buffs, debuffs and cooldowns, in an overlay of their own");
    if (in.settings == nullptr || render_plugin_disabled_gate(in, out, plugins::PluginId::Auras)) return;
    const SettingsSection sections[] = {
        {[&] { effects_card(in, out); }}, {[&] { cooldowns_card(in, out); }},   {[&] { alerts_card(in, out); }},
        {[&] { watch_card(in, out); }},   {[&] { overlay_card(in, out); }},      {[&] { visibility_card(in, out); }},
    };
    render_settings_grid(sections, std::size(sections));
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
