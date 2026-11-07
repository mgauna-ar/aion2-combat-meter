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

/// What each player pressed, and how often.
void casts_tab(const meter::FightSummary& f, AppViewState& state);

/// What a tab shows for a fight saved before the meter kept its detail.
void no_detail_state(const char* icon);

// The ranking tables' parts (view_meter.cpp), for the tabs here that rank players.

/// The row's player: a selectable across the row, which opens their breakdown in
/// `selected` (or closes it). `tab` keeps the ids of two tabs apart.
void name_cell(int column, const meter::CombatantTotals& c, meter::EntityId& selected, const char* tab);
/// The table's height when a breakdown shares the space under it.
[[nodiscard]] float table_height(bool breakdown_open);
/// The fight's row for `id`; null for 0 or a player not in it.
[[nodiscard]] const meter::CombatantTotals* find_row(const meter::FightSummary& f, meter::EntityId id);
/// The card a breakdown sits in, titled with the player, with a Close button. False
/// (the card already ended) when it was closed.
bool begin_breakdown(const char* id, const char* title, const meter::CombatantTotals& c, meter::EntityId& selected);

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
