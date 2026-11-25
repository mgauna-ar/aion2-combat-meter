// The Auras plugin's page: what its overlay lists, its watch list, and the overlay
// itself, in ffxiv-hub's order (the plugin's own settings, then its overlay's).

#include "app/ui/views.hpp"

#include "app/ui/icons.hpp"
#include "app/ui/page.hpp"
#include "app/ui/widgets.hpp"
#include "game/skill_names.hpp"

#ifdef HAVE_IMGUI
#include <algorithm>
#include <cctype>
#include <cstdio>
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
    const AuraSettings def{};
    setting_switch("Debuffs on you", "Enemies' effects on you, in red.", &a.show_debuffs, out);
    setting_switch("Yours on your target", "Your effects on the enemy you fight: the biggest one you hit lately.",
                   &a.show_target, out);
    if (advanced_section("##auras_effects_adv")) {
        setting_switch("Permanent effects", "Passive skills' effects, which never end.", &a.show_permanent, out);
        setting_slider("Shortest effect", "Shorter ones are left out: a dodge's mark, a cast's state.",
                       &a.min_duration_s, 0.0f, 10.0f, def.min_duration_s, "%.1f s", out);
    }
    end_settings_card();
}

void cooldowns_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##auras_cooldowns", ICON_HOURGLASS, "COOLDOWNS", colors::Warning);
    AuraSettings& a = in.settings->auras;
    const AuraSettings def{};
    setting_switch("Your cooldowns", "Estimates (~): the listed cooldown less your specialties, learned as you play.",
                   &a.show_cooldowns, out);
    if (advanced_section("##auras_cooldowns_adv")) {
        setting_slider("Shortest cooldown", "Shorter ones are left out: Dodge's, a combo's.", &a.min_cooldown_s, 0.0f,
                       30.0f, def.min_cooldown_s, "%.0f s", out);
    }
    end_settings_card();
}

void alerts_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##auras_alerts", ICON_BELL, "ALERTS", colors::Warning);
    AuraSettings& a = in.settings->auras;
    const AuraSettings def{};
    setting_switch("Chain skills", "Lights up a chain skill while the game offers it: Decisive Strike after Vicious Strike.",
                   &a.show_chains, out);
    setting_switch("Ready cooldowns",
                   "A cooldown coming back shows \"Ready\" for a moment; as icons, ready skills stay lit.",
                   &a.show_ready, out);
    setting_switch("Low HP", "Flags your HP under the threshold below, as a share of your max.", &a.low_hp_alert, out);
    if (!a.low_hp_alert) ImGui::BeginDisabled();
    setting_slider("Low HP under", "Your HP as a share of your max.", &a.low_hp_percent, 5, 90,
                   def.low_hp_percent, "%d%%", out);
    if (!a.low_hp_alert) ImGui::EndDisabled();
    end_settings_card();
}

/// One row of the seen card: an effect, and whether it is pinned, watched or hidden.
struct SeenChoice {
    uint32_t key{0};
    std::string label;
    bool hostile{false};
};

/// Adds or drops `key` in a list capped at kMaxWatchedEffects. False when full.
bool set_listed(std::vector<uint32_t>& list, uint32_t key, bool on) {
    const auto it = std::find(list.begin(), list.end(), key);
    if (!on) {
        if (it != list.end()) list.erase(it);
        return true;
    }
    if (it != list.end()) return true;
    if (list.size() >= kMaxWatchedEffects) return false;
    list.push_back(key);
    return true;
}

/// Drops `key` from the pins of `cls`, and the class's list once it is empty.
void unpin(AuraSettings& a, game::AionClass cls, uint32_t key) {
    const auto it = a.pinned.find(cls);
    if (it == a.pinned.end()) return;
    set_listed(it->second, key, false);
    if (it->second.empty()) a.pinned.erase(it);
}

