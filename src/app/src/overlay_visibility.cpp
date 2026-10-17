#include "app/overlay_visibility.hpp"

namespace aion2::app {

bool overlay_shown(const OverlaySettings& s, const OverlayConditions& now) noexcept {
    if (!s.visible) return false;
    if (!s.locked) return true;
    if (s.show_in_combat_only && !now.fight_live && now.seconds_since_fight >= s.hide_after_combat_s) return false;
    if (s.hide_when_game_unfocused && !now.game_in_front) return false;
    if (s.show_in_dungeons_only && !now.in_dungeon) return false;
    return true;
}

} // namespace aion2::app
