// The Combat Power plugin's page: the target and the map's players now, what its
// overlay shows, and the overlay itself, in ffxiv-hub's order (the plugin's own
// settings, then its overlay's).

#include "app/ui/views.hpp"

#include "app/power_view.hpp"
#include "app/ui/icons.hpp"
#include "app/ui/page.hpp"
#include "app/ui/widgets.hpp"

#ifdef HAVE_IMGUI
#include <iterator>
#include <string>
#endif

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

namespace {

/// The target, the strongest player on the map and how many are in view, as the
/// overlay has them.
void figures(const AppFrameInput& in) {
    const combat_power::PowerSnapshot none{};
    const combat_power::PowerSnapshot& p = in.snapshot != nullptr ? in.snapshot->power : none;

    const bool has_target = p.target.id != 0;
    const std::string target_value = has_target ? format_power(p.target.combat_power) : std::string("-");
    const std::string target_note = has_target ? p.target.name : std::string("Use a skill on a player");
    // The second tile follows the list's order: the strongest, or the nearest.
    const bool by_distance = in.settings->power.sort_by_distance && p.has_local_position && !p.nearest.empty();
    const bool listed = !p.players.empty();
    const combat_power::PowerRow first = by_distance ? p.nearest.front() : listed ? p.players.front() : combat_power::PowerRow{};
    const std::string top_value =
        by_distance ? format_distance(first.distance_m) : listed ? format_power(first.combat_power) : std::string("-");
    const std::string top_note = listed ? first.name : std::string("No players in view yet");
    const std::string in_view = std::to_string(p.in_view);

    const StatTileSpec tiles[] = {
        {"##tile_power_target", ICON_CROSSHAIR, "TARGET", target_value.c_str(),
         has_target ? colors::TextPrimary : colors::TextDim, target_note.c_str(), colors::Danger},
        {"##tile_power_top", ICON_TRENDING, by_distance ? "NEAREST" : "STRONGEST", top_value.c_str(),
         listed ? colors::TextPrimary : colors::TextDim, top_note.c_str(), colors::Warning},
        {"##tile_power_view", ICON_EYE, "IN VIEW", in_view.c_str(), colors::TextPrimary, "Other players on this map",
         colors::Accent},
    };
    const char* const tooltips[] = {
        "The last player you used a skill on, for half a minute after it.",
        by_distance ? "The player closest to where you last used a skill or entered the map."
                    : "The highest combat power among the players the game has shown you on this map.",
        "Players the game has shown you on this map. The overlay lists the strongest of them.",
    };
    stat_tile_row(tiles, std::size(tiles), tooltips);
    ImGui::Dummy(ImVec2(0.0f, m(metrics::Gutter)));
}

void display_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##power_display", ICON_GAUGE, "WHAT IT SHOWS", colors::Violet);
    PowerSettings& p = in.settings->power;
    const PowerSettings def{};
    setting_switch("List the map's players",
                   "The players on the map with you, strongest first, with your target marked: an arena's opponents, "
                   "a dungeon's party, a town's strongest. Off, only your target shows.",
                   &p.show_list, out, Applies::Overlay);
    setting_slider("List up to", "In a crowd, the strongest this many, and a count of the rest.",
                   &p.list_max_players, kMinPowerList, kMaxPowerList, def.list_max_players, "%d players", out,
                   Applies::Overlay);
    setting_switch("Sort by distance",
                   "The nearest players first, each with how far they are. The game does not send you your own "
                   "steps, so distances are measured from where you last used a skill or entered the map, and dim "
                   "once that is a few seconds old.",
                   &p.sort_by_distance, out, Applies::Overlay);
    end_settings_card();
}

void source_card() {
    begin_settings_card("##power_how", ICON_INFO, "WHERE IT COMES FROM", colors::Accent);
    ImGui::PushTextWrapPos(0.0f);
    text_dim("The game sends each player's combat power as they come into view, the figure their character page "
             "shows. The plugin reads it there: nothing is sent, and nothing is looked up anywhere. A player already "
             "beside you when the meter started shows once the game sends them again, on the next map. Your own "
             "combat power is not sent that way, so it is not shown.");
    ImGui::PopTextWrapPos();
    end_settings_card();
}

void overlay_card(const AppFrameInput& in, AppActions& out) {
    PowerSettings& p = in.settings->power;
    const PowerSettings def{};
    OverlayControls c;
    c.id = "##power_overlay";
    c.locked = &p.locked;
    c.opacity = &p.opacity;
    c.default_opacity = def.opacity;
    c.scale = &p.scale;
    c.hide_when_game_unfocused = &p.hide_when_game_unfocused;
    c.show_in_dungeons_only = &p.show_in_dungeons_only;
    c.reset_position = &out.reset_power_position;
    c.position_help = "Back to its spot beside the ping overlay's.";
    overlay_settings_card(c, out);
}

} // namespace

void render_power_page(const AppFrameInput& in, AppViewState&, AppActions& out) {
    render_plugin_header(in, out, plugins::PluginId::CombatPower, plugin_icon(plugins::PluginId::CombatPower),
                         "The combat power of the player you are fighting, as the game sends it");
    if (in.settings == nullptr || render_plugin_disabled_gate(in, out, plugins::PluginId::CombatPower)) return;
    figures(in);
    const SettingsSection sections[] = {
        {[&] { display_card(in, out); }},
        {[&] { overlay_card(in, out); }},
        {[&] { source_card(); }},
    };
    render_settings_grid(sections, std::size(sections));
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
