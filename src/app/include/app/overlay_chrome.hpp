#pragma once

#include "app/display_format.hpp"
#include "app/imgui_guard.hpp"
#include "app/overlay_view.hpp"

#include <string>

// What the meter, aura, ping and combat power overlays share, so they stay one look:
// the fonts, the window's frame, the header and its buttons, the resize grip, and the
// bars. All of it draws, so all of it is ImGui's: nothing here exists where ImGui is
// not built (HAVE_IMGUI unset), as nothing there calls it.

#ifdef HAVE_IMGUI

namespace aion2::app::chrome {

[[nodiscard]] ImVec4 rgba(Rgb c, float a = 1.0f);
[[nodiscard]] ImU32 u32(Rgb c, float a = 1.0f);
/// `c` mixed `t` of the way to white.
[[nodiscard]] Rgb lighten(Rgb c, float t) noexcept;

/// An overlay's fonts and icons, into the current context's atlas, which it clears
/// first: the regular font, a bold one as font 1, the Lucide glyphs (ui/icons.hpp)
/// merged into both, then the game's icons at `icon_px` (build_atlas_with_icons).
/// - An empty or unreadable `regular_ttf` falls back to ImGui's own font, and the
///   result is false: the caller scales that one with FontGlobalScale.
/// - Without `bold_ttf`, bold text draws in the regular font.
/// The window (overlay_window.cpp) and the screenshot tool both build theirs here,
/// so a picture shows what the window draws.
bool build_overlay_fonts(const std::string& regular_ttf, const std::string& bold_ttf, float font_px, float icon_px);

/// Bold text for as long as it lives: figures and names the eye should land on.
class BoldScope {
public:
    BoldScope();
    ~BoldScope();
    BoldScope(const BoldScope&) = delete;
    BoldScope& operator=(const BoldScope&) = delete;
};

/// How wide `s` is in the bold font, for laying a line out before it is drawn.
[[nodiscard]] float bold_text_width(const std::string& s);

/// Text ending at `x_right`, in the current font.
void text_right(ImDrawList* dl, float x_right, float y, ImU32 col, const std::string& s);
/// Text with a dark shadow, readable over any icon.
void shadowed_text(ImDrawList* dl, ImVec2 at, const std::string& text, float scale);
/// `s` cut with "..." to fit `width` in the current font, by whole UTF-8 code points.
[[nodiscard]] std::string fit(const std::string& s, float width);

/// Begins an overlay's one window, filling the display: rounded, bordered, its
/// background at `opacity`. The border is full while the overlay can be moved
/// (`interactive`), so an empty one can still be found. Always pair with end().
bool begin(const char* id, float opacity, float scale, bool interactive);
/// The same over part of the display, from `pos`, `size` large. What it leaves
/// uncovered stays see-through: an overlay window is transparent where nothing is
/// drawn, and click-through all over while locked.
bool begin(const char* id, float opacity, float scale, bool interactive, ImVec2 pos, ImVec2 size);
void end();

/// The window's padding and its items' spacing, as begin() sets them.
[[nodiscard]] ImVec2 window_padding(float scale) noexcept;
[[nodiscard]] ImVec2 item_spacing(float scale) noexcept;
/// How far a header (a title line, finish_header) pushes the content under it down.
/// An overlay that draws no header while locked can begin() that much lower, so its
/// content sits where it sat unlocked, at the size it had.
[[nodiscard]] float header_height(float scale);
/// The width dot() takes.
[[nodiscard]] float dot_width(float scale) noexcept;

/// A filled circle a text line high, as an item: a status light before a label.
void dot(Rgb color, float scale, float alpha = 1.0f);
/// A one-pixel rule across the content width.
void hairline();
/// A dim caption with a rule running from it to the right edge.
void section_title(const std::string& title, float scale);

/// The header's buttons, after the caller drew its title on the current line: the
/// app and lock icons at the right edge, left out when the title leaves no room. Sets
/// `out`'s lock and open_app, and its drag_area to the header's empty space. Ends
/// with a rule under the header.
struct HeaderButtons {
    float scale{1.0f};
    /// False draws no buttons and leaves no drag area: a locked overlay.
    bool interactive{true};
    const char* app_tooltip{""};
    const char* lock_tooltip{""};
};
void finish_header(const HeaderButtons& buttons, OverlayActions& out);

/// The corner grip an unlocked overlay resizes by: where it is, having drawn it.
[[nodiscard]] ClientRect resize_grip(float scale);

/// A bar from `min` to `max`, `fraction` of it filled in `color`: a faint track, a
/// fill that brightens toward its end, and a bright edge where it ends.
struct BarStyle {
    float rounding{3.0f};
    /// The fill's opacity over the track.
    float alpha{0.60f};
    bool track{true};
};
void draw_bar(ImDrawList* dl, ImVec2 min, ImVec2 max, float fraction, Rgb color, float scale,
              const BarStyle& style = {});

} // namespace aion2::app::chrome

#endif // HAVE_IMGUI
