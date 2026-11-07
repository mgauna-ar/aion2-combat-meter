#include "app/timeline_chart.hpp"

#include <algorithm>
#include <cmath>

namespace aion2::app {

bool update_timeline_chart(TimelineChart& chart, const meter::FightSummary& fight, const TimelineView& view,
                           size_t points) {
    const std::shared_ptr<const meter::FightDetail>& detail = fight.detail;
    const size_t length =
        detail == nullptr ? 0 : std::clamp<size_t>(detail->timeline.seconds, 1, meter::kTimelineMaxSeconds);
    points = std::max<size_t>(points, 2);
    if (chart.source == detail && chart.metric == view.metric && chart.smoothing_s == view.smoothing_s &&
        chart.points == points && chart.length_s == length) {
        return false;
    }
    chart = TimelineChart{detail, view.metric, view.smoothing_s, points, length, {}, {}};
    if (detail == nullptr) return true;

    for (const meter::CombatantTotals& c : fight.combatants) {
        const meter::TimelineRow* row = detail->timeline.row(c.id);
        if (row == nullptr) continue;
        TimelineLine line;
        for (size_t i = 0; i < row->bins.size() && i < length; ++i) {
            line.total += meter::timeline_value(row->bins[i], view.metric);
        }
        if (line.total <= 0.0) continue;
        line.id = c.id;
        line.name = c.name;
        line.cls = c.cls;
        line.is_local = c.is_local;
        line.values = meter::downsample(meter::smoothed_series(row->bins, length, view.metric, view.smoothing_s), points);
        line.peak = *std::max_element(line.values.begin(), line.values.end());
        chart.lines.push_back(std::move(line));
    }
    std::sort(chart.lines.begin(), chart.lines.end(), [](const TimelineLine& a, const TimelineLine& b) {
        if (a.total != b.total) return a.total > b.total;
        return a.id < b.id;
    });

    const meter::BossHpSeries& boss = detail->timeline.boss_hp;
    if (!boss.empty()) {
        // As many points as a line has: one a second, or one a span of seconds, each
        // the figure at the span's end.
        const size_t n = std::min(length, points);
        chart.boss_hp.reserve(n);
        for (size_t p = 0; p < n; ++p) {
            const size_t last = std::max((p + 1) * length / n, size_t{1}) - 1;
            const int permille = boss.at(last);
            chart.boss_hp.push_back(permille < 0 ? -1.0f : static_cast<float>(permille) / 1000.0f);
        }
    }
    return true;
}

double chart_top(double peak) {
    const double step = std::max(peak, 1.0) / 4.0;
    const double magnitude = std::pow(10.0, std::floor(std::log10(step)));
    for (const double nice : {1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0}) {
        if (nice * magnitude >= step) return nice * magnitude * 4.0;
    }
    return magnitude * 40.0;
}

double time_step(double length_s, float plot_w, float min_gap) {
    for (const double step : {10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 900.0, 1800.0}) {
        if (static_cast<double>(plot_w) * step / length_s >= min_gap) return step;
    }
    return 3600.0;
}

} // namespace aion2::app
