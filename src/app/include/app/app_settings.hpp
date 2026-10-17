#pragma once

#include "common/config/json.hpp"
#include "common/config/key_table.hpp"
#include "meter/meter_settings.hpp"

#include <array>
#include <filesystem>
#include <string>

namespace aion2::app {

/// Every key below is documented once, in README.md ("Configuration").
struct CaptureSettings {
    int server_port{13328};
    /// Not in the key table: a string. Read and written by hand in app_settings.cpp.
    std::string game_process{"Aion2.exe"};
    /// Watch other TCP flows for the game's heartbeat (ping reducers, VPNs).
    bool detect_relays{true};
};

struct OverlaySettings {
    bool visible{true};
    /// Locked: click-through, and cannot be moved or resized.
    bool locked{false};
    /// Of the background only; text stays opaque.
    float opacity{0.80f};
    float scale{1.0f};
    /// Screen position and size; x and y of -1 place it near the top-left.
    int x{-1};
    int y{-1};
    int width{430};
    int height{250};
    bool show_share{true};
    bool show_crit{true};
    bool show_back{true};
    bool show_max_hit{false};
};

struct HistorySettings {
    /// Saved fights kept on disk; the oldest go first.
    int max_fights{500};
};

/// The desktop window, and how the app behaves around it. Start with Windows is
/// not here: the registry's Run key is its only record (os/auto_start.hpp).
struct WindowSettings {
    /// Open the window when the meter starts; else it starts in the tray.
    bool open_at_start{true};
    /// Closing the window hides it to the tray; the tray's Exit quits.
    bool close_to_tray{true};
    /// Tray notifications: a capture that failed to start, a recording started.
    bool notifications{true};
    /// Screen position and size; x and y of -1 centre it on the main monitor.
    int x{-1};
    int y{-1};
    int width{1100};
    int height{700};
};

struct AppSettings {
    WindowSettings window;
    CaptureSettings capture;
    OverlaySettings overlay;
    /// Spelled from aion2:: because GCC rejects a member named `meter` after an
    /// unqualified `meter::` in the same class ("changes meaning").
    aion2::meter::MeterSettings meter;
    HistorySettings history;
};

inline constexpr std::array<config::ConfigKey<CaptureSettings>, 2> kCaptureKeys{{
    config::field<CaptureSettings, &CaptureSettings::server_port>("server_port"),
    config::field<CaptureSettings, &CaptureSettings::detect_relays>("detect_relays"),
}};

inline constexpr std::array<config::ConfigKey<OverlaySettings>, 12> kOverlayKeys{{
    config::field<OverlaySettings, &OverlaySettings::visible>("visible"),
    config::field<OverlaySettings, &OverlaySettings::locked>("locked"),
    config::field<OverlaySettings, &OverlaySettings::opacity>("opacity"),
    config::field<OverlaySettings, &OverlaySettings::scale>("scale"),
    config::field<OverlaySettings, &OverlaySettings::x>("x"),
    config::field<OverlaySettings, &OverlaySettings::y>("y"),
    config::field<OverlaySettings, &OverlaySettings::width>("width"),
    config::field<OverlaySettings, &OverlaySettings::height>("height"),
    config::field<OverlaySettings, &OverlaySettings::show_share>("show_share"),
    config::field<OverlaySettings, &OverlaySettings::show_crit>("show_crit"),
    config::field<OverlaySettings, &OverlaySettings::show_back>("show_back"),
    config::field<OverlaySettings, &OverlaySettings::show_max_hit>("show_max_hit"),
}};

inline constexpr std::array<config::ConfigKey<HistorySettings>, 1> kHistoryKeys{{
    config::field<HistorySettings, &HistorySettings::max_fights>("max_fights"),
}};

inline constexpr std::array<config::ConfigKey<WindowSettings>, 7> kWindowKeys{{
    config::field<WindowSettings, &WindowSettings::open_at_start>("open_window_at_start"),
    config::field<WindowSettings, &WindowSettings::close_to_tray>("close_to_tray"),
    config::field<WindowSettings, &WindowSettings::notifications>("notifications"),
    config::field<WindowSettings, &WindowSettings::x>("window_x"),
    config::field<WindowSettings, &WindowSettings::y>("window_y"),
    config::field<WindowSettings, &WindowSettings::width>("window_width"),
    config::field<WindowSettings, &WindowSettings::height>("window_height"),
}};

/// Clamps every value a hand-edited file could put out of range.
void sanitize(AppSettings& settings);

[[nodiscard]] config::JsonValue settings_to_json(const AppSettings& settings);
/// Keys absent from `json` keep their defaults; a value of the wrong type is ignored.
[[nodiscard]] AppSettings settings_from_json(const config::JsonValue& json);

/// The settings in `file` (config.json), or the defaults when it is missing or unreadable.
[[nodiscard]] AppSettings load_settings(const std::filesystem::path& file);
/// Empty on success, else what failed.
[[nodiscard]] std::string save_settings(const std::filesystem::path& file, const AppSettings& settings);

} // namespace aion2::app
