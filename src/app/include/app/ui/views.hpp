#pragma once

// The desktop window's pages, and the settings rows they share. Called by
// draw_app() between its sidebar and the end of the frame.

#include "app/app_frame.hpp"
#include "app/imgui_guard.hpp"

#include <cstdint>

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

void render_dashboard(const AppFrameInput& in, AppViewState& state, AppActions& out);
void render_meter_page(const AppFrameInput& in, AppViewState& state, AppActions& out);
void render_settings_page(const AppFrameInput& in, AppViewState& state, AppActions& out);

/// What else a settings row's edit must reach, besides config.json.
enum class Applies : uint8_t {
    /// Only the file: read where it is used, or at the next start.
    Saved,
    /// The overlay window, at once: shown while a slider is dragged.
    Overlay,
    /// The engine thread, through AppRuntime::update_settings, once let go of.
    Meter,
};

/// A settings row with a switch that edits `value` in place. Returns true when it
/// flipped, having marked `out` to save now and apply where `applies` says.
bool setting_switch(const char* label, const char* help, bool* value, AppActions& out,
                    Applies applies = Applies::Saved);
/// A settings row with a slider. A drag marks the settings changed (and previews an
/// overlay edit); letting go saves them and hands a meter edit to the engine.
bool setting_slider(const char* label, const char* help, float* value, float min, float max, const char* format,
                    AppActions& out, Applies applies = Applies::Saved);
bool setting_slider(const char* label, const char* help, double* value, double min, double max, const char* format,
                    AppActions& out, Applies applies = Applies::Saved);
bool setting_slider(const char* label, const char* help, int* value, int min, int max, const char* format,
                    AppActions& out, Applies applies = Applies::Saved);

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
