#pragma once

// The Combat Meter page's tabs that live in files of their own: each draws one
// fight's detail (meter/fight_detail.hpp) into the space the page leaves it.

#include "app/app_frame.hpp"
#include "app/imgui_guard.hpp"
#include "meter/types.hpp"

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

/// Each player's damage, healing or damage taken second by second, the boss's HP,
/// the deaths and the party-wide buffs.
void timeline_tab(const meter::FightSummary& f, AppViewState& state);
/// The deaths, and the last hits and heals before the one selected.
void deaths_tab(const meter::FightSummary& f, AppViewState& state);
/// How long each effect was up: on the party, from enemies, on the enemies.
void effects_tab(const meter::FightSummary& f, AppViewState& state);

/// What a tab shows for a fight saved before the meter kept its detail.
void no_detail_state(const char* icon);

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
