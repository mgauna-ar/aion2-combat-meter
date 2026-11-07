#pragma once

#include "game/classes.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// Forward declared so this header does not pull imgui.h into everything that uses it.
struct ImFontAtlas;
struct ImDrawList;

namespace aion2::app {

/// The game's own art, as the app draws it: each class's party emblem and each
/// skill's icon, packed by tools/gen_icon_sheet.py into game_icon_sheet.inc. The
/// images are NCSOFT's, bundled because the meter fetches nothing at run time
/// (AGENTS.md, Passive only). An icon is a slot in that sheet.
inline constexpr int kNoIcon = -1;

/// The class's emblem, or kNoIcon: Unknown and Brawler have none.
[[nodiscard]] int class_icon(game::AionClass cls) noexcept;
/// The skill's icon: the one of the row that names it (game::skill_key), so a
/// variant shows its skill's icon. kNoIcon for a code without one: a monster's
/// skill, a spirit's attack.
[[nodiscard]] int skill_icon(uint32_t code) noexcept;
/// An effect's icon: that of the skill that names it (game::effect_skill).
[[nodiscard]] int effect_icon(uint32_t buff_id, uint32_t source_skill = 0) noexcept;

/// The sheet's slots: how many, and their side in pixels.
[[nodiscard]] int icon_slot_count() noexcept;
[[nodiscard]] int icon_sheet_px() noexcept;
/// A slot's pixels, icon_sheet_px() square, RGBA with straight alpha. Empty for
/// kNoIcon, a slot past the sheet, or a sheet that did not decode.
[[nodiscard]] std::span<const uint8_t> icon_pixels(int slot);

/// The app's own mark, wings around a crystal as app.ico draws it, `app_mark_px()`
/// square, RGBA with straight alpha. Not the game's art: this project's drawing, from
/// tools/gen_app_icon.py (app_mark.inc), kept apart from the sheet. Empty if it did
/// not decode.
[[nodiscard]] int app_mark_px() noexcept;
[[nodiscard]] std::span<const uint8_t> app_mark_pixels();

/// `src` (`src_px` square, RGBA, straight alpha) resized to `dst_px` square by the
/// area each destination pixel covers, weighted by alpha so that transparent
/// pixels lend no colour to an edge. `dst` rows are `dst_stride` bytes apart.
void resample_icon(std::span<const uint8_t> src, int src_px, uint8_t* dst, int dst_px, size_t dst_stride);

/// Adds every icon to `atlas` at `icon_px` (no larger than the sheet's), and the app's
/// mark at `mark_px` when that is above 0 (no larger than the mark's own), builds it
/// and paints them into its RGBA pixels, which the renderer then uploads with the
/// fonts. Call after adding the fonts, before the first frame or after invalidating
/// the renderer's font texture, from the thread that draws.
void build_atlas_with_icons(ImFontAtlas& atlas, float icon_px, float mark_px = 0.0f);

/// Draws an icon from the current context's font atlas, `size` square at (x, y).
/// Nothing for kNoIcon, or on an atlas built without icons.
void draw_icon(ImDrawList* dl, int slot, float x, float y, float size, float alpha = 1.0f);

/// Draws the app's mark from the current context's font atlas, `size` square at
/// (x, y). False, having drawn nothing, on an atlas built without it.
bool draw_app_mark(ImDrawList* dl, float x, float y, float size);

} // namespace aion2::app
