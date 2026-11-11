#pragma once

#include "app/app_settings.hpp"
#include "app/overlay_view.hpp"
#include "combat_power/power_tracker.hpp"
#include "pipeline/pipeline.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aion2::app {

/// One frame's inputs for the combat power overlay. The snapshot may be null before
/// the first.
struct PowerFrameInput {
    const pipeline::LiveSnapshot* snapshot{nullptr};
    const PowerSettings* settings{nullptr};
    /// The settings' scale times the monitor's DPI scale.
    float scale{1.0f};
};

/// "134,667"; "-" for a player whose record carried none.
[[nodiscard]] std::string format_power(uint32_t combat_power);

/// What the overlay draws, as data: no ImGui here, so the tests check it on the Mac.
struct PowerLayout {
    /// The target on a line of its own: only while the list is not shown, since a
    /// listed target is marked in its row instead. `id` 0 for none.
    combat_power::PowerRow target_line;
    /// The map's players, strongest first; empty in a crowd or with the list off.
    std::vector<combat_power::PowerRow> rows;
    /// The row to mark as the target; 0 for none.
    combat_power::EntityId target{0};
    /// The strongest row's figure, which the bars are measured against; 0 for none.
    uint32_t top{0};

    [[nodiscard]] bool empty() const noexcept { return target_line.id == 0 && rows.empty(); }
};

/// The list while `show_list` is on and the map's players number list_max_players or
/// fewer, with the target marked in it; else the target alone.
[[nodiscard]] PowerLayout power_layout(const combat_power::PowerSnapshot& power, const PowerSettings& settings);

/// Draws the combat power overlay into the frame the caller has begun: a header to
/// drag it by while unlocked, then the target or the map's players, each with a bar
/// measured against the strongest. A no-op where ImGui is not built (HAVE_IMGUI
/// unset).
OverlayActions draw_power(const PowerFrameInput& input);

} // namespace aion2::app
