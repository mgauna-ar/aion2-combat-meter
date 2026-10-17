#pragma once

// The desktop window's contents: a sidebar and three pages (Dashboard, Combat
// Meter, App Settings), drawn with ImGui into a frame the caller has begun. Like
// the overlay it reads its inputs, edits only the settings it is handed, and
// reports everything else as actions, so the Win32 window that hosts it stays
// small and the screenshot tool can draw it on the Mac.

#include "app/app_settings.hpp"
#include "app/updater.hpp"
#include "meter/history_store.hpp"
#include "meter/types.hpp"
#include "pipeline/pipeline.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aion2::app {

enum class AppPage : uint8_t { Dashboard, Meter, Settings };

/// What the window remembers between frames.
struct AppViewState {
    AppPage page{AppPage::Meter};
    /// The saved fight on view; empty for the live one.
    std::string selected_file;
    std::optional<meter::FightSummary> selected_fight;
    /// The player whose breakdown is open on each tab; 0 for none.
    meter::EntityId damage_player{0};
    meter::EntityId heal_player{0};
    meter::EntityId taken_player{0};
    /// A Combat Meter tab to open on the next frame (0 Damage .. 3 Settings); -1 for none.
    int open_meter_tab{-1};
};

/// One frame's inputs. Pointers may be null before the first snapshot.
struct AppFrameInput {
    const pipeline::LiveSnapshot* snapshot{nullptr};
    /// Saved fights, oldest first.
    const std::vector<meter::HistoryEntry>* history{nullptr};
    std::string source_status;
    bool recording{false};
    std::string recording_path;
    /// Edited in place by the settings pages; AppActions says what changed.
    AppSettings* settings{nullptr};
    bool start_with_windows{false};
    /// Shown on the dashboard, in UTF-8.
    std::string data_dir;
    std::string log_path;
    /// The updater's state; unset where there is none (the screenshots).
    std::optional<UpdateStatus> update;
};

/// Files and folders the window asks to open.
enum class OpenTarget : uint8_t { None, DataFolder, History, Recordings, Config, Log };

/// What the user did this frame, for the caller to carry out.
struct AppActions {
    /// The settings were edited: save them soon.
    bool settings_changed{false};
    /// Save them now: a toggle flipped, or a slider let go of.
    bool save_settings{false};
    /// Apply the overlay settings to the overlay window now (a preview while dragging).
    bool overlay_changed{false};
    /// Hand the meter and history settings to the engine.
    bool meter_changed{false};
    std::optional<bool> start_with_windows;
    bool end_fight{false};
    bool toggle_recording{false};
    bool clear_history{false};
    bool reset_overlay_position{false};
    OpenTarget open{OpenTarget::None};
    /// A saved fight to load into AppViewState::selected_fight.
    std::string load_file;
    /// Updates: check now, download and install the release found, open its page.
    bool check_updates{false};
    bool install_update{false};
    bool open_release_notes{false};
};

/// Draws the window's contents filling the display. A no-op where ImGui is not
/// built (HAVE_IMGUI unset).
AppActions draw_app(const AppFrameInput& input, AppViewState& state);

} // namespace aion2::app
