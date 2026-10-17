#pragma once

#include "app/app_settings.hpp"
#include "meter/types.hpp"
#include "pipeline/pipeline.hpp"

#include <string>

namespace aion2::app {

/// A rectangle in the overlay window's client pixels.
struct ClientRect {
    float x0{0};
    float y0{0};
    float x1{0};
    float y1{0};

    [[nodiscard]] bool contains(float x, float y) const noexcept { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

/// One frame's inputs. The snapshot may be null before the first one.
struct OverlayFrameInput {
    const pipeline::LiveSnapshot* snapshot{nullptr};
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
    /// Open the app window: the history and every setting live there.
    bool open_app{false};
    /// Where a press drags the window (empty space in the header) and where it
    /// resizes it (the corner grip). The window's hit test uses them while unlocked.
    ClientRect drag_area;
    ClientRect resize_grip;
};

/// Draws the meter's live fight as one ImGui window filling the display, into the
/// frame the caller has begun: the party ranked, the boss's HP and the timer. Saved
/// fights and settings are the app window's. A no-op where ImGui is not built
/// (HAVE_IMGUI unset).
OverlayActions draw_overlay(const OverlayFrameInput& input);

} // namespace aion2::app
