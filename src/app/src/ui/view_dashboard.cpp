#include "app/ui/views.hpp"

#include "app/display_format.hpp"
#include "app/ui/icons.hpp"
#include "app/ui/page.hpp"
#include "app/ui/widgets.hpp"
#include "game/map_names.hpp"

#ifdef HAVE_IMGUI
#include <cinttypes>
#include <cstdio>
#include <string>
#include <string_view>
#endif

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

namespace {

/// A settings row whose control column holds a value, not a control.
void info_row(const char* label, const std::string& value, uint32_t color = colors::TextPrimary) {
    begin_setting_row(label, nullptr);
    text_colored_u32(color, "%s", value.c_str());
    end_setting_row();
}

std::string count(uint64_t n) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%" PRIu64, n);
    return buf;
}

struct CaptureLook {
    const char* label;
    const char* detail;
    uint32_t color;
};

CaptureLook capture_look(const pipeline::LiveSnapshot* s) {
    if (s == nullptr) return {"Starting", "The capture is starting", colors::TextDim};
    switch (s->capture) {
        case pipeline::CaptureState::Live: return {"Live", "Game traffic is arriving", colors::SuccessLight};
        case pipeline::CaptureState::Quiet: return {"Quiet", "The game's traffic stopped", colors::WarningLight};
        case pipeline::CaptureState::Waiting: break;
    }
    return {"Waiting", "No game traffic yet", colors::TextMuted};
}

void capture_card(const AppFrameInput& in) {
    begin_settings_card("##dash_capture", ICON_ACTIVITY, "CAPTURE", colors::Accent);
    ImGui::PushTextWrapPos(0.0f);
    text_colored_u32(colors::TextBody, "%s", in.source_status.empty() ? "Starting capture..." : in.source_status.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.0f, m(4.0f)));
    if (in.snapshot != nullptr) {
        const auto& p = in.snapshot->parse;
        const auto& q = in.snapshot->capture_totals;
        char coverage[48];
        std::snprintf(coverage, sizeof(coverage), "%" PRIu64 " (%.1f%%)", p.decoded, p.coverage_percent());
        info_row("Frames decoded", coverage);
        info_row("Parse failures", count(p.failed), p.failed > 0 ? colors::WarningLight : colors::TextPrimary);
        info_row("Bytes lost to gaps", count(q.gap_bytes), q.gap_bytes > 0 ? colors::WarningLight : colors::TextPrimary);
        info_row("Resyncs", count(q.resyncs));
        info_row("Dropped packets", count(q.dropped_packets),
                 q.dropped_packets > 0 ? colors::WarningLight : colors::TextPrimary);
    }
    end_settings_card();
}

void recording_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##dash_rec", ICON_DOWNLOAD, "RECORDING", colors::Danger);
    ImGui::PushTextWrapPos(0.0f);
    if (in.recording) {
        text_colored_u32(colors::DangerLight, "Recording to %s", in.recording_path.c_str());
    } else {
        text_muted("Saves the game's traffic to a .pcapng, for aion2meter-replay and for fixing the decoder.");
    }
    text_dim("Recordings hold character names: share them with care.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.0f, m(4.0f)));
    if (button(in.recording ? ICON_CIRCLE_X "  Stop recording" : ICON_PLAY "  Record",
               in.recording ? ButtonKind::Danger : ButtonKind::Primary)) {
        out.toggle_recording = true;
    }
    same_line_if_room(button_width(ICON_FOLDER "  Open recordings"));
    if (button(ICON_FOLDER "  Open recordings")) out.open = OpenTarget::Recordings;
    end_settings_card();
}

void files_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##dash_files", ICON_FOLDER, "FILES", colors::Violet);
    ImGui::PushTextWrapPos(0.0f);
    text_dim("Settings, saved fights, recordings and the log live in");
    text_colored_u32(colors::TextBody, "%s", in.data_dir.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.0f, m(4.0f)));
    if (button(ICON_FOLDER "  Open folder")) out.open = OpenTarget::DataFolder;
    same_line_if_room(button_width(ICON_HISTORY "  Saved fights"));
    if (button(ICON_HISTORY "  Saved fights")) out.open = OpenTarget::History;
    if (button(ICON_FILE "  config.json")) out.open = OpenTarget::Config;
    same_line_if_room(button_width(ICON_TERMINAL "  Log"));
    if (button(ICON_TERMINAL "  Log")) out.open = OpenTarget::Log;
    end_settings_card();
}

