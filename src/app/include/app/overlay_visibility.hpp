#pragma once

#include "app/app_settings.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace aion2::app {

/// What the overlay's hide conditions look at, as the UI thread sees it.
struct OverlayConditions {
    /// The live fight is on.
    bool fight_live{false};
    /// Since the last fight was last seen live, on the UI's own clock; a fight never
    /// seen counts as long ago.
    double seconds_since_fight{1e9};
    /// The game is running and the foreground window is its, or one of the meter's
    /// (foreground_is_game).
    bool game_in_front{true};
    /// The map is a dungeon (game::is_dungeon: by its id, named or not). False before
    /// any map load.
    bool in_dungeon{false};
};

/// Whether the game counts as in front, for hide_when_game_unfocused, from process
/// ids alone (no handle is opened to find them).
/// - `game_pids`: the running processes named as the game; empty when it is not
///   running, and then the game is not in front, whatever is: the meter's own window
///   included, since an overlay over no game shows nothing worth seeing.
/// - `foreground_pid`: the foreground window's process; none while focus is passing
///   between windows, which counts as in front so the overlay does not flicker.
/// - `own_pid`: the meter's own process, whose windows count as the game's while it
///   runs, so opening the app window does not hide the overlays it configures.
[[nodiscard]] bool foreground_is_game(std::span<const uint32_t> game_pids, std::optional<uint32_t> foreground_pid,
                                      uint32_t own_pid) noexcept;

/// Whether the meter overlay shows now. The Combat Meter plugin switched off hides
/// it; then `visible` (Ctrl+Alt+H) rules. The other conditions apply only while the
/// overlay is locked: an unlocked overlay can always be found and dragged back.
[[nodiscard]] bool overlay_shown(const AppSettings& settings, const OverlayConditions& now) noexcept;

/// Whether the aura overlay shows now: the Auras plugin's master switch first, then,
/// while it is locked, its own hide_when_game_unfocused and show_in_dungeons_only.
/// Its effects matter out of combat too, so it has no show_in_combat_only.
[[nodiscard]] bool auras_shown(const AppSettings& settings, const OverlayConditions& now) noexcept;

/// Whether the ping overlay shows now: the Ping plugin's master switch first, then,
/// while it is locked, its own hide_when_game_unfocused and show_in_dungeons_only.
/// Lag matters out of combat too, so it has no show_in_combat_only either.
[[nodiscard]] bool ping_shown(const AppSettings& settings, const OverlayConditions& now) noexcept;

} // namespace aion2::app
