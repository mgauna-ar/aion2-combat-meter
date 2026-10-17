#pragma once

namespace aion2::app::ui {

/// Merges the embedded icon glyphs (icons.hpp) into the font added last, at
/// `size_px`. Call once per font that shows icons, right after adding it. If the
/// atlas rejects the data the icon macros draw as blanks; every label that uses
/// one also has text.
void load_icon_font(float size_px);

} // namespace aion2::app::ui
