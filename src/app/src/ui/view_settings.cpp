#include "app/ui/views.hpp"

#include "aion2/version.hpp"
#include "app/ui/icons.hpp"
#include "app/ui/page.hpp"
#include "app/ui/widgets.hpp"

#ifdef HAVE_IMGUI
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#endif

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

namespace {

void window_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##set_window", ICON_MONITOR, "WINDOW", colors::Accent);
    WindowSettings& w = in.settings->window;
    setting_switch("Open this window at start", "Off, the meter starts in the tray.", &w.open_at_start, out);
    setting_switch("Close to tray", "Closing the window hides it; the tray's Exit quits.", &w.close_to_tray, out);
    setting_switch("Notifications", "Tray notices: a capture that failed to start, a recording, a new version.",
                   &w.notifications, out);
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

/// The status line under the version: what the last check or install found.
void update_status_line(const UpdateStatus& status) {
    const std::string next = status.release ? status.release->version.to_string() : std::string();
    ImGui::PushTextWrapPos(0.0f);
    switch (status.state) {
        case UpdateState::Idle:
            text_colored_u32(colors::TextDim, "Not checked for updates yet.");
            break;
        case UpdateState::Checking:
            text_colored_u32(colors::TextMuted, "Checking GitHub for a newer release...");
            break;
        case UpdateState::UpToDate:
            text_colored_u32(colors::SuccessLight, "Up to date.");
            break;
        case UpdateState::Available:
            text_colored_u32(colors::AccentHover, "Version %s is available.", next.c_str());
            if (!status.release->installable()) {
                text_colored_u32(colors::TextDim,
                                 "It can't be checked before installing, so download it from its release page.");
            }
            break;
        case UpdateState::Downloading:
            text_colored_u32(colors::TextMuted, "Downloading version %s...", next.c_str());
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, v4(colors::Accent));
            ImGui::ProgressBar(status.progress, ImVec2(-FLT_MIN, m(6.0f)), "");
            ImGui::PopStyleColor();
            break;
        case UpdateState::Ready:
            text_colored_u32(colors::TextMuted, "Installing version %s...", next.c_str());
            break;
        case UpdateState::RestartPending:
            text_colored_u32(colors::WarningLight, "Version %s is installed. Restart the meter to finish updating.",
                             next.c_str());
            break;
        case UpdateState::Failed:
            text_colored_u32(colors::DangerLight, "%s", status.error.c_str());
            break;
    }
    ImGui::PopTextWrapPos();
}

void update_confirm_popup(const UpdateStatus& status, AppActions& out) {
    if (!ImGui::BeginPopupModal("##ConfirmUpdate", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        return;
    }
    icon_chip(ICON_DOWNLOAD, colors::Accent, metrics::ChipSizeLg);
    ImGui::SameLine(0.0f, m(10.0f));
    ImGui::BeginGroup();
    ImGui::PushFont(bold_font());
    text_colored_u32(colors::TextPrimary, "Update to version %s?",
                     status.release ? status.release->version.to_string().c_str() : "");
    ImGui::PopFont();
    text_dim("The meter downloads it, checks it and restarts.");
    text_dim("A fight still going is not saved.");
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0.0f, m(8.0f)));
    if (button(ICON_DOWNLOAD "  Update & restart", ButtonKind::Primary, ButtonSize::Large)) {
        out.install_update = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine(0.0f, m(8.0f));
    if (button("Cancel", ButtonKind::Secondary, ButtonSize::Small)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void updates_card(const AppFrameInput& in, AppActions& out) {
    begin_settings_card("##set_updates", ICON_DOWNLOAD, "UPDATES", colors::Accent);
    if (in.update) {
        const UpdateStatus& status = *in.update;
        update_status_line(status);
        ImGui::Dummy(ImVec2(0.0f, m(4.0f)));

        const char* update_label = ICON_DOWNLOAD "  Update & restart";
        const char* check_label = ICON_REFRESH "  Check now";
        const char* notes_label = ICON_FILE "  Release notes";
        const bool has_release = status.release.has_value();
        if (has_release) {
            ImGui::BeginDisabled(!status.can_install());
            if (button(update_label, ButtonKind::Primary)) ImGui::OpenPopup("##ConfirmUpdate");
            ImGui::EndDisabled();
            if (!status.release->installable() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("This release publishes no checksum to verify the download against.");
            }
            same_line_if_room(button_width(check_label));
        }
        // Once installed, only a restart is left to do.
        ImGui::BeginDisabled(status.busy() || status.state == UpdateState::RestartPending);
        if (button(check_label)) out.check_updates = true;
        ImGui::EndDisabled();
        if (has_release && !status.release->page_url.empty()) {
            same_line_if_room(button_width(notes_label));
            if (button(notes_label)) out.open_release_notes = true;
        }
        update_confirm_popup(status, out);
        ImGui::Dummy(ImVec2(0.0f, m(4.0f)));
    }
    setting_switch("Check for updates at startup", "Asks GitHub for a newer release each time the meter starts.",
                   &in.settings->window.check_for_updates, out);
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
        {[&] { updates_card(in, out); }},
        {[&] { about_card(out); }},
    };
    render_settings_grid(sections, sizeof(sections) / sizeof(sections[0]));
}

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
