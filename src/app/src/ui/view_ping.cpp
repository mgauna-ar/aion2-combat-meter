// The Ping plugin's page: the figures now, what its overlay shows, and the overlay
// itself, in ffxiv-hub's order (the plugin's own settings, then its overlay's).

#include "app/ui/views.hpp"

#include "app/ping_view.hpp"
#include "app/ui/icons.hpp"
#include "app/ui/page.hpp"
#include "app/ui/widgets.hpp"

#ifdef HAVE_IMGUI
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <optional>
#include <string>
#endif

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

namespace {

uint32_t level_color(PingLevel level) {
    switch (level) {
        case PingLevel::Good: return colors::SuccessLight;
        case PingLevel::Warn: return colors::WarningLight;
        case PingLevel::Bad: return colors::DangerLight;
    }
    return colors::TextPrimary;
}

/// The ping, the jitter and the last five minutes' range, as the overlay has them.
void figures(const AppFrameInput& in) {
    const ping::PingSnapshot none{};
    const ping::PingSnapshot& p = in.snapshot != nullptr ? in.snapshot->ping : none;
    const Micros now = in.snapshot != nullptr ? in.snapshot->now : 0;
    const PingSettings& s = in.settings->ping;

    const std::optional<float> ping = shown_ping(p, now);
    const std::string ping_value = ping ? format_ms(*ping) : std::string("-");
    const char* ping_note = ping ? "The game's own, every 10 s" : ping_status(p, now);
    const std::string jitter_value = p.has_jitter ? format_ms(p.jitter_ms) : std::string("-");
    std::string range = "-";
    std::string range_note = "Over the last 5 minutes";
    if (p.samples > 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.0f - %.0f ms", static_cast<double>(p.min_ms), static_cast<double>(p.max_ms));
        range = buf;
        std::snprintf(buf, sizeof(buf), "Average %.0f ms over 5 minutes", static_cast<double>(p.avg_ms));
        range_note = buf;
    }
    const StatTileSpec tiles[] = {
        {"##tile_ping", ICON_ACTIVITY, "PING", ping_value.c_str(),
         ping ? level_color(ping_level(*ping, s)) : colors::TextDim, ping_note, colors::Accent},
        {"##tile_jitter", ICON_TRENDING, "JITTER", jitter_value.c_str(), colors::TextPrimary,
         "How unevenly the heartbeats arrive", colors::Warning},
        {"##tile_range", ICON_CLOCK, "RANGE", range.c_str(), colors::TextPrimary, range_note.c_str(), colors::Violet},
    };
    const char* const tooltips[] = {
        "The round trip of the game's own ping, read off the server's answer. Nothing is sent.",
        "The gap between two heartbeats' arrivals against the gap between their server times, smoothed.",
        "The least and the most of the pings measured in the last five minutes.",
    };
    stat_tile_row(tiles, std::size(tiles), tooltips);
    ImGui::Dummy(ImVec2(0.0f, m(metrics::Gutter)));
}

void display_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##ping_display", ICON_GAUGE, "WHAT IT SHOWS", colors::Violet);
    PingSettings& p = in.settings->ping;
    const PingSettings def{};
    setting_switch("Jitter", "How unevenly the heartbeats arrive: it moves within a second of a lag spike.",
                   &p.show_jitter, out, Applies::Overlay);
    setting_switch("Graph", "The last two minutes: the ping as a line, each second's lateness as bars, and the range.",
                   &p.show_graph, out, Applies::Overlay);
    if (setting_slider("Amber from", "A ping this high or higher shows in amber.", &p.warn_ms, kMinPingThreshold,
                       kMaxPingThreshold, def.warn_ms, "%d ms", out, Applies::Overlay)) {
        p.bad_ms = std::max(p.bad_ms, p.warn_ms);
    }
    if (setting_slider("Red from", "A ping this high or higher shows in red.", &p.bad_ms, kMinPingThreshold,
                       kMaxPingThreshold, def.bad_ms, "%d ms", out, Applies::Overlay)) {
        p.warn_ms = std::min(p.warn_ms, p.bad_ms);
    }
    end_settings_card();
}

void measure_card(const AppFrameInput& in) {
    begin_settings_card("##ping_how", ICON_INFO, "HOW IT IS MEASURED", colors::Accent);
    ImGui::PushTextWrapPos(0.0f);
    text_dim("The game pings its server every 10 seconds, and the answer carries the moment the game asked. The "
             "plugin reads that answer and sends nothing, so the ping updates every 10 seconds. Between two answers, "
             "the heartbeats (20 a second) give the jitter and the graph's bars.");
    const ping::PingSnapshot none{};
    const ping::PingSnapshot& p = in.snapshot != nullptr ? in.snapshot->ping : none;
    if (p.state == ping::PingState::ClockRejected) {
        ImGui::Dummy(ImVec2(0.0f, m(2.0f)));
        text_colored_u32(colors::WarningLight,
                         ICON_WARNING "  The game's clock is not the one this version expects: its last answer read as "
                                      "%.0f ms. No ping is shown; the jitter and the bars still are.",
                         p.last_rejected_ms);
    } else if (p.state == ping::PingState::NoCounter) {
        ImGui::Dummy(ImVec2(0.0f, m(2.0f)));
        text_colored_u32(colors::WarningLight,
                         ICON_WARNING "  A capture played back has no ping, only the jitter and the bars.");
    }
    ImGui::PopTextWrapPos();
    end_settings_card();
}

void overlay_card(const AppFrameInput& in, AppActions& out) {
    PingSettings& p = in.settings->ping;
    const PingSettings def{};
    OverlayControls c;
    c.id = "##ping_overlay";
    c.locked = &p.locked;
    c.opacity = &p.opacity;
    c.default_opacity = def.opacity;
    c.scale = &p.scale;
    c.hide_when_game_unfocused = &p.hide_when_game_unfocused;
    c.show_in_dungeons_only = &p.show_in_dungeons_only;
    c.reset_position = &out.reset_ping_position;
    c.position_help = "Back to its spot beside the meter overlay's.";
    overlay_settings_card(c, out);
}

} // namespace

void render_ping_page(const AppFrameInput& in, AppViewState&, AppActions& out) {
    render_plugin_header(in, out, plugins::PluginId::Ping, plugin_icon(plugins::PluginId::Ping),
                         "Your latency to the game server, read off the game's own pings");
    if (in.settings == nullptr || render_plugin_disabled_gate(in, out, plugins::PluginId::Ping)) return;
    figures(in);
    const SettingsSection sections[] = {
        {[&] { display_card(in, out); }},
        {[&] { overlay_card(in, out); }},
        {[&] { measure_card(in); }},
    };
    render_settings_grid(sections, std::size(sections));
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