void plugins_card(const AppFrameInput& in, AppViewState& state) {
    begin_settings_card("##dash_plugins", ICON_PLUG, "PLUGINS", colors::Accent);
    for (const plugins::PluginDescriptor& d : plugins::ALL) {
        const bool on = in.settings != nullptr && in.settings->plugins.enabled(d.id);
        ImGui::PushID(static_cast<int>(d.id));
        begin_setting_row(d.name, d.description);
        pill(on ? "On" : "Off", on ? colors::Success : colors::TextDim);
        ImGui::SameLine(0.0f, m(8.0f));
        if (button(plugin_icon(d.id), ButtonKind::Secondary, ButtonSize::Icon)) state.page = plugin_page(d.id);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open %s", d.name);
        end_setting_row();
        ImGui::PopID();
    }
    end_settings_card();
}

void safety_card() {
    begin_settings_card("##dash_safety", ICON_SHIELD, "PASSIVE ONLY", colors::Success);
    ImGui::PushTextWrapPos(0.0f);
    text_muted("The meter reads the game's traffic as it reaches this PC and draws its own windows.");
    text_dim("It never opens the game's process, reads its memory, injects or hooks anything, or sends packets "
             "or input. Its one connection is the update check, to GitHub, which Settings can turn off.");
    ImGui::PopTextWrapPos();
    end_settings_card();
}

} // namespace

void render_dashboard(const AppFrameInput& in, AppViewState& state, AppActions& out) {
    page_header(ICON_DASHBOARD, "Dashboard", "What the capture sees, the plugins, and recordings");
    ImGui::Dummy(ImVec2(0.0f, m(4.0f)));

    const CaptureLook look = capture_look(in.snapshot);
    const pipeline::LiveSnapshot* s = in.snapshot;
    const std::string character = s != nullptr && !s->local_name.empty() ? s->local_name : "Not seen yet";
    // The map's name when the table has its id, else the id itself.
    const uint32_t map_id = s != nullptr ? s->map_id : 0;
    const std::string_view map_name = game::map_name(map_id);
    std::string map = "-";
    std::string map_detail = "No map load seen yet";
    if (!map_name.empty()) {
        map = std::string(map_name);
        map_detail = "Map " + std::to_string(map_id);
    } else if (map_id != 0) {
        map = std::to_string(map_id);
        map_detail = "No name for this id yet";
    }
    size_t plugins_on = 0;
    for (const plugins::PluginDescriptor& d : plugins::ALL) {
        if (in.settings != nullptr && in.settings->plugins.enabled(d.id)) ++plugins_on;
    }
    const std::string plugins_label = std::to_string(plugins_on) + " of " + std::to_string(plugins::ALL.size());
    const StatTileSpec tiles[] = {
        {"##tile_capture", ICON_ACTIVITY, "CAPTURE", look.label, look.color, look.detail, colors::Accent},
        {"##tile_char", ICON_TARGET, "CHARACTER", character.c_str(), colors::TextPrimary, "From the game's traffic",
         colors::Violet},
        {"##tile_map", ICON_CROSSHAIR, "MAP", map.c_str(), colors::TextPrimary, map_detail.c_str(), colors::Warning},
        {"##tile_plugins", ICON_PLUG, "PLUGINS ON", plugins_label.c_str(), colors::TextPrimary, "Switched on their pages",
         colors::Success},
    };
    stat_tile_row(tiles, sizeof(tiles) / sizeof(tiles[0]));
    ImGui::Dummy(ImVec2(0.0f, m(metrics::Gutter)));

    const SettingsSection sections[] = {
        {[&] { capture_card(in); }},
        {[&] { plugins_card(in, state); }},
        {[&] { recording_card(in, out); }},
        {[&] { files_card(in, out); }},
        {[&] { safety_card(); }},
    };
    render_settings_grid(sections, sizeof(sections) / sizeof(sections[0]));
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
