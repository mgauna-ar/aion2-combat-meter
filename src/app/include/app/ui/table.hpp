#pragma once

// The tables of the Combat Meter page: columns that drop by priority when the
// table is narrow, and the cells its tabs share.

#include "app/imgui_guard.hpp"
#include "app/ui/theme.hpp"
#include "meter/types.hpp"

#include <cstdint>
#include <string>

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

/// Every table on the page: a table that does not fit scrolls rather than crushing
/// its columns (table_sizing). PadOuterX keeps the edge columns off the sides.
inline constexpr ImGuiTableFlags kTableFlags =
    ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX;

struct Column {
    const char* name;
    /// Unscaled pixels; 0 stretches.
    float width;
    const char* tip;
    /// 0 is shown first; the highest go when the table is narrow.
    int priority;
};

/// A ranking or breakdown table: columns, the header row with each column's
/// tooltip, scrolling with the header frozen. A table narrower than its columns
/// drops the least important ones (by priority) rather than scroll sideways; only
/// one too narrow even for those scrolls. False when nothing is to be drawn.
bool begin_table(const char* id, const Column* cols, int count, float height);

/// A cell's text; nothing for a column dropped for width.
void cell(int column, uint32_t color, const std::string& text);

/// A skill's icon, then its name; nothing for a column dropped for width. A skill
/// without an icon keeps the space, so the names line up.
void skill_cell(int column, uint32_t code, uint32_t color = colors::TextBody);

/// An effect's icon, then its name (game::effect_label).
void effect_cell(int column, uint32_t buff_id, uint32_t source_skill, uint32_t color = colors::TextBody);

/// A player's name in their class's colour, after their class's emblem.
void player_cell(int column, const std::string& name, game::AionClass cls);

/// A row's colour: its class's, the accent for the local player whose class is not
/// known, and grey for anyone else's.
[[nodiscard]] uint32_t row_color_u32(const meter::CombatantTotals& c);

/// A class's colour, or `fallback` while it is not known.
[[nodiscard]] uint32_t class_or(game::AionClass cls, uint32_t fallback);

/// "1234".
[[nodiscard]] std::string number(uint64_t n);

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
