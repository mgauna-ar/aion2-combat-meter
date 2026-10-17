#pragma once

// The desktop window's look: colours, spacing and fonts. Ported from ffxiv-hub's
// slate theme, so the two apps read the same. The overlay keeps its own palette
// (display_format.hpp).

#include "meter/types.hpp"

#include <cstdint>
#include <string>

// Forward declared so this header does not pull imgui.h into everything that uses it.
struct ImFont;

namespace aion2::app::ui {

namespace colors {

/// Packs a colour the way IM_COL32 does (R in the low byte), so a token can be
/// written in readable channel order.
constexpr uint32_t rgb(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}

// Surfaces, darkest to lightest.
constexpr uint32_t Canvas = rgb(0x0B, 0x0E, 0x14);
constexpr uint32_t SurfaceSunken = rgb(0x09, 0x0C, 0x12);
constexpr uint32_t Surface = rgb(0x16, 0x1B, 0x26);
constexpr uint32_t SurfaceLow = rgb(0x11, 0x15, 0x1E);
constexpr uint32_t SurfaceRaised = rgb(0x1C, 0x22, 0x31);
constexpr uint32_t SidebarTop = rgb(0x0F, 0x13, 0x1C);
constexpr uint32_t SidebarBottom = rgb(0x0A, 0x0D, 0x13);

// Lines.
constexpr uint32_t Border = rgb(0x23, 0x2B, 0x3D);
constexpr uint32_t BorderStrong = rgb(0x2A, 0x32, 0x45);
constexpr uint32_t BorderSubtle = rgb(0x1A, 0x21, 0x30);

// Text.
constexpr uint32_t TextPrimary = rgb(0xF3, 0xF4, 0xF6);
constexpr uint32_t TextBody = rgb(0xC9, 0xD1, 0xDE);
constexpr uint32_t TextMuted = rgb(0x9C, 0xA3, 0xAF);
constexpr uint32_t TextDim = rgb(0x6B, 0x72, 0x80);
constexpr uint32_t TextFaint = rgb(0x4B, 0x55, 0x63);
constexpr uint32_t White = rgb(0xFF, 0xFF, 0xFF);

// Accent.
constexpr uint32_t Accent = rgb(0x3B, 0x82, 0xF6);
constexpr uint32_t AccentHover = rgb(0x60, 0xA5, 0xFA);
constexpr uint32_t AccentDeep = rgb(0x1D, 0x4E, 0xD8);

// Semantic.
constexpr uint32_t Success = rgb(0x10, 0xB9, 0x81);
constexpr uint32_t SuccessLight = rgb(0x34, 0xD3, 0x99);
constexpr uint32_t Warning = rgb(0xF5, 0x9E, 0x0B);
constexpr uint32_t WarningLight = rgb(0xFB, 0xBF, 0x24);
constexpr uint32_t Danger = rgb(0xEF, 0x44, 0x44);
constexpr uint32_t DangerLight = rgb(0xF8, 0x71, 0x71);
constexpr uint32_t Violet = rgb(0xA8, 0x55, 0xF7);
/// A killed boss's outcome dot.
constexpr uint32_t Gold = rgb(0xF5, 0xC5, 0x42);
/// DPS figures, as the overlay draws them.
constexpr uint32_t Dps = rgb(0xF5, 0x8C, 0x33);
constexpr uint32_t Heal = rgb(0x34, 0xD3, 0x99);

/// Same colour at another opacity. Alpha is absolute, not multiplied in.
[[nodiscard]] constexpr uint32_t with_alpha(uint32_t color, float a) {
    const auto alpha = static_cast<uint32_t>((a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a)) * 255.0f);
    return (color & 0x00FFFFFFu) | (alpha << 24);
}

} // namespace colors

/// Layout constants in unscaled (96 DPI) pixels. Read them through m() so every
/// call site picks up the DPI scale the same way.
namespace metrics {
constexpr float Gutter = 12.0f;  // between siblings in a row or grid
constexpr float PagePad = 18.0f;  // page edge to content
constexpr float CardPad = 14.0f;  // card edge to content
constexpr float CardRadius = 8.0f;
constexpr float ChipSize = 28.0f;  // icon chip square
constexpr float ChipSizeLg = 36.0f;
constexpr float RowHeight = 30.0f;  // one settings row's control
constexpr float ButtonH = 30.0f;
constexpr float ButtonSm = 110.0f;  // button widths are minimums; see button_width()
constexpr float ButtonMd = 150.0f;
constexpr float ButtonLg = 200.0f;
constexpr float ControlW = 186.0f;  // right-hand control column in a settings row
constexpr float StatTileH = 92.0f;
constexpr float SidebarW = 220.0f;
constexpr float NavItemH = 38.0f;
constexpr float RailW = 300.0f;  // the pull list on the Combat Meter page
constexpr float GridMinCol = 430.0f;  // a settings grid drops to one column below this
constexpr float TileMinW = 150.0f;  // a stat tile row wraps rather than go below this
constexpr float HeaderActionMinW = 360.0f;  // below this a page header's action drops to its own line
constexpr float WindowMinW = 880.0f;  // smallest client area the layout is designed for
constexpr float WindowMinH = 600.0f;
constexpr float WindowDefaultW = 1100.0f;  // client area the window opens at
constexpr float WindowDefaultH = 700.0f;
}  // namespace metrics

/// The slate styling on the current ImGui context, at the scale set_ui_scale() set.
void apply_slate_theme();

/// The DPI scale (1.0 = 96 DPI) fonts and layout constants are sized by.
void set_ui_scale(float scale);
[[nodiscard]] float ui_scale();

/// An unscaled pixel constant (a metrics:: value, or a one-off) at the current DPI.
[[nodiscard]] inline float m(float px) { return px * ui_scale(); }

/// The bold font for headers and figures; null until the fonts are loaded.
void set_bold_font(ImFont* font);
[[nodiscard]] ImFont* bold_font();

/// A class's colour (display_format.hpp's, packed), at `alpha`.
[[nodiscard]] uint32_t class_color_u32(meter::AionClass cls, float alpha = 1.0f);

/// Loads the window's fonts, regular and bold, each with the icon glyphs merged
/// in, at 16 px times `dpi_scale`, and applies the theme. A font file that does
/// not load falls back to ImGui's own. Call once, right after CreateContext().
void setup_fonts_and_theme(const std::string& regular_ttf, const std::string& bold_ttf, float dpi_scale);

} // namespace aion2::app::ui
