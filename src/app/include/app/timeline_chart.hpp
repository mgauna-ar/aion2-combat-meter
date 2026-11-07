#pragma once

// The Timeline tab's chart, as numbers: which lines it draws and their points. No
// ImGui here, so the tests check it on the Mac; view_timeline.cpp draws it.

#include "game/classes.hpp"
#include "meter/fight_detail.hpp"
#include "meter/timeline.hpp"
#include "meter/types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace aion2::app {

/// What the Timeline tab shows, as its controls and legend set it.
struct TimelineView {
    meter::TimelineMetric metric{meter::TimelineMetric::Damage};
    /// Each point averages this many seconds around it.
    size_t smoothing_s{15};
    /// The party-wide buff bands behind the lines.
    bool show_buffs{true};
    /// The boss's HP line, on its own scale.
    bool show_boss{true};
    /// Lines turned off in the legend, by name: ids change from one fight to the next.
    std::set<std::string> hidden;
};

/// One player's line.
struct TimelineLine {
    meter::EntityId id{0};
    std::string name;
    game::AionClass cls{game::AionClass::Unknown};
    bool is_local{false};
    /// The metric over the whole fight: what orders the lines.
    double total{0.0};
    /// Per second, one value a point.
    std::vector<float> values;
    float peak{0.0f};
};

/// The chart as last built, and what it was built from.
struct TimelineChart {
    /// Held, so a rebuild is due exactly when the fight's detail is another one.
    std::shared_ptr<const meter::FightDetail> source;
    meter::TimelineMetric metric{meter::TimelineMetric::Damage};
    size_t smoothing_s{0};
    size_t points{0};
    /// Seconds the chart spans: the timeline's, which can run past the fight's
    /// duration (deaths and the boss's last HP after the last hit).
    size_t length_s{0};
    /// Most first; players without any of the metric are left out.
    std::vector<TimelineLine> lines;
    /// The boss's HP, 0..1, a point each as many as a line has; below 0 before its
    /// first figure. Empty with no boss line.
    std::vector<float> boss_hp;
};

/// Rebuilds `chart` for `fight` at `points` points across when what it shows
/// changed (another detail, metric, smoothing or width). True when it rebuilt.
bool update_timeline_chart(TimelineChart& chart, const meter::FightSummary& fight, const TimelineView& view,
                           size_t points);

/// The top of four gridlines that each read cleanly and leave little room above `peak`.
[[nodiscard]] double chart_top(double peak);
/// Seconds between time labels, at least `min_gap` pixels apart on a `plot_w` wide plot.
[[nodiscard]] double time_step(double length_s, float plot_w, float min_gap);

} // namespace aion2::app
