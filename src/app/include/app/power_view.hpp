#pragma once

#include "app/app_settings.hpp"
#include "app/overlay_view.hpp"
#include "combat_power/power_tracker.hpp"
#include "common/micros.hpp"
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

/// "135k", "98.3k", "4.76k", "1.23M": the overlay's, where the room is. "-" for none.
[[nodiscard]] std::string format_power_short(uint32_t combat_power);

/// "12 m", "1,240 m"; "-" for a distance not known (negative).
[[nodiscard]] std::string format_distance(float metres);

/// What the overlay draws, as data: no ImGui here, so the tests check it on the Mac.
struct PowerLayout {
    /// The target on a line of its own, above the list: only while they are not in
    /// it, since a listed target is marked in its row instead. `id` 0 for none.
    combat_power::PowerRow target_line;
    /// The map's players, strongest first: list_max_players of them at most. Empty
    /// with the list off.
    std::vector<combat_power::PowerRow> rows;
    /// The row to mark as the target; 0 for none.
    combat_power::EntityId target{0};
    /// The strongest figure drawn, which the bars are measured against; 0 for none.
    uint32_t top{0};
    /// The map's players the list leaves out: a crowd's weaker ones, or its farther
    /// ones. 0 with the list off.
    uint32_t more{0};
    /// Each row's distance is drawn: sort_by_distance, once the local player's
    /// position is known. Until then the list stays strongest first.
    bool distances{false};
    /// That position is too old to measure from (PowerSnapshot::local_position_stale):
    /// each distance is drawn as "?", and the list is strongest first until they act.
    bool stale{false};

    [[nodiscard]] bool empty() const noexcept { return target_line.id == 0 && rows.empty(); }
};

/// With `show_list`, the map's list_max_players strongest (nearest, with
/// `sort_by_distance`), the enemies among them first (with `enemies_first`), the
/// target marked among them or on a line above them; without it, the target alone.
[[nodiscard]] PowerLayout power_layout(const combat_power::PowerSnapshot& power, const PowerSettings& settings);

/// Draws the combat power overlay into the frame the caller has begun: a header to
/// drag it by while unlocked, then the target and the map's strongest players, each with a
/// bar measured against the strongest, and how many more there are. A no-op where ImGui is not built (HAVE_IMGUI
/// unset).
OverlayActions draw_power(const PowerFrameInput& input);

} // namespace aion2::app
