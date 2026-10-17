#include "app/ui/views.hpp"

#include "aion2/version.hpp"
#include "app/ui/icons.hpp"
#include "app/ui/page.hpp"
#include "app/ui/widgets.hpp"

#ifdef HAVE_IMGUI
#include <algorithm>
#include <cstdio>
#include <cstring>
#endif

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

namespace {

void window_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##set_window", ICON_MONITOR, "WINDOW", colors::Accent);
    WindowSettings& w = in.settings->window;
    setting_switch("Open this window at start", "Off, the meter starts in the tray.", &w.open_at_start, out);
    setting_switch("Close to tray", "Closing the window hides it; the tray's Exit quits.", &w.close_to_tray, out);
    setting_switch("Notifications", "Tray notices: a capture that failed to start, a recording.", &w.notifications,
                   out);
    bool start = in.start_with_windows;
    if (setting_toggle("Start with Windows", "Launch the meter when you sign in to Windows.", &start)) {
        out.start_with_windows = start;
    }
    end_settings_card();
}

void capture_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##set_capture", ICON_ACTIVITY, "CAPTURE", colors::Violet);
    pill("Applies after a restart", colors::WarningLight);
    ImGui::Dummy(ImVec2(0.0f, m(2.0f)));
    CaptureSettings& c = in.settings->capture;

    begin_setting_row("Server port", "The game server's TCP port.");
    int port = c.server_port;
    if (ImGui::InputInt("##server_port", &port, 0, 0)) {
        c.server_port = std::clamp(port, 1, 65535);
        out.settings_changed = true;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) out.save_settings = true;
    end_setting_row();

    begin_setting_row("Game process", "Its executable, to follow a ping reducer's relay.");
    char name[65] = {};
    std::snprintf(name, sizeof(name), "%s", c.game_process.c_str());
    if (ImGui::InputText("##game_process", name, sizeof(name))) {
        c.game_process = name;
        out.settings_changed = true;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) out.save_settings = true;
    end_setting_row();

    setting_switch("Detect relays", "Watch other flows for the game's heartbeat (ping reducers, VPNs).",
                   &c.detect_relays, out);
    end_settings_card();
}

void hotkeys_card() {
    begin_settings_card("##set_keys", ICON_BOLT, "HOTKEYS", colors::Warning);
    const struct {
        const char* keys;
        const char* what;
    } rows[] = {
        {"Ctrl+Alt+L", "Lock or unlock the overlay"},
        {"Ctrl+Alt+H", "Hide or show the overlay"},
        {"Ctrl+Alt+R", "End the current fight"},
    };
    for (const auto& r : rows) {
        begin_setting_row(r.what, nullptr);
        text_colored_u32(colors::TextPrimary, "%s", r.keys);
        end_setting_row();
    }
    end_settings_card();
}

void about_card(AppActions& out) {
    begin_settings_card("##set_about", ICON_INFO, "ABOUT", colors::Success);
    text_colored_u32(colors::TextPrimary, "AION 2 Combat Meter %s", VERSION_STRING);
    ImGui::PushTextWrapPos(0.0f);
    text_dim("Settings are saved as you change them, to config.json. Edit that file only while the meter is "
             "closed.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.0f, m(4.0f)));
    if (button(ICON_FILE "  config.json")) out.open = OpenTarget::Config;
    same_line_if_room(button_width(ICON_TERMINAL "  Log"));
    if (button(ICON_TERMINAL "  Log")) out.open = OpenTarget::Log;
    end_settings_card();
}

} // namespace

void render_settings_page(const AppFrameInput& in, AppViewState&, AppActions& out) {
    page_header(ICON_SETTINGS, "Settings", "The app, its window and the capture");
    ImGui::Dummy(ImVec2(0.0f, m(4.0f)));
    if (in.settings == nullptr) return;
    const SettingsSection sections[] = {
        {[&] { window_card(in, out); }},
        {[&] { capture_card(in, out); }},
        {[&] { hotkeys_card(); }},
        {[&] { about_card(out); }},
    };
    render_settings_grid(sections, sizeof(sections) / sizeof(sections[0]));
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
