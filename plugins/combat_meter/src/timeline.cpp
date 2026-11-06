#include "meter/timeline.hpp"

#include <algorithm>
#include <limits>

namespace aion2::meter {

namespace {

/// Each value the average of the `window` values centred on it that exist.
std::vector<double> box_average(const std::vector<double>& values, size_t window) {
    const size_t n = values.size();
    std::vector<double> prefix(n + 1, 0.0);
    for (size_t i = 0; i < n; ++i) prefix[i + 1] = prefix[i] + values[i];
    std::vector<double> averaged(n);
    for (size_t i = 0; i < n; ++i) {
        const size_t lo = i >= window / 2 ? i - window / 2 : 0;
        const size_t hi = std::min(n - 1, i + (window - 1) / 2);
        averaged[i] = (prefix[hi + 1] - prefix[lo]) / static_cast<double>(hi - lo + 1);
    }
    return averaged;
}

} // namespace

int BossHpSeries::at(size_t second) const noexcept {
    if (permille.empty() || second < first_s) return -1;
    const size_t i = std::min(second - first_s, permille.size() - 1);
    return permille[i];
}

const TimelineRow* FightTimeline::row(EntityId id) const noexcept {
    const auto it = std::find_if(rows.begin(), rows.end(), [&](const TimelineRow& r) { return r.id == id; });
    return it == rows.end() ? nullptr : &*it;
}

uint32_t timeline_value(const TimelineBin& bin, TimelineMetric metric) noexcept {
    switch (metric) {
        case TimelineMetric::Healing: return bin.healing;
        case TimelineMetric::Taken: return bin.taken;
        case TimelineMetric::Damage: break;
    }
    return bin.damage;
}

void add_capped(uint32_t& into, uint64_t amount) noexcept {
    constexpr uint64_t kMax = std::numeric_limits<uint32_t>::max();
    into = static_cast<uint32_t>(std::min<uint64_t>(kMax, uint64_t{into} + std::min(amount, kMax)));
}

TimelineBin* timeline_bin(std::vector<TimelineBin>& bins, Micros start, Micros ts) {
    const uint64_t second = ts > start ? (ts - start) / kMicrosPerSecond : 0;
    if (second >= kTimelineMaxSeconds) return nullptr;
    const auto i = static_cast<size_t>(second);
    if (bins.size() <= i) bins.resize(i + 1);
    return &bins[i];
}

std::vector<float> smoothed_series(const std::vector<TimelineBin>& bins, size_t length, TimelineMetric metric,
                                   size_t window_s) {
    std::vector<double> values(length, 0.0);
    for (size_t i = 0; i < length && i < bins.size(); ++i) values[i] = timeline_value(bins[i], metric);
    // Two passes of half the window each weigh a second by how near the middle it
    // is, so a hit slides out of the average rather than dropping out of it at once.
    const size_t window = std::max<size_t>(window_s, 1);
    const size_t first = (window + 1) / 2;
    const std::vector<double> smoothed = box_average(box_average(values, first), window + 1 - first);
    std::vector<float> series(smoothed.size());
    std::transform(smoothed.begin(), smoothed.end(), series.begin(), [](double v) { return static_cast<float>(v); });
    return series;
}

std::vector<float> downsample(const std::vector<float>& values, size_t points) {
    if (points == 0) return {};
    if (values.size() <= points) return values;
    const size_t n = values.size();
    std::vector<float> out(points);
    for (size_t p = 0; p < points; ++p) {
        const size_t from = p * n / points;
        const size_t to = std::max(from + 1, (p + 1) * n / points);
        double sum = 0.0;
        for (size_t i = from; i < to; ++i) sum += values[i];
        out[p] = static_cast<float>(sum / static_cast<double>(to - from));
    }
    return out;
}

} // namespace aion2::meter
