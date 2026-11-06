#include "test_framework.hpp"
#include "meter_test_support.hpp"

#include "meter/death_log.hpp"
#include "meter/effect_uptime.hpp"
#include "meter/timeline.hpp"

#include <cstdint>
#include <limits>
#include <vector>

using namespace aion2;
using namespace aion2::meter;
using namespace aion2::test::meter_support;

TEST_CASE(Timeline, SmoothingWeighsTheMiddleOfItsWindow) {
    std::vector<TimelineBin> bins(7);
    bins[3].damage = 90;
    const std::vector<float> smoothed = smoothed_series(bins, 7, TimelineMetric::Damage, 5);
    TEST_ASSERT_EQ(smoothed.size(), size_t{7});
    // Five seconds weigh 1, 2, 3, 2, 1: the hit counts most where it landed.
    TEST_ASSERT_NEAR(smoothed[3], 30.0, 1e-4);
    TEST_ASSERT_NEAR(smoothed[2], 20.0, 1e-4);
    TEST_ASSERT_NEAR(smoothed[4], 20.0, 1e-4);
    TEST_ASSERT_NEAR(smoothed[1], 10.0, 1e-4);
    TEST_ASSERT_NEAR(smoothed[0], 0.0, 1e-4);

    // A steady rate reads the same all the way along, the edges included.
    std::vector<TimelineBin> steady(40);
    for (TimelineBin& bin : steady) bin.damage = 1000;
    for (const size_t window : {size_t{5}, size_t{15}, size_t{30}}) {
        for (const float v : smoothed_series(steady, 40, TimelineMetric::Damage, window)) {
            TEST_ASSERT_NEAR(v, 1000.0, 1e-3);
        }
    }

    // Seconds past the last bin had nothing in them.
    std::vector<TimelineBin> short_bins(2);
    short_bins[0].healing = 10;
    short_bins[1].healing = 10;
    const std::vector<float> padded = smoothed_series(short_bins, 4, TimelineMetric::Healing, 1);
    TEST_ASSERT_EQ(padded.size(), size_t{4});
    TEST_ASSERT_NEAR(padded[1], 10.0, 1e-6);
    TEST_ASSERT_NEAR(padded[3], 0.0, 1e-6);
}

TEST_CASE(Timeline, DownsampleAveragesEachSpan) {
    const std::vector<float> seven{1, 3, 5, 7, 9, 11, 13};
    const std::vector<float> three = downsample(seven, 3);
    TEST_ASSERT_EQ(three.size(), size_t{3});
    TEST_ASSERT_NEAR(three[0], 2.0, 1e-6);   // 1, 3
    TEST_ASSERT_NEAR(three[1], 6.0, 1e-6);   // 5, 7
    TEST_ASSERT_NEAR(three[2], 11.0, 1e-6);  // 9, 11, 13
    TEST_ASSERT_EQ(downsample(seven, 10).size(), size_t{7});
    const std::vector<float> flat(kTimelineMaxSeconds, 42.0f);
    for (const float v : downsample(flat, 700)) TEST_ASSERT_NEAR(v, 42.0, 1e-4);
}

TEST_CASE(Timeline, BinsStopAtAnHourAndSaturate) {
    std::vector<TimelineBin> bins;
    TimelineBin* first = timeline_bin(bins, at_s(10), at_s(9));  // before the start: second 0
    TEST_ASSERT(first != nullptr);
    TEST_ASSERT_EQ(bins.size(), size_t{1});
    TEST_ASSERT(timeline_bin(bins, at_s(10), at_s(12.9)) == &bins[2]);
    TEST_ASSERT(timeline_bin(bins, at_s(0), at_s(3599.5)) != nullptr);
    TEST_ASSERT(timeline_bin(bins, at_s(0), at_s(3600)) == nullptr);
    TEST_ASSERT_EQ(bins.size(), kTimelineMaxSeconds);

    uint32_t v = std::numeric_limits<uint32_t>::max() - 5;
    add_capped(v, 100);
    TEST_ASSERT_EQ(v, std::numeric_limits<uint32_t>::max());
    uint32_t w = 1;
    add_capped(w, uint64_t{1} << 40);
    TEST_ASSERT_EQ(w, std::numeric_limits<uint32_t>::max());
}

TEST_CASE(Timeline, BossLineCarriesItsLastFigure) {
    BossHpSeries s;
    s.first_s = 2;
    s.permille = {1000, 950};
    TEST_ASSERT_EQ(s.at(1), -1);
    TEST_ASSERT_EQ(s.at(2), 1000);
    TEST_ASSERT_EQ(s.at(3), 950);
    TEST_ASSERT_EQ(s.at(30), 950);
}

TEST_CASE(DeathLog, RecapKeepsTheNewestTenWithinTwelveSeconds) {
    RecapRing ring;
    // 20 samples, a second apart: the ring keeps 16 (4..19).
    for (int i = 0; i < 20; ++i) {
        RecapSample s;
        s.ts = at_s(i);
        s.amount = static_cast<uint64_t>(i);
        ring.push(s);
    }
    TEST_ASSERT_EQ(ring.size(), RecapRing::kCapacity);
    TEST_ASSERT_EQ(ring.at(0).amount, uint64_t{4});

    // A death at 20 s: 8..19 are within 12 s, and the newest ten are 10..19.
    const auto recap = recap_before(ring, at_s(20));
    TEST_ASSERT_EQ(recap.size(), kRecapShown);
    TEST_ASSERT_EQ(recap.front().amount, uint64_t{10});
    TEST_ASSERT_EQ(recap.back().amount, uint64_t{19});
    // Nothing after the death counts.
    TEST_ASSERT_EQ(recap_before(ring, at_s(5.5)).size(), size_t{2});
}

TEST_CASE(DeathLog, KillingBlowIsTheLastDamage) {
    DeathRecord d;
    TEST_ASSERT(d.killing_blow() == nullptr);
    d.recap.push_back(RecapEvent{2.0, RecapKind::Hit, 1, 0, {}, 100, false});
    d.recap.push_back(RecapEvent{1.0, RecapKind::Dot, 2, 0, {}, 50, false});
    d.recap.push_back(RecapEvent{0.5, RecapKind::HealTick, 3, 0, "Zikel", 70, false});
    TEST_ASSERT(d.killing_blow() != nullptr);
    TEST_ASSERT_EQ(d.killing_blow()->amount, uint64_t{50});
    TEST_ASSERT_FALSE(d.up());
    TEST_ASSERT(recap_kind_from_name(recap_kind_name(RecapKind::HealTick)) == RecapKind::HealTick);
}

TEST_CASE(EffectUptime, CoverageCountsOverlapsOnce) {
    const std::vector<TimeSpan> spans{{at_s(1), at_s(5)}, {at_s(3), at_s(8)}, {at_s(20), at_s(40)}};
    // Clamped to [2, 30]: 2..8 and 20..30.
    TEST_ASSERT_NEAR(covered_seconds(spans, at_s(2), at_s(30)), 16.0, 1e-9);
    // Bridged under a second, two runs become one.
    const auto merged = merged_spans({{at_s(1), at_s(2)}, {at_s(2.5), at_s(4)}, {at_s(6), at_s(7)}}, at_s(0),
                                     at_s(10), kMicrosPerSecond);
    TEST_ASSERT_EQ(merged.size(), size_t{2});
    TEST_ASSERT_EQ(merged[0].second, at_s(4));
}
