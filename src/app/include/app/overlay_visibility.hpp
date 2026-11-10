#pragma once

#include "app/app_settings.hpp"

namespace aion2::app {

/// What the overlay's hide conditions look at, as the UI thread sees it.
struct OverlayConditions {
    /// The live fight is on.
    bool fight_live{false};
    /// Since the last fight was last seen live, on the UI's own clock; a fight never
    /// seen counts as long ago.
    double seconds_since_fight{1e9};
    /// The foreground window is the game's, or one of the meter's. True whenever
    /// that cannot be told (no game process found), so a condition never hides the
    /// overlay on a guess.
    bool game_in_front{true};
    /// The map is a dungeon (game::is_dungeon). False before any map load.
    bool in_dungeon{false};
};

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
