#pragma once

#include "app/app_settings.hpp"
#include "app/overlay_view.hpp"
#include "common/micros.hpp"
#include "ping/ping_tracker.hpp"
#include "pipeline/pipeline.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aion2::app {

/// One frame's inputs for the ping overlay. The snapshot may be null before the first.
struct PingFrameInput {
    const pipeline::LiveSnapshot* snapshot{nullptr};
    const PingSettings* settings{nullptr};
    /// The settings' scale times the monitor's DPI scale.
    float scale{1.0f};
};

/// How a ping reads against the settings' two thresholds.
enum class PingLevel : uint8_t { Good, Warn, Bad };

[[nodiscard]] PingLevel ping_level(float ms, const PingSettings& settings) noexcept;

/// The ping to show at `now`: the latest pong's, while it is the latest pong that
/// was measured and it is not stale (PingTracker::kPingStale).
[[nodiscard]] std::optional<float> shown_ping(const ping::PingSnapshot& ping, Micros now) noexcept;

/// Why shown_ping() gives nothing, in a few words; empty when it gives one.
[[nodiscard]] const char* ping_status(const ping::PingSnapshot& ping, Micros now) noexcept;

/// "87 ms", and "3.6 ms" under ten.
[[nodiscard]] std::string format_ms(float ms);

/// How the figures' line is laid out: the ping, and the jitter beside it or under it.
struct PingRowLayout {
    int rows{1};
    /// The line's width: both side by side, or the wider of the two.
    float width{0.0f};

    bool operator==(const PingRowLayout&) const = default;
};

/// The ping (`ping_w` wide) and, with `jitter_w` above 0, the jitter `gap` after it
/// when both fit in `max_w`, else on a line of its own.
[[nodiscard]] PingRowLayout ping_row_layout(float ping_w, float jitter_w, float gap, float max_w) noexcept;

/// One second of the graph: negative where that second has no figure.
struct PingGraphColumn {
    float ping_ms{-1.0f};
    float late_ms{-1.0f};

    bool operator==(const PingGraphColumn&) const = default;
};

/// The graph as numbers: no ImGui here, so the tests check it on the Mac.
struct PingGraph {
    /// Oldest first; the last is the second `now` is in.
    std::vector<PingGraphColumn> columns;
    /// The figure at the top edge (ping_graph_top).
    float top_ms{0.0f};
};

/// The seconds the graph spans: PingTracker::kGraphWindow.
inline constexpr size_t kPingGraphColumns = 120;

/// A round figure at or above `highest`, 50 ms at least, so the scale does not
/// twitch with every sample.
[[nodiscard]] float ping_graph_top(float highest) noexcept;

/// The last kPingGraphColumns seconds up to `now`, each second in its column.
[[nodiscard]] PingGraph ping_graph(const ping::PingSnapshot& ping, Micros now);

/// Draws the ping overlay into the frame the caller has begun: the ping, coloured by
/// its level, the jitter, and the graph (the ping as a line over each second's
/// lateness as bars) with the least, average and most under it.
/// - **Unlocked,** one window fills the display: a header to drag it by, then the
///   figures.
/// - **Locked,** only the figures' panel is drawn, where it sat under the header and
///   at the size it had: locking changes nothing but the header going. The strip the
///   header took stays see-through.
/// - **Without the graph,** that panel is as large as its line of figures, however
///   large the window is.
/// A no-op where ImGui is not built (HAVE_IMGUI unset).
OverlayActions draw_ping(const PingFrameInput& input);

} // namespace aion2::app
