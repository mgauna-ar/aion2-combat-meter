#include "app/overlay_visibility.hpp"

namespace aion2::app {

bool overlay_shown(const AppSettings& settings, const OverlayConditions& now) noexcept {
    const OverlaySettings& s = settings.overlay;
    if (!settings.plugins.combat_meter || !s.visible) return false;
    if (!s.locked) return true;
    if (s.show_in_combat_only && !now.fight_live && now.seconds_since_fight >= s.hide_after_combat_s) return false;
    if (s.hide_when_game_unfocused && !now.game_in_front) return false;
    if (s.show_in_dungeons_only && !now.in_dungeon) return false;
    return true;
}

bool auras_shown(const AppSettings& settings, const OverlayConditions& now) noexcept {
    const AuraSettings& a = settings.auras;
    if (!settings.plugins.auras) return false;
    if (!a.locked) return true;
    if (a.hide_when_game_unfocused && !now.game_in_front) return false;
    if (a.show_in_dungeons_only && !now.in_dungeon) return false;
    return true;
}

} // namespace aion2::app
