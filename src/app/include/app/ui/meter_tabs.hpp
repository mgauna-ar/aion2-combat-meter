#pragma once

// The Combat Meter page's tabs that live in files of their own: each draws one
// fight's detail (meter/fight_detail.hpp) into the space the page leaves it.

#include "app/app_frame.hpp"
#include "app/imgui_guard.hpp"
#include "meter/types.hpp"

#include <cstddef>

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
/// A ranking table's height: its `rows` and its header, and no more than half the
/// space when a breakdown shares it.
[[nodiscard]] float table_height(size_t rows, bool breakdown_open);
/// What fills the space under a ranking table while no player is selected: that a
/// row can be clicked, and `what` for. Nothing when there is too little room.
void select_hint(const char* what);
/// The fight's row for `id`; null for 0 or a player not in it.
[[nodiscard]] const meter::CombatantTotals* find_row(const meter::FightSummary& f, meter::EntityId id);
/// Begins a card that ends under its content rather than at the pane's bottom, so a
/// table of a few rows leaves no empty card under it. Returns the room it has, the
/// rest of the pane, for fit_card_table_h(). Pair with end_card().
[[nodiscard]] float begin_fit_card(const char* id);
/// The height of a table of `rows` in a fit card, under what the card holds already:
/// the rows whole, up to the card's `room`, where they scroll instead.
[[nodiscard]] float fit_card_table_h(size_t rows, float room);
/// The card a breakdown sits in, titled with the player, with a Close button, and
/// sized to its table's `rows` as a fit card is. Returns the table's height, or 0
/// (the card already ended) when it was closed. With no rows the card fills the
/// pane, for an empty state.
[[nodiscard]] float begin_breakdown(const char* id, const char* title, const meter::CombatantTotals& c,
                                    meter::EntityId& selected, size_t rows);

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
