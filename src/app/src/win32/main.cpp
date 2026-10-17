// aion2-combat-meter.exe: the tray icon, the overlay window and the frame loop.
// Everything that is not Win32 lives in AppRuntime and the layers under it.

#include "overlay_window.hpp"

#include "aion2/version.hpp"
#include "app/app_runtime.hpp"
#include "app/app_settings.hpp"
#include "capture/file_source.hpp"
#include "capture/os/npcap_source.hpp"
#include "common/os/logger.hpp"
#include "common/os/paths.hpp"
#include "common/os/single_instance.hpp"
#include "common/os/tray_icon.hpp"

#include <shellapi.h>

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

enum TrayCommand : uint32_t {
    kShowHide = 1000,
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
};

CommandLine parse_command_line() {
    CommandLine cl;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) return cl;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--replay" && i + 1 < argc) {
            cl.replay = std::filesystem::path(argv[++i]);
        } else if (arg == L"--speed" && i + 1 < argc) {
            const double s = _wtof(argv[++i]);
            if (s > 0.0) cl.speed = s;
        }
    }
    LocalFree(argv);
    return cl;
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
    if (!instance.try_acquire()) {
        // The running one shows its overlay.
        instance.notify_existing_instance();
        return 0;
    }

    os::Logger::init(data / "meter.log");
    os::Logger::info(std::string("AION 2 Combat Meter ") + VERSION_STRING);

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
    const auto set_visible = [&](bool visible) {
        settings.overlay.visible = visible;
        window.set_visible(visible);
        settings_dirty = true;
    };
    const auto toggle_recording = [&] {
        runtime.request_recording(!runtime.recording());
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

    os::TrayIcon tray;
    tray.create(app::win32::OverlayWindow::kIconResource, instance.activation_message_id());
    tray.set_menu_builder([&] {
        std::vector<os::TrayMenuItem> items;
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
    tray.set_on_activate([&] { set_visible(true); });
    tray.set_on_command([&](uint32_t id) {
        switch (id) {
            case kShowHide: set_visible(!settings.overlay.visible); break;
            case kLock: set_locked(!settings.overlay.locked); break;
            case kEndFight: runtime.request_end_fight(); break;
            case kRecord: toggle_recording(); break;
            case kOpenHistory:
                std::filesystem::create_directories(runtime.history_dir(), ec);
                os::open_with_default_app(runtime.history_dir());
                break;
            case kOpenRecordings:
                std::filesystem::create_directories(runtime.captures_dir(), ec);
                os::open_with_default_app(runtime.captures_dir());
                break;
            case kOpenConfig:
                if (settings_dirty) save_now();
                os::open_with_default_app(config_path);
                break;
            case kOpenLog: os::open_with_default_app(os::Logger::log_file_path()); break;
            case kExit: running = false; break;
            default: break;
        }
    });
    if (!capturing) tray.notify("AION 2 Combat Meter", capture_error);

    app::OverlayViewState view;
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

        if (window.visible()) {
            // The revision before the list: a save in between then shows next frame
            // instead of being taken as seen.
            if (const uint64_t revision = runtime.history_revision(); revision != history_revision) {
                history = runtime.history();
                history_revision = revision;
            }
            const auto snapshot = runtime.snapshot();
            app::OverlayFrameInput input;
            input.snapshot = snapshot.get();
            input.history = &history;
            input.source_status = runtime.source_status();
            input.recording = runtime.recording();
            input.settings = &settings.overlay;
            input.scale = settings.overlay.scale;
            const app::OverlayActions actions = window.render(input, view);

            if (actions.end_fight) runtime.request_end_fight();
            if (actions.lock) set_locked(true);
            if (actions.toggle_recording) toggle_recording();
            if (actions.open_history_folder) os::open_with_default_app(runtime.history_dir());
            if (actions.hide) set_visible(false);
            if (!actions.load_file.empty()) {
                view.selected_file = actions.load_file;
                view.selected_fight = runtime.load_fight(actions.load_file);
            }
        }

        if (Clock::now() - last_housekeeping >= kHousekeeping) {
            last_housekeeping = Clock::now();
            window.keep_on_top();
            std::string tip = "AION 2 Combat Meter";
            const std::string status = runtime.source_status();
            if (!status.empty()) tip += "\n" + status;
            tray.set_tooltip(tip.substr(0, 120));
            const bool recording = runtime.recording();
            if (recording && !was_recording) tray.notify("Recording", "Saving the game's traffic to " +
                                                                          runtime.recording_path());
            was_recording = recording;
            if (settings_dirty) save_now();
        }

        const auto budget = window.visible() ? kVisibleFrame : kHiddenFrame;
        const auto spent = Clock::now() - frame_start;
        if (spent < budget) std::this_thread::sleep_for(budget - spent);
    }

    if (settings_dirty) save_now();
    runtime.stop();
    window.destroy();
    tray.destroy();
    os::Logger::info("exit");
    os::Logger::shutdown();
    return 0;
}
