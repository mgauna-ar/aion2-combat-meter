// aion2-combat-meter.exe: the tray icon, the app window, the overlay and the frame
// loop. Everything that is not Win32 lives in AppRuntime, draw_app, draw_overlay and
// the layers under them.

#include "app_window.hpp"
#include "overlay_window.hpp"

#include "aion2/version.hpp"
#include "app/app_runtime.hpp"
#include "app/app_settings.hpp"
#include "app/overlay_visibility.hpp"
#include "app/updater.hpp"
#include "capture/file_source.hpp"
#include "capture/os/connection_finder.hpp"
#include "capture/os/npcap_source.hpp"
#include "common/os/auto_start.hpp"
#include "common/os/http_client.hpp"
#include "common/os/logger.hpp"
#include "common/os/paths.hpp"
#include "common/os/process_launch.hpp"
#include "common/os/single_instance.hpp"
#include "common/os/tray_icon.hpp"
#include "meter/map_names.hpp"

#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace aion2;
using Clock = std::chrono::steady_clock;

constexpr auto kVisibleFrame = std::chrono::milliseconds(33);
constexpr auto kHiddenFrame = std::chrono::milliseconds(100);
constexpr auto kHousekeeping = std::chrono::seconds(2);
/// How long an updated build waits for the one that installed it to exit.
constexpr auto kRelaunchWait = std::chrono::seconds(15);

enum TrayCommand : uint32_t {
    kOpenApp = 1000,
    kShowHide,
    kLock,
    kEndFight,
    kRecord,
    kOpenHistory,
    kOpenRecordings,
    kOpenConfig,
    kOpenLog,
    kExit,
};

struct CommandLine {
    std::optional<std::filesystem::path> replay;
    double speed{1.0};
    /// Started by the updater (app::RELAUNCH_ARG).
    bool updated{false};
};

CommandLine parse_command_line() {
    CommandLine cl;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) return cl;
    const std::wstring relaunch_arg(app::RELAUNCH_ARG.begin(), app::RELAUNCH_ARG.end());
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == relaunch_arg) {
            cl.updated = true;
        } else if (arg == L"--replay" && i + 1 < argc) {
            cl.replay = std::filesystem::path(argv[++i]);
        } else if (arg == L"--speed" && i + 1 < argc) {
            const double s = _wtof(argv[++i]);
            if (s > 0.0) cl.speed = s;
        }
    }
    LocalFree(argv);
    return cl;
}

/// Whether the foreground window is the game's or one of the meter's, by process id
/// alone: GetWindowThreadProcessId opens no handle. True when the game's process
/// was not found, so the condition never hides the overlay on a guess.
bool game_in_front(const std::vector<uint32_t>& game_pids) {
    if (game_pids.empty()) return true;
    HWND fg = GetForegroundWindow();
    if (fg == nullptr) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId() || std::find(game_pids.begin(), game_pids.end(), pid) != game_pids.end();
}