/// A row the search keeps: part of its name, whatever the case, or of its number.
bool matches(const SeenChoice& c, const std::string& search) {
    if (search.empty()) return true;
    const auto lower = [](std::string text) {
        for (char& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return text;
    };
    return lower(c.label).find(lower(search)) != std::string::npos ||
           std::to_string(c.key).find(search) != std::string::npos;
}

void watch_card(const AppFrameInput& in, AppViewState& state, AppActions& out) {
    begin_settings_card("##auras_watch", ICON_CHECKLIST, "EFFECTS SEEN ON YOU", colors::Violet);
    AuraSettings& a = in.settings->auras;
    // Pins are the class's being played, which the plugin knows once its passives or
    // a press have come.
    const game::AionClass cls = in.snapshot != nullptr ? in.snapshot->auras.local_class : game::AionClass::Unknown;
    const bool class_known = cls != game::AionClass::Unknown;
    const std::string class_label(game::class_name(cls));
    ImGui::PushTextWrapPos(0.0f);
    if (class_known) {
        text_dim("Pin (%s): a place of its own on the overlay, lit while it is on you; while it is off, its "
                 "skill's cooldown when it has one. Once your %s has a pin, the overlay shows only pins and debuffs.",
                 class_label.c_str(), class_label.c_str());
    } else {
        text_dim("Pin: kept per class, which the page learns from your skills once you are in game.");
    }
    text_dim("Watch: flagged on the overlay while it is not on you (a Chanter's mantra, a scroll). "
             "Hide: left off the overlay (a proc that comes and goes all fight).");
    ImGui::PopTextWrapPos();

    // The effects seen on you this session, then any pinned, watched or hidden one not
    // seen yet.
    std::vector<SeenChoice> choices;
    if (in.snapshot != nullptr) {
        for (const auras::SeenEffect& e : in.snapshot->auras.seen) choices.push_back({e.key, e.label, e.hostile});
    }
    const auto add_unseen = [&](const std::vector<uint32_t>& keys) {
        for (const uint32_t key : keys) {
            const bool listed =
                std::any_of(choices.begin(), choices.end(), [&](const SeenChoice& c) { return c.key == key; });
            if (listed) continue;
            choices.push_back({key, game::effect_key_label(key), false});
        }
    };
    add_unseen(pins_for(a, cls));
    add_unseen(a.watch);
    add_unseen(a.hidden);
    if (choices.empty()) {
        text_dim("Effects show up here once the plugin has seen them on you.");
    } else {
        char search[64] = {};
        std::snprintf(search, sizeof(search), "%s", state.aura_search.c_str());
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::InputTextWithHint("##auras_search", ICON_SEARCH " Search by name or number", search,
                                     sizeof(search))) {
            state.aura_search = search;
        }
    }

    const float gap = m(6.0f);
    const float chips = toggle_chip_width("Pin") + gap + toggle_chip_width("Watch") + gap + toggle_chip_width("Hide");
    for (const SeenChoice& c : choices) {
        if (!matches(c, state.aura_search)) continue;
        ImGui::PushID(static_cast<int>(c.key));
        const std::vector<uint32_t>& pins = pins_for(a, cls);
        bool pinned = std::find(pins.begin(), pins.end(), c.key) != pins.end();
        bool watched = std::find(a.watch.begin(), a.watch.end(), c.key) != a.watch.end();
        bool hidden = std::find(a.hidden.begin(), a.hidden.end(), c.key) != a.hidden.end();
        ImGui::AlignTextToFramePadding();
        if (c.hostile) {
            text_colored_u32(colors::Danger, "%s", c.label.c_str());
        } else {
            ImGui::TextUnformatted(c.label.c_str());
        }
        // The key, so an effect with no name can be told from another, and reported.
        ImGui::SameLine();
        text_dim("%u", c.key);
        ImGui::SameLine();
        right_align(chips);
        bool changed = false;
        // An enemy's debuff is nothing to pin, nor to watch for.
        if (c.hostile && !pinned) {
            ImGui::Dummy(ImVec2(toggle_chip_width("Pin"), 0.0f));
        } else {
            if (!class_known) ImGui::BeginDisabled();
            if (toggle_chip("Pin", &pinned)) {
                if (pinned) {
                    if (!set_listed(a.pinned[cls], c.key, true)) pinned = false;
                    // Pinned is shown and hidden is not: one or the other.
                    if (pinned) set_listed(a.hidden, c.key, false);
                } else {
                    unpin(a, cls, c.key);
                }
                changed = true;
            }
            if (!class_known) ImGui::EndDisabled();
        }
        ImGui::SameLine(0.0f, gap);
        if (c.hostile && !watched) {
            ImGui::Dummy(ImVec2(toggle_chip_width("Watch"), 0.0f));
        } else if (toggle_chip("Watch", &watched)) {
            if (!set_listed(a.watch, c.key, watched)) watched = false;
            changed = true;
        }
        ImGui::SameLine(0.0f, gap);
        if (toggle_chip("Hide", &hidden)) {
            if (!set_listed(a.hidden, c.key, hidden)) hidden = false;
            if (hidden && class_known) unpin(a, cls, c.key);
            changed = true;
        }
        if (changed) {
            out.settings_changed = true;
            out.save_settings = true;
        }
        ImGui::PopID();
    }
    end_settings_card();
}

void overlay_card(const AppFrameInput& in, AppActions& out) {
    AuraSettings& a = in.settings->auras;
    const AuraSettings def{};
    OverlayControls c;
    c.id = "##auras_overlay";
    c.locked = &a.locked;
    c.opacity = &a.opacity;
    c.default_opacity = def.opacity;
    c.scale = &a.scale;
    c.hide_when_game_unfocused = &a.hide_when_game_unfocused;
    c.show_in_dungeons_only = &a.show_in_dungeons_only;
    c.reset_position = &out.reset_aura_position;
    c.position_help = "Back to its spot under the meter overlay's.";
    c.look = [&] {
        setting_switch("Icons", "Icons in a grid, with their time left, instead of bars.", &a.icons, out,
                       Applies::Overlay);
        if (!a.icons) ImGui::BeginDisabled();
        setting_slider("Icon size", "In pixels, before the scale.", &a.icon_size, kMinAuraIcon, kMaxAuraIcon,
                       def.icon_size, "%d px", out, Applies::Overlay);
        if (!a.icons) ImGui::EndDisabled();
    };
    overlay_settings_card(c, out);
}

} // namespace

void render_auras_page(const AppFrameInput& in, AppViewState& state, AppActions& out) {
    render_plugin_header(in, out, plugins::PluginId::Auras, plugin_icon(plugins::PluginId::Auras),
                         "Your buffs, debuffs and cooldowns, in an overlay of their own");
    if (in.settings == nullptr || render_plugin_disabled_gate(in, out, plugins::PluginId::Auras)) return;
    const SettingsSection sections[] = {
        {[&] { effects_card(in, out); }}, {[&] { overlay_card(in, out); }}, {[&] { alerts_card(in, out); }},
        {[&] { watch_card(in, state, out); }}, {[&] { cooldowns_card(in, out); }},
    };
    render_settings_grid(sections, std::size(sections));
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
