#include "test_framework.hpp"

#include "app/ping_view.hpp"
#include "ping/ping_plugin.hpp"
#include "ping/ping_tracker.hpp"

#include <cmath>
#include <string>

using namespace aion2;
using namespace aion2::ping;

namespace {

/// A capture clock, and a machine an hour up as it starts: the counter read 3,600 s
/// at kStart.
constexpr Micros kStart = 1'792'152'000'000'000ULL;
constexpr Micros kOffset = kStart - 3'600 * kMicrosPerSecond;

/// The pong that arrives at `arrival` (capture time), `rtt_ms` after the game asked.
proto::Pong pong_at(Micros arrival, uint64_t rtt_ms) {
    proto::Pong p;
    p.client_ms = kClientClockBaseMs + (arrival - kOffset) / kMicrosPerMilli - rtt_ms;
    p.server_ms = arrival / kMicrosPerMilli - rtt_ms / 2;
    return p;
}

/// A heartbeat every 50 ms from `from` for `seconds`, each arriving `delay_ms` after
/// its server time.
void beats(PingTracker& t, Micros from, int seconds, int64_t delay_ms, uint32_t connection = 1) {
    for (int i = 0; i < seconds * 20; ++i) {
        const Micros ts = from + static_cast<Micros>(i) * 50 * kMicrosPerMilli;
        t.on_heartbeat(static_cast<uint64_t>(static_cast<int64_t>(ts / kMicrosPerMilli) - delay_ms), connection, ts);
    }
}

} // namespace

// --- Ping: a pong against the counter ---

TEST_CASE(Ping, APongAgainstTheCounterIsThePing) {
    PingTracker t;
    t.set_counter_offset(kOffset);
    TEST_ASSERT(t.snapshot(kStart).state == PingState::Waiting);
    const Micros at = kStart + 5 * kMicrosPerSecond;
    t.on_pong(pong_at(at, 80), 1, at);
    const PingSnapshot s = t.snapshot(at);
    TEST_ASSERT(s.state == PingState::Measured);
    TEST_ASSERT_NEAR(s.ping_ms, 80.0f, 1e-3f);
    TEST_ASSERT_EQ(s.ping_at, at);
    TEST_ASSERT_EQ(s.samples, uint32_t{1});
    TEST_ASSERT_NEAR(s.min_ms, 80.0f, 1e-3f);
    TEST_ASSERT_NEAR(s.max_ms, 80.0f, 1e-3f);
    TEST_ASSERT_EQ(s.pongs, uint64_t{1});
    TEST_ASSERT_EQ(s.rejected, uint64_t{0});
}

