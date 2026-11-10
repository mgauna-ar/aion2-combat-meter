#pragma once

// The desktop window's pages, and the settings rows they share. Called by
// draw_app() between its sidebar and the end of the frame.

#include "aion2/plugin_registry.hpp"
#include "app/app_frame.hpp"
#include "app/imgui_guard.hpp"

#include <cstdint>
#include <functional>

namespace aion2::app::ui {

#ifdef HAVE_IMGUI

void render_dashboard(const AppFrameInput& in, AppViewState& state, AppActions& out);
void render_meter_page(const AppFrameInput& in, AppViewState& state, AppActions& out);
void render_auras_page(const AppFrameInput& in, AppViewState& state, AppActions& out);
void render_ping_page(const AppFrameInput& in, AppViewState& state, AppActions& out);
void render_settings_page(const AppFrameInput& in, AppViewState& state, AppActions& out);

/// A plugin's icon and page, for the sidebar and the dashboard.
[[nodiscard]] const char* plugin_icon(plugins::PluginId id) noexcept;
[[nodiscard]] AppPage plugin_page(plugins::PluginId id) noexcept;

/// A plugin page's title band: its icon, its name from the descriptor table, a
/// subtitle, and its master switch on the right, so the switch is in the same place
/// on every plugin's page. Flipping it saves and reaches the engine at once.
void render_plugin_header(const AppFrameInput& in, AppActions& out, plugins::PluginId id, const char* icon,
                          const char* subtitle);

/// Draws the "switched off" panel, with the one button that brings the plugin back,
/// and returns true when the caller should draw nothing else.
[[nodiscard]] bool render_plugin_disabled_gate(const AppFrameInput& in, AppActions& out, plugins::PluginId id);

/// What else a settings row's edit must reach, besides config.json.
enum class Applies : uint8_t {
    /// Only the file: read where it is used, or at the next start.
    Saved,
    /// The overlay window, at once: shown while a slider is dragged.
    Overlay,
    /// The engine thread, through AppRuntime::update_settings, once let go of.
    Engine,
};

/// A settings row with a switch that edits `value` in place. Returns true when it
/// flipped, having marked `out` to save now and apply where `applies` says.
bool setting_switch(const char* label, const char* help, bool* value, AppActions& out,
                    Applies applies = Applies::Saved);
/// A settings row with a slider. A drag marks the settings changed (and previews an
/// overlay edit); letting go saves them and hands a meter edit to the engine. `def`
/// is the setting's default: while the value is off it, the row shows a button that
/// puts it back. True when the value changed, by the slider or by that button.
bool setting_slider(const char* label, const char* help, float* value, float min, float max, float def,
                    const char* format, AppActions& out, Applies applies = Applies::Saved);
bool setting_slider(const char* label, const char* help, double* value, double min, double max, double def,
                    const char* format, AppActions& out, Applies applies = Applies::Saved);
bool setting_slider(const char* label, const char* help, int* value, int min, int max, int def, const char* format,
                    AppActions& out, Applies applies = Applies::Saved);

/// The fold at the foot of a settings card that keeps its fine tuning out of the
/// way: closed until asked for. True while open; the caller then emits its rows.
[[nodiscard]] bool advanced_section(const char* id);

/// One overlay's settings, for the card every plugin page shows the same way.
struct OverlayControls {
    /// The card's "##" id, which also keeps one page's rows apart from another's.
    const char* id;
    bool* locked;
    float* opacity;
    float default_opacity;
    float* scale;
    bool* hide_when_game_unfocused;
    bool* show_in_dungeons_only;
    /// The AppActions flag that puts the overlay back where it starts.
    bool* reset_position;
    const char* position_help;
    /// Rows of the page's own: above Lock, after it, and among the conditions.
    std::function<void()> before{};
    std::function<void()> look{};
    std::function<void()> conditions{};
};

/// The "In-game overlay" card: lock, look and position, then when it shows.
void overlay_settings_card(const OverlayControls& c, AppActions& out);

#endif // HAVE_IMGUI

} // namespace aion2::app::ui
