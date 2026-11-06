#pragma once

#include "common/config/json.hpp"
#include "common/config/key_table.hpp"
#include "meter/meter_settings.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

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
    bool show_hits{false};
    bool show_perfect{false};
    bool show_double{false};
    bool show_front{false};
    bool show_multi{false};
    bool show_block{false};
    bool show_heal{false};
    /// Bars in each player's class colour; off, all in one colour.
    bool class_bars{true};
    /// Rows drawn, best first.
    int max_rows{8};
    /// Shown only during a fight and for hide_after_combat_s after it. Like the
    /// next one, it applies only while the overlay is locked, so an unlocked one
    /// can always be found and dragged back.
    bool show_in_combat_only{false};
    /// Only in a dungeon (meter::is_dungeon), while locked.
    bool show_in_dungeons_only{false};
    float hide_after_combat_s{5.0f};
    /// Hidden while neither the game nor one of the meter's windows is in front.
    bool hide_when_game_unfocused{false};
};

/// The aura overlay: the effects on the local player, and theirs on the boss, in a
/// window of its own (aura_view.hpp).
struct AuraSettings {
    /// Off until turned on in the app's Auras page.
    bool visible{false};
    /// Locked: click-through, and cannot be moved or resized.
    bool locked{false};
    /// Of the background only; text and bars stay opaque.
    float opacity{0.55f};
    float scale{1.0f};
    /// Screen position and size; x and y of -1 place it under the meter's default spot.
    int x{-1};
    int y{-1};
    int width{280};
    int height{300};
    /// Enemies' effects on the local player.
    bool show_debuffs{true};
    /// The local player's effects on the live fight's boss.
    bool show_target{true};
    /// Effects with no end: passive skills' and the ones a login puts on.
    bool show_permanent{false};
    /// Effects shorter than this are left out: a dodge's mark, a cast's state.
    float min_duration_s{1.0f};
    /// The local player's cooldowns, estimated (meter::CooldownTracker).
    bool show_cooldowns{true};
    /// Cooldowns shorter than this are left out: Dodge's, a combo's.
    float min_cooldown_s{3.0f};
    /// Effects (meter::effect_key) to flag while they are not on the local player.
    /// Not in the key table: a list. Read and written by hand in app_settings.cpp.
    std::vector<uint32_t> watch;
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
    /// Tray notifications: a capture that failed to start, a recording started, a
    /// new version.
    bool notifications{true};
    /// Ask GitHub for a newer release once, soon after the meter starts (Updater).
    bool check_for_updates{true};
    /// Screen position and size; x and y of -1 centre it on the main monitor.
    int x{-1};
    int y{-1};
    int width{1280};
    int height{760};
};

struct AppSettings {
    WindowSettings window;
    CaptureSettings capture;
    OverlaySettings overlay;
    AuraSettings auras;
    /// Spelled from aion2:: because GCC rejects a member named `meter` after an
    /// unqualified `meter::` in the same class ("changes meaning").
    aion2::meter::MeterSettings meter;
    HistorySettings history;
};

inline constexpr std::array<config::ConfigKey<CaptureSettings>, 2> kCaptureKeys{{
    config::field<CaptureSettings, &CaptureSettings::server_port>("server_port"),
    config::field<CaptureSettings, &CaptureSettings::detect_relays>("detect_relays"),
}};

inline constexpr std::array<config::ConfigKey<OverlaySettings>, 25> kOverlayKeys{{
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
    config::field<OverlaySettings, &OverlaySettings::show_hits>("show_hits"),
    config::field<OverlaySettings, &OverlaySettings::show_perfect>("show_perfect"),
    config::field<OverlaySettings, &OverlaySettings::show_double>("show_double"),
    config::field<OverlaySettings, &OverlaySettings::show_front>("show_front"),
    config::field<OverlaySettings, &OverlaySettings::show_multi>("show_multi"),
    config::field<OverlaySettings, &OverlaySettings::show_block>("show_block"),
    config::field<OverlaySettings, &OverlaySettings::show_heal>("show_heal"),
    config::field<OverlaySettings, &OverlaySettings::class_bars>("class_bars"),
    config::field<OverlaySettings, &OverlaySettings::max_rows>("max_rows"),
    config::field<OverlaySettings, &OverlaySettings::show_in_combat_only>("show_in_combat_only"),
    config::field<OverlaySettings, &OverlaySettings::hide_after_combat_s>("hide_after_combat_s"),
    config::field<OverlaySettings, &OverlaySettings::hide_when_game_unfocused>("hide_when_game_unfocused"),
    config::field<OverlaySettings, &OverlaySettings::show_in_dungeons_only>("show_in_dungeons_only"),
}};

inline constexpr std::array<config::ConfigKey<AuraSettings>, 14> kAuraKeys{{
    config::field<AuraSettings, &AuraSettings::visible>("visible"),
    config::field<AuraSettings, &AuraSettings::locked>("locked"),
    config::field<AuraSettings, &AuraSettings::opacity>("opacity"),
    config::field<AuraSettings, &AuraSettings::scale>("scale"),
    config::field<AuraSettings, &AuraSettings::x>("x"),
    config::field<AuraSettings, &AuraSettings::y>("y"),
    config::field<AuraSettings, &AuraSettings::width>("width"),
    config::field<AuraSettings, &AuraSettings::height>("height"),
    config::field<AuraSettings, &AuraSettings::show_debuffs>("show_debuffs"),
    config::field<AuraSettings, &AuraSettings::show_target>("show_target"),
    config::field<AuraSettings, &AuraSettings::show_permanent>("show_permanent"),
    config::field<AuraSettings, &AuraSettings::min_duration_s>("min_duration_s"),
    config::field<AuraSettings, &AuraSettings::show_cooldowns>("show_cooldowns"),
    config::field<AuraSettings, &AuraSettings::min_cooldown_s>("min_cooldown_s"),
}};

/// Most effects a watch list holds.
inline constexpr size_t kMaxWatchedEffects = 64;

inline constexpr std::array<config::ConfigKey<HistorySettings>, 1> kHistoryKeys{{
    config::field<HistorySettings, &HistorySettings::max_fights>("max_fights"),
}};

inline constexpr std::array<config::ConfigKey<WindowSettings>, 8> kWindowKeys{{
    config::field<WindowSettings, &WindowSettings::open_at_start>("open_window_at_start"),
    config::field<WindowSettings, &WindowSettings::close_to_tray>("close_to_tray"),
    config::field<WindowSettings, &WindowSettings::notifications>("notifications"),
    config::field<WindowSettings, &WindowSettings::check_for_updates>("check_for_updates"),
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