TEST_CASE(Ping, TheCapturesPongReadsAsUptime) {
    // The dungeon capture's first pong echoed 16,784,277,100: 7,061.1 s of uptime on
    // Unreal's clock. A counter that read 7,061.142 s as it arrived makes 42 ms.
    PingTracker t;
    const Micros at = 1'792'247'444'447'000ULL;
    t.set_counter_offset(at - 7'061'142 * kMicrosPerMilli);
    t.on_pong(proto::Pong{16'784'277'100, 1'792'247'444'258}, 1, at);
    TEST_ASSERT(t.snapshot(at).state == PingState::Measured);
    TEST_ASSERT_NEAR(t.snapshot(at).ping_ms, 42.0f, 1e-3f);
}

TEST_CASE(Ping, WithNoCounterAPongGivesNoPing) {
    PingTracker t;
    const Micros at = kStart + kMicrosPerSecond;
    t.on_pong(pong_at(at, 80), 1, at);
    const PingSnapshot s = t.snapshot(at);
    TEST_ASSERT(s.state == PingState::NoCounter);
    TEST_ASSERT_EQ(s.pongs, uint64_t{1});
    TEST_ASSERT_EQ(s.samples, uint32_t{0});
    TEST_ASSERT_EQ(s.ping_at, Micros{0});
}

TEST_CASE(Ping, AClockThatIsNotTheCountersIsRejected) {
    PingTracker t;
    t.set_counter_offset(kOffset);
    const Micros at = kStart + 5 * kMicrosPerSecond;

    // Unix milliseconds, not a counter's.
    t.on_pong(proto::Pong{at / kMicrosPerMilli, at / kMicrosPerMilli}, 1, at);
    PingSnapshot s = t.snapshot(at);
    TEST_ASSERT(s.state == PingState::ClockRejected);
    TEST_ASSERT_EQ(s.rejected, uint64_t{1});
    TEST_ASSERT_EQ(s.samples, uint32_t{0});
    TEST_ASSERT(std::abs(s.last_rejected_ms) > 1e9);

    // Under the base, 50 ms ahead of the counter, and six seconds behind it.
    t.on_pong(proto::Pong{1000, at / kMicrosPerMilli}, 1, at);
    proto::Pong ahead = pong_at(at, 0);
    ahead.client_ms += 50;
    t.on_pong(ahead, 1, at);
    TEST_ASSERT_NEAR(t.snapshot(at).last_rejected_ms, -50.0, 1e-6);
    t.on_pong(pong_at(at, 6'000), 1, at);
    TEST_ASSERT_NEAR(t.snapshot(at).last_rejected_ms, 6'000.0, 1e-6);
    // Garbage that no clock is.
    t.on_pong(proto::Pong{~uint64_t{0}, 0}, 1, at);
    s = t.snapshot(at);
    TEST_ASSERT_EQ(s.rejected, uint64_t{5});
    TEST_ASSERT_EQ(s.samples, uint32_t{0});
    TEST_ASSERT(s.state == PingState::ClockRejected);

    // The edges are pings: none at all, and the longest one.
    t.on_pong(pong_at(at, 0), 1, at);
    t.on_pong(pong_at(at, 5'000), 1, at);
    s = t.snapshot(at);
    TEST_ASSERT(s.state == PingState::Measured);
    TEST_ASSERT_EQ(s.samples, uint32_t{2});
    TEST_ASSERT_NEAR(s.ping_ms, 5'000.0f, 1e-3f);
}

TEST_CASE(Ping, TheRangeCoversTheLastFiveMinutes) {
    PingTracker t;
    t.set_counter_offset(kOffset);
    // A pong every 10 s for ten minutes: 50 ms, then 51, 52...
    Micros at = kStart;
    for (uint64_t i = 0; i < 60; ++i) {
        at = kStart + i * 10 * kMicrosPerSecond;
        t.on_pong(pong_at(at, 50 + i), 1, at);
        t.tick(at);
    }
    const PingSnapshot s = t.snapshot(at);
    // The last five minutes hold the pongs 29 to 59: 300 s back, inclusive.
    TEST_ASSERT_EQ(s.samples, uint32_t{31});
    TEST_ASSERT_NEAR(s.min_ms, 79.0f, 1e-3f);
    TEST_ASSERT_NEAR(s.max_ms, 109.0f, 1e-3f);
    TEST_ASSERT_NEAR(s.avg_ms, 94.0f, 1e-3f);
    TEST_ASSERT_NEAR(s.ping_ms, 109.0f, 1e-3f);
    // A snapshot long after counts none of them, ticked or not.
    TEST_ASSERT_EQ(t.snapshot(at + 400 * kMicrosPerSecond).samples, uint32_t{0});
}

// --- Jitter and lateness: the heartbeats ---

TEST_CASE(Ping, EvenHeartbeatsHaveNoJitter) {
    PingTracker t;
    TEST_ASSERT_FALSE(t.snapshot(kStart).has_jitter);
    t.on_heartbeat(kStart / kMicrosPerMilli - 200, 1, kStart);
    TEST_ASSERT_FALSE(t.snapshot(kStart).has_jitter);  // one arrival has no gap
    beats(t, kStart + 50 * kMicrosPerMilli, 2, 200);
    const PingSnapshot s = t.snapshot(kStart + 2 * kMicrosPerSecond);
    TEST_ASSERT(s.has_jitter);
    TEST_ASSERT_NEAR(s.jitter_ms, 0.0f, 1e-6f);
}

TEST_CASE(Ping, JitterIsRfc3550s) {
    PingTracker t;
    beats(t, kStart, 1, 200);
    // The next one 16 ms late: the gap was 66 ms against the server's 50.
    const Micros next = kStart + kMicrosPerSecond;
    t.on_heartbeat(next / kMicrosPerMilli - 200, 1, next + 16 * kMicrosPerMilli);
    TEST_ASSERT_NEAR(*t.jitter_ms(), 1.0, 1e-9);
    // And the one after on time again: 34 ms against 50, as far off the other way.
    const Micros after = next + 50 * kMicrosPerMilli;
    t.on_heartbeat(after / kMicrosPerMilli - 200, 1, after);
    TEST_ASSERT_NEAR(*t.jitter_ms(), 1.0 + 15.0 / 16.0, 1e-9);
}

TEST_CASE(Ping, LatenessIsTheDelayAboveTheWindowsLeast) {
    PingTracker t;
    beats(t, kStart, 3, 200);
    // One heartbeat 60 ms later than the rest, in the fourth second.
    const Micros spike = kStart + 3 * kMicrosPerSecond + 100 * kMicrosPerMilli;
    t.on_heartbeat(spike / kMicrosPerMilli - 260, 1, spike);
    PingSnapshot s = t.snapshot(spike);
    TEST_ASSERT_EQ(s.seconds.size(), size_t{4});
    TEST_ASSERT_NEAR(s.seconds[0].late_ms, 0.0f, 1e-3f);
    TEST_ASSERT_NEAR(s.seconds[3].late_ms, 60.0f, 1e-3f);
    TEST_ASSERT_EQ(s.seconds[3].ts, kStart + 3 * kMicrosPerSecond);
    TEST_ASSERT_EQ(s.heartbeat_at, spike);

    // The two clocks are not set alike: a server clock ahead of the capture's works
    // the same.
    PingTracker ahead;
    beats(ahead, kStart, 1, -5'000);
    ahead.on_heartbeat(kStart / kMicrosPerMilli + 1'000 + 5'000 - 40, 1, kStart + kMicrosPerSecond);
    TEST_ASSERT_NEAR(ahead.snapshot(kStart + kMicrosPerSecond).seconds.back().late_ms, 40.0f, 1e-3f);
}

TEST_CASE(Ping, ALastingDelayBecomesTheUsualOne) {
    PingTracker t;
    beats(t, kStart, 5, 200);
    // The route gets 30 ms longer and stays so.
    const Micros change = kStart + 5 * kMicrosPerSecond;
    beats(t, change, 1, 230);
    TEST_ASSERT_NEAR(t.snapshot(change + kMicrosPerSecond).seconds.back().late_ms, 30.0f, 1e-3f);
    // Once the window holds nothing shorter, it is the usual delay.
    beats(t, change + kMicrosPerSecond, 40, 230);
    const Micros end = change + 41 * kMicrosPerSecond;
    TEST_ASSERT_NEAR(t.snapshot(end).seconds.back().late_ms, 0.0f, 1e-3f);
}

TEST_CASE(Ping, AHeartbeatFarFromTheCapturesClockIsNotOne) {
    PingTracker t;
    t.on_heartbeat(12345, 1, kStart);
    t.on_heartbeat(~uint64_t{0}, 1, kStart + 50 * kMicrosPerMilli);
    const PingSnapshot s = t.snapshot(kStart + kMicrosPerSecond);
    TEST_ASSERT_EQ(s.heartbeat_at, Micros{0});
    TEST_ASSERT(s.seconds.empty());
}

TEST_CASE(Ping, ANewConnectionStartsItsHeartbeatsAfresh) {
    PingTracker t;
    t.set_counter_offset(kOffset);
    beats(t, kStart, 2, 200, 3);
    t.on_pong(pong_at(kStart + kMicrosPerSecond, 70), 3, kStart + kMicrosPerSecond);
    TEST_ASSERT(t.jitter_ms().has_value());

    // The dungeon's server: another clock, 300 ms further from the capture's.
    const Micros entered = kStart + 2 * kMicrosPerSecond;
    t.on_heartbeat(entered / kMicrosPerMilli - 500, 5, entered);
    TEST_ASSERT_FALSE(t.jitter_ms().has_value());
    PingSnapshot s = t.snapshot(entered);
    TEST_ASSERT_NEAR(s.seconds.back().late_ms, 0.0f, 1e-3f);
    // The ping stays until the new server answers.
    TEST_ASSERT(s.state == PingState::Measured);
    TEST_ASSERT_NEAR(s.ping_ms, 70.0f, 1e-3f);

    // The town's connection saying its last is not followed.
    const Micros late = entered + 20 * kMicrosPerMilli;
    t.on_heartbeat(late / kMicrosPerMilli - 200, 3, late);
    t.on_pong(pong_at(late, 999), 3, late);
    s = t.snapshot(late);
    TEST_ASSERT_EQ(s.heartbeat_at, entered);
    TEST_ASSERT_EQ(s.pongs, uint64_t{1});
    TEST_ASSERT_NEAR(s.ping_ms, 70.0f, 1e-3f);
}

TEST_CASE(Ping, TheGraphKeepsTwoMinutesASecondEach) {
    PingTracker t;
    t.set_counter_offset(kOffset);
    // Three minutes of heartbeats, a pong every 10 s for the first two.
    for (int second = 0; second < 180; ++second) {
        const Micros at = kStart + static_cast<Micros>(second) * kMicrosPerSecond;
        if (second % 10 == 5 && second < 120) t.on_pong(pong_at(at, 60), 1, at);
        beats(t, at, 1, 200);
        t.tick(at);
    }
    const Micros end = kStart + 180 * kMicrosPerSecond - 50 * kMicrosPerMilli;
    t.tick(end);
    const PingSnapshot s = t.snapshot(end);
    // The seconds that began in the last 120 s: 60 to 179, the graph's 120 columns.
    TEST_ASSERT_EQ(s.seconds.size(), size_t{120});
    TEST_ASSERT_EQ(s.seconds.front().ts, kStart + 60 * kMicrosPerSecond);
    TEST_ASSERT_EQ(s.seconds.back().ts, kStart + 179 * kMicrosPerSecond);
    // The last pong came at 115 s: its ping holds for 25 s, then there is none.
    TEST_ASSERT_NEAR(s.seconds[0].ping_ms, 60.0f, 1e-3f);
    TEST_ASSERT_NEAR(s.seconds[139 - 60].ping_ms, 60.0f, 1e-3f);  // closes at 140 s: 25 s on
    TEST_ASSERT(s.seconds[140 - 60].ping_ms < 0.0f);
    TEST_ASSERT(s.seconds.back().ping_ms < 0.0f);
}

TEST_CASE(Ping, ClearForgetsAllButTheCounter) {
    PingTracker t;
    t.set_counter_offset(kOffset);
    beats(t, kStart, 2, 200, 4);
    t.on_pong(pong_at(kStart + kMicrosPerSecond, 70), 4, kStart + kMicrosPerSecond);
    t.clear();
    PingSnapshot s = t.snapshot(kStart + 2 * kMicrosPerSecond);
    TEST_ASSERT(s.state == PingState::Waiting);
    TEST_ASSERT(s.seconds.empty());
    TEST_ASSERT_EQ(s.pongs, uint64_t{0});
    TEST_ASSERT_FALSE(s.has_jitter);
    // An older connection is followed again, and the counter is still known.
    const Micros at = kStart + 3 * kMicrosPerSecond;
    t.on_pong(pong_at(at, 55), 2, at);
    s = t.snapshot(at);
    TEST_ASSERT(s.state == PingState::Measured);
    TEST_ASSERT_NEAR(s.ping_ms, 55.0f, 1e-3f);
}

TEST_CASE(Ping, ThePluginReadsPongsAndHeartbeatsOnly) {
    PingPlugin plugin;
    plugin.set_counter_offset(kOffset);
    const Micros at = kStart + kMicrosPerSecond;
    plugin.on_event(proto::ParsedEvent{at, 0x0036, proto::Heartbeat{at / kMicrosPerMilli - 200}, 1});
    plugin.on_event(proto::ParsedEvent{at, 0x0336, pong_at(at, 90), 1});
    plugin.on_event(proto::ParsedEvent{at, 0x4236, proto::Death{7}, 1});
    plugin.tick(at);
    const PingSnapshot s = plugin.snapshot(at);
    TEST_ASSERT(s.state == PingState::Measured);
    TEST_ASSERT_NEAR(s.ping_ms, 90.0f, 1e-3f);
    TEST_ASSERT_EQ(s.heartbeat_at, at);
    plugin.clear();
    TEST_ASSERT(plugin.snapshot(at).state == PingState::Waiting);
}

// --- The overlay's figures (app/ping_view.hpp) ---

TEST_CASE(PingView, LevelsFollowTheTwoThresholds) {
    app::PingSettings s;
    s.warn_ms = 100;
    s.bad_ms = 180;
    TEST_ASSERT(app::ping_level(99.9f, s) == app::PingLevel::Good);
    TEST_ASSERT(app::ping_level(100.0f, s) == app::PingLevel::Warn);
    TEST_ASSERT(app::ping_level(179.9f, s) == app::PingLevel::Warn);
    TEST_ASSERT(app::ping_level(180.0f, s) == app::PingLevel::Bad);
}

TEST_CASE(PingView, APingShowsWhileItIsFresh) {
    PingSnapshot p;
    const Micros now = kStart + 60 * kMicrosPerSecond;
    TEST_ASSERT_FALSE(app::shown_ping(p, now).has_value());
    TEST_ASSERT(std::string(app::ping_status(p, now)).find("Waiting") == 0);

    p.state = PingState::Measured;
    p.ping_ms = 87.0f;
    p.ping_at = now - 12 * kMicrosPerSecond;
    TEST_ASSERT_NEAR(*app::shown_ping(p, now), 87.0f, 1e-6f);
    TEST_ASSERT_EQ(std::string(app::ping_status(p, now)), std::string());
    // Two pongs missed: it is no longer the ping now.
    p.ping_at = now - PingTracker::kPingStale - 1;
    TEST_ASSERT_FALSE(app::shown_ping(p, now).has_value());
    TEST_ASSERT_FALSE(std::string(app::ping_status(p, now)).empty());

    // An earlier ping is not shown once the pongs stop making sense.
    p.ping_at = now - kMicrosPerSecond;
    p.state = PingState::ClockRejected;
    TEST_ASSERT_FALSE(app::shown_ping(p, now).has_value());
    TEST_ASSERT_FALSE(std::string(app::ping_status(p, now)).empty());
    p.state = PingState::NoCounter;
    TEST_ASSERT_FALSE(app::shown_ping(p, now).has_value());
    TEST_ASSERT_FALSE(std::string(app::ping_status(p, now)).empty());
}

TEST_CASE(PingView, FormatsMilliseconds) {
    TEST_ASSERT_EQ(app::format_ms(87.4f), std::string("87 ms"));
    TEST_ASSERT_EQ(app::format_ms(3.64f), std::string("3.6 ms"));
    TEST_ASSERT_EQ(app::format_ms(9.96f), std::string("10 ms"));
    TEST_ASSERT_EQ(app::format_ms(0.0f), std::string("0.0 ms"));
    TEST_ASSERT_EQ(app::format_ms(1234.5f), std::string("1234 ms"));
}

TEST_CASE(PingView, TheGraphsTopIsARoundFigure) {
    TEST_ASSERT_NEAR(app::ping_graph_top(0.0f), 50.0f, 1e-6f);
    TEST_ASSERT_NEAR(app::ping_graph_top(50.0f), 50.0f, 1e-6f);
    TEST_ASSERT_NEAR(app::ping_graph_top(50.1f), 100.0f, 1e-6f);
    TEST_ASSERT_NEAR(app::ping_graph_top(143.0f), 150.0f, 1e-6f);
    TEST_ASSERT_NEAR(app::ping_graph_top(4'200.0f), 5'000.0f, 1e-6f);
}

TEST_CASE(PingView, EachSecondHasItsColumn) {
    PingSnapshot p;
    const Micros now = kStart + 500 * kMicrosPerSecond + 300 * kMicrosPerMilli;
    const Micros now_second = kStart + 500 * kMicrosPerSecond;
    p.seconds.push_back(PingSecond{now_second - 200 * kMicrosPerSecond, 70.0f, 900.0f});  // out of the graph
    p.seconds.push_back(PingSecond{now_second - 119 * kMicrosPerSecond, 70.0f, 2.0f});
    p.seconds.push_back(PingSecond{now_second - 5 * kMicrosPerSecond, -1.0f, 130.0f});
    p.seconds.push_back(PingSecond{now_second, 64.0f, 0.0f});
    const app::PingGraph g = app::ping_graph(p, now);
    TEST_ASSERT_EQ(g.columns.size(), app::kPingGraphColumns);
    TEST_ASSERT(g.columns[0] == (app::PingGraphColumn{70.0f, 2.0f}));
    TEST_ASSERT(g.columns[1] == app::PingGraphColumn{});  // a second with no heartbeat
    TEST_ASSERT(g.columns[114] == (app::PingGraphColumn{-1.0f, 130.0f}));
    TEST_ASSERT(g.columns[119] == (app::PingGraphColumn{64.0f, 0.0f}));
    // The scale fits what is drawn, not what has left the graph.
    TEST_ASSERT_NEAR(g.top_ms, 150.0f, 1e-6f);

    // Nothing to draw still has a scale.
    const app::PingGraph empty = app::ping_graph(PingSnapshot{}, now);
    TEST_ASSERT_EQ(empty.columns.size(), app::kPingGraphColumns);
    TEST_ASSERT_NEAR(empty.top_ms, 50.0f, 1e-6f);
}

TEST_CASE(PingView, TheJitterSitsBesideThePingWhileBothFit) {
    // The ping alone is its own width, however narrow the window.
    TEST_ASSERT(app::ping_row_layout(100.0f, 0.0f, 14.0f, 400.0f) == (app::PingRowLayout{1, 100.0f}));
    TEST_ASSERT(app::ping_row_layout(100.0f, 0.0f, 14.0f, 60.0f) == (app::PingRowLayout{1, 100.0f}));
    // Both on one line, the gap between them, up to exactly the room there is.
    TEST_ASSERT(app::ping_row_layout(100.0f, 80.0f, 14.0f, 400.0f) == (app::PingRowLayout{1, 194.0f}));
    TEST_ASSERT(app::ping_row_layout(100.0f, 80.0f, 14.0f, 194.0f) == (app::PingRowLayout{1, 194.0f}));
    // A pixel short, and the jitter goes under: the line is as wide as the wider.
    TEST_ASSERT(app::ping_row_layout(100.0f, 80.0f, 14.0f, 193.0f) == (app::PingRowLayout{2, 100.0f}));
    TEST_ASSERT(app::ping_row_layout(60.0f, 80.0f, 14.0f, 100.0f) == (app::PingRowLayout{2, 80.0f}));
}