std::filesystem::path data_dir() {
    std::filesystem::path dir = os::app_data_dir();
    if (dir.empty()) dir = os::executable_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const CommandLine cl = parse_command_line();
    const std::filesystem::path data = data_dir();

    // Before the log: opening it starts it afresh, which would wipe the running
    // instance's.
    os::SingleInstance instance;
    // An updated build waits for the one that installed it: its lock frees as it exits.
    const bool primary = cl.updated ? instance.acquire_within(kRelaunchWait) : instance.try_acquire();
    if (!primary) {
        // The running one shows its overlay.
        instance.notify_existing_instance();
        return 0;
    }

    os::Logger::init(data / "meter.log");
    os::Logger::info(std::string("AION 2 Combat Meter ") + VERSION_STRING);
    // After the lock, so the build that left them has exited.
    app::remove_update_leftovers(os::executable_dir());

    const std::filesystem::path config_path = data / "config.json";
    app::AppSettings settings = app::load_settings(config_path);
    std::error_code ec;
    if (!std::filesystem::exists(config_path, ec)) {
        const std::string why = app::save_settings(config_path, settings);
        if (!why.empty()) os::Logger::warn("cannot write config.json: " + why);
    }

    app::AppRuntime runtime(settings, data);
    std::unique_ptr<capture::PacketSource> source;
    if (cl.replay) {
        source = std::make_unique<capture::FileSource>(*cl.replay, cl.speed);
    } else {
        capture::NpcapOptions npcap;
        npcap.server_port = static_cast<uint16_t>(settings.capture.server_port);
        npcap.game_process = settings.capture.game_process;
        source = std::make_unique<capture::NpcapSource>(npcap);
    }
    std::string capture_error;
    const bool capturing = runtime.start(std::move(source), capture_error);

    app::win32::OverlayWindow window;
    std::string window_error;
    if (!window.create(settings.overlay, window_error)) {
        os::Logger::error(window_error);
        MessageBoxW(nullptr, os::to_wide(window_error).c_str(), L"AION 2 Combat Meter", MB_OK | MB_ICONERROR);
        runtime.stop();
        return 1;
    }
    // The app window is a convenience: without it the overlay and the tray still work.
    app::win32::AppWindow app_window;
    std::string app_error;
    const bool have_app = app_window.create(settings.window, app::win32::OverlayWindow::kIconResource, app_error);
    if (!have_app) os::Logger::error(app_error);
    if (have_app && settings.window.open_at_start) app_window.show();

    os::TrayIcon tray;
    bool running = true;
    bool settings_dirty = false;
    const auto save_now = [&] {
        const std::string why = app::save_settings(config_path, settings);
        if (!why.empty()) os::Logger::warn("cannot save config.json: " + why);
        settings_dirty = false;
    };
    const auto set_locked = [&](bool locked) {
        settings.overlay.locked = locked;
        window.set_locked(locked);
        settings_dirty = true;
    };
    // The window follows overlay_shown() each frame; this is the user's switch.
    const auto set_visible = [&](bool visible) {
        settings.overlay.visible = visible;
        settings_dirty = true;
    };
    const auto toggle_recording = [&] {
        runtime.request_recording(!runtime.recording());
    };
    const auto notify = [&](const std::string& title, const std::string& text) {
        if (settings.window.notifications) tray.notify(title, text);
    };
    const auto open_app = [&] {
        if (have_app) app_window.show();
    };
    const auto open_target = [&](app::OpenTarget target) {
        std::error_code dir_ec;
        switch (target) {
            case app::OpenTarget::DataFolder: os::open_with_default_app(data); break;
            case app::OpenTarget::History:
                std::filesystem::create_directories(runtime.history_dir(), dir_ec);
                os::open_with_default_app(runtime.history_dir());
                break;
            case app::OpenTarget::Recordings:
                std::filesystem::create_directories(runtime.captures_dir(), dir_ec);
                os::open_with_default_app(runtime.captures_dir());
                break;
            case app::OpenTarget::Config:
                if (settings_dirty) save_now();
                os::open_with_default_app(config_path);
                break;
            case app::OpenTarget::Log: os::open_with_default_app(os::Logger::log_file_path()); break;
            case app::OpenTarget::None: break;
        }
    };
    // The overlay window follows its settings, which the app window edits in place;
    // whether it shows is decided each frame.
    const auto apply_overlay = [&] {
        if (window.locked() != settings.overlay.locked) window.set_locked(settings.overlay.locked);
    };

    window.on_hotkey = [&](app::win32::Hotkey key) {
        switch (key) {
            case app::win32::Hotkey::ToggleLock: set_locked(!settings.overlay.locked); break;
            case app::win32::Hotkey::ToggleVisible: set_visible(!settings.overlay.visible); break;
            case app::win32::Hotkey::EndFight: runtime.request_end_fight(); break;
        }
    };
    window.on_geometry_changed = [&] {
        const RECT r = window.screen_rect();
        settings.overlay.x = r.left;
        settings.overlay.y = r.top;
        settings.overlay.width = r.right - r.left;
        settings.overlay.height = r.bottom - r.top;
        settings_dirty = true;
    };

    tray.create(app::win32::OverlayWindow::kIconResource, instance.activation_message_id());
    tray.set_menu_builder([&] {
        std::vector<os::TrayMenuItem> items;
        items.push_back({kOpenApp, "Open AION 2 Combat Meter", false, have_app});
        items.push_back({0, "", false, true, true});
        items.push_back({kShowHide, settings.overlay.visible ? "Hide overlay\tCtrl+Alt+H" : "Show overlay\tCtrl+Alt+H"});
        items.push_back({kLock, "Lock (click-through)\tCtrl+Alt+L", settings.overlay.locked});
        items.push_back({kEndFight, "End current fight\tCtrl+Alt+R"});
        items.push_back({kRecord, "Record capture", runtime.recording()});
        items.push_back({0, "", false, true, true});
        items.push_back({kOpenHistory, "Open history folder"});
        items.push_back({kOpenRecordings, "Open recordings folder"});
        items.push_back({kOpenConfig, "Open config.json"});
        items.push_back({kOpenLog, "Open log"});
        items.push_back({0, "", false, true, true});
        items.push_back({kExit, "Exit"});
        return items;
    });
    // A click on the icon, or a second launch, opens the app window.
    tray.set_on_activate([&] {
        if (have_app) {
            open_app();
        } else {
            set_visible(true);
        }
    });
    app_window.on_close = [&] {
        if (settings.window.close_to_tray) {
            app_window.hide();
        } else {
            running = false;
        }
    };
    app_window.on_geometry_changed = [&] {
        const RECT r = app_window.screen_rect();
        settings.window.x = r.left;
        settings.window.y = r.top;
        settings.window.width = r.right - r.left;
        settings.window.height = r.bottom - r.top;
        settings_dirty = true;
    };
    tray.set_on_command([&](uint32_t id) {
        switch (id) {
            case kOpenApp: open_app(); break;
            case kShowHide: set_visible(!settings.overlay.visible); break;
            case kLock: set_locked(!settings.overlay.locked); break;
            case kEndFight: runtime.request_end_fight(); break;
            case kRecord: toggle_recording(); break;
            case kOpenHistory: open_target(app::OpenTarget::History); break;
            case kOpenRecordings: open_target(app::OpenTarget::Recordings); break;
            case kOpenConfig: open_target(app::OpenTarget::Config); break;
            case kOpenLog: open_target(app::OpenTarget::Log); break;
            case kExit: running = false; break;
            default: break;
        }
    });
    if (!capturing) notify("AION 2 Combat Meter", capture_error);
    if (cl.updated) notify("AION 2 Combat Meter", std::string("Updated to version ") + VERSION_STRING + ".");

    app::Updater updater(os::executable_dir());
    app::UpdateNotifier update_notifier;

    app::AppViewState app_view;
    // The overlay's hide conditions: when a fight was last seen live, and the game's
    // processes, refreshed with the housekeeping.
    std::optional<Clock::time_point> last_live;
    std::vector<uint32_t> game_pids = capture::game_process_ids(settings.capture.game_process);
    bool start_with_windows = os::auto_start_enabled();
    const std::string data_utf8 = os::to_utf8(data);
    const std::string log_utf8 = os::to_utf8(os::Logger::log_file_path());
    std::vector<meter::HistoryEntry> history;
    uint64_t history_revision = 0;
    bool was_recording = false;
    auto last_housekeeping = Clock::now();

    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;
        const auto frame_start = Clock::now();

        updater.tick(frame_start, os::http_supported() && settings.window.check_for_updates);
        const app::UpdateStatus update = updater.status();
        if (update.state == app::UpdateState::Available && update.release) {
            if (const auto notice = update_notifier.update(update.release->version)) {
                notify(notice->title, notice->message);
            }
        }
        if (update.state == app::UpdateState::Ready && updater.apply_staged()) {
            const std::filesystem::path exe = updater.install_dir() / app::UPDATE_EXE_NAME;
            if (os::launch_detached(exe, {std::string(app::RELAUNCH_ARG)})) {
                os::Logger::info("restarting into the update");
                break;
            }
            os::Logger::warn("cannot start the updated meter; restart it to finish updating");
        }

        const bool show_app = have_app && app_window.visible();
        // The revision before the list: a save in between then shows next frame
        // instead of being taken as seen.
        if (show_app) {
            if (const uint64_t revision = runtime.history_revision(); revision != history_revision) {
                history = runtime.history();
                history_revision = revision;
            }
        }
        const auto snapshot = runtime.snapshot();

        if (show_app) {
            app::AppFrameInput input;
            input.snapshot = snapshot.get();
            input.history = &history;
            input.source_status = runtime.source_status();
            input.recording = runtime.recording();
            input.recording_path = runtime.recording_path();
            input.settings = &settings;
            input.start_with_windows = start_with_windows;
            input.data_dir = data_utf8;
            input.log_path = log_utf8;
            input.update = update;
            const app::AppActions actions = app_window.render(input, app_view);

            if (actions.overlay_changed) apply_overlay();
            if (actions.meter_changed) runtime.update_settings(settings.meter, settings.history);
            if (actions.save_settings) {
                save_now();
            } else if (actions.settings_changed) {
                settings_dirty = true;
            }
            if (actions.start_with_windows) {
                if (os::set_auto_start(*actions.start_with_windows)) {
                    start_with_windows = *actions.start_with_windows;
                } else {
                    os::Logger::warn("cannot change Start with Windows");
                }
            }
            if (actions.end_fight) runtime.request_end_fight();
            if (actions.toggle_recording) toggle_recording();
            if (actions.clear_history) runtime.request_clear_history();
            if (actions.reset_overlay_position) {
                window.reset_position();
                settings.overlay.x = -1;
                settings.overlay.y = -1;
                save_now();
            }
            open_target(actions.open);
            if (actions.check_updates) updater.check_now();
            if (actions.install_update) updater.install();
            if (actions.open_release_notes && update.release) os::open_url(update.release->page_url);
            if (!actions.load_file.empty()) {
                app_view.selected_file = actions.load_file;
                app_view.selected_fight = runtime.load_fight(actions.load_file);
            }
        }

        app::OverlayConditions conditions;
        conditions.fight_live = snapshot != nullptr && snapshot->fight.live;
        if (conditions.fight_live) last_live = Clock::now();
        if (last_live) {
            conditions.seconds_since_fight = std::chrono::duration<double>(Clock::now() - *last_live).count();
        }
        conditions.game_in_front = game_in_front(game_pids);
        conditions.in_dungeon = snapshot != nullptr && meter::is_dungeon(snapshot->map_id);
        if (const bool shown = app::overlay_shown(settings.overlay, conditions); shown != window.visible()) {
            window.set_visible(shown);
        }

        if (window.visible()) {
            app::OverlayFrameInput input;
            input.snapshot = snapshot.get();
            input.source_status = runtime.source_status();
            input.recording = runtime.recording();
            input.settings = &settings.overlay;
            input.scale = settings.overlay.scale;
            const app::OverlayActions actions = window.render(input);

            if (actions.end_fight) runtime.request_end_fight();
            if (actions.lock) set_locked(true);
            if (actions.open_app) open_app();
        }

        if (Clock::now() - last_housekeeping >= kHousekeeping) {
            last_housekeeping = Clock::now();
            window.keep_on_top();
            if (settings.overlay.hide_when_game_unfocused) {
                game_pids = capture::game_process_ids(settings.capture.game_process);
            }
            std::string tip = "AION 2 Combat Meter";
            const std::string status = runtime.source_status();
            if (!status.empty()) tip += "\n" + status;
            tray.set_tooltip(tip.substr(0, 120));
            const bool recording = runtime.recording();
            if (recording && !was_recording) notify("Recording", "Saving the game's traffic to " + runtime.recording_path());
            was_recording = recording;
            if (settings_dirty) save_now();
        }

        const auto budget = window.visible() || (have_app && app_window.visible()) ? kVisibleFrame : kHiddenFrame;
        const auto spent = Clock::now() - frame_start;
        if (spent < budget) std::this_thread::sleep_for(budget - spent);
    }

    if (settings_dirty) save_now();
    updater.stop();
    runtime.stop();
    app_window.destroy();
    window.destroy();
    tray.destroy();
    os::Logger::info("exit");
    os::Logger::shutdown();
    return 0;
}
