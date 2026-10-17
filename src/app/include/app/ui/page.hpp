#pragma once

// A page's tab bar and settings layout, ported from ffxiv-hub's plugin pages.

#include "app/imgui_guard.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

/// One tab of a page: its icon, its label, and what it draws.
struct PageTab {
    const char* icon{nullptr};
    const char* label{nullptr};
    std::function<void()> body;
};

/// The tab bar under a page's header. No label is ever clipped: every tab shows its
/// icon while all of them fit, only its label once they do not, and the bar scrolls
/// when even the bare labels do not fit. A tab keeps its id either way, so the open
/// tab survives a resize across the switch. `select`, when in range, opens that tab
/// this frame.
void render_tabs(const char* id, const PageTab* tabs, size_t count, int select = -1);

/// One card of a settings page. The order these are listed in is the order the
/// user sees.
struct SettingsSection {
    std::function<void()> body;
};

/// Settings sections in two balanced columns when there is room, one otherwise.
void render_settings_grid(const SettingsSection* sections, size_t count);

/// Opens a titled card in a settings grid. Always paired with end_settings_card().
void begin_settings_card(const char* id, const char* icon, const char* label, uint32_t accent);
void end_settings_card();

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
