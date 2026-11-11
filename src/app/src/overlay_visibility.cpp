#include "app/overlay_visibility.hpp"

#include <algorithm>

namespace aion2::app {

bool foreground_is_game(std::span<const uint32_t> game_pids, std::optional<uint32_t> foreground_pid,
                        uint32_t own_pid) noexcept {
    if (game_pids.empty()) return false;
    if (!foreground_pid) return true;
    return *foreground_pid == own_pid || std::find(game_pids.begin(), game_pids.end(), *foreground_pid) != game_pids.end();
}

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

bool ping_shown(const AppSettings& settings, const OverlayConditions& now) noexcept {
    const PingSettings& p = settings.ping;
    if (!settings.plugins.ping) return false;
    if (!p.locked) return true;
    if (p.hide_when_game_unfocused && !now.game_in_front) return false;
    if (p.show_in_dungeons_only && !now.in_dungeon) return false;
    return true;
}

} // namespace aion2::app
