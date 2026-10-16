#pragma once

#include "app/app_settings.hpp"
#include "meter/history_store.hpp"
#include "meter/types.hpp"
#include "pipeline/pipeline.hpp"

#include <optional>
#include <string>
#include <vector>

namespace aion2::app {

/// A rectangle in the overlay window's client pixels.
struct ClientRect {
    float x0{0};
    float y0{0};
    float x1{0};
    float y1{0};

    [[nodiscard]] bool contains(float x, float y) const noexcept { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

/// What the overlay shows between frames.
struct OverlayViewState {
    enum class Tab : uint8_t { Live, History };
    Tab tab{Tab::Live};
    /// The saved fight on view in History; empty for none.
    std::string selected_file;
    std::optional<meter::FightSummary> selected_fight;
};

/// One frame's inputs. Pointers may be null before the first snapshot.
struct OverlayFrameInput {
    const pipeline::LiveSnapshot* snapshot{nullptr};
    const std::vector<meter::HistoryEntry>* history{nullptr};
    std::string source_status;
    bool recording{false};
    const OverlaySettings* settings{nullptr};
    /// The settings' scale times the monitor's DPI scale.
    float scale{1.0f};
};

/// What the user did this frame, for the window to carry out.
struct OverlayActions {
    bool end_fight{false};
    bool lock{false};
    bool toggle_recording{false};
    bool open_history_folder{false};
    bool hide{false};
    /// A saved fight to load into OverlayViewState::selected_fight.
    std::string load_file;
    /// Where a press drags the window (empty space in the header) and where it
    /// resizes it (the corner grip). The window's hit test uses them while unlocked.
    ClientRect drag_area;
    ClientRect resize_grip;
};

/// Draws the meter as one ImGui window filling the display, into the frame the
/// caller has begun. A no-op where ImGui is not built (HAVE_IMGUI unset).
OverlayActions draw_overlay(const OverlayFrameInput& input, OverlayViewState& state);

} // namespace aion2::app
