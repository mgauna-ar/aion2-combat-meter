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
};

/// Whether the overlay shows now. `visible` (Ctrl+Alt+H) rules first. The other
/// conditions apply only while the overlay is locked: an unlocked overlay can
/// always be found and dragged back.
[[nodiscard]] bool overlay_shown(const OverlaySettings& settings, const OverlayConditions& now) noexcept;

} // namespace aion2::app
