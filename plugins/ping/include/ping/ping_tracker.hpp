#pragma once

#include "common/micros.hpp"
#include "protocol/events.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace aion2::ping {

/// Where the clock the game stamps its pings with starts: Unreal's
/// FPlatformTime::Seconds() is the performance counter plus 16,777,216 s (2^24, so a
/// time kept in a float shows). Inferred from the two captures, whose pongs read
/// 1.96 h and 7.40 h of uptime that way (AGENTS.md, Protocol, Pong).
inline constexpr uint64_t kClientClockBaseMs = 16'777'216'000;

/// What the latest pong gave.
enum class PingState : uint8_t {
    /// No pong yet.
    Waiting,
    Measured,
    /// Pongs come, with no performance counter to hold them against: a capture
    /// file played back.
    NoCounter,
    /// Pongs come and read as no ping could (PingTracker::kMaxPing): the game's
    /// clock is not the one assumed.
    ClockRejected,
};

/// One second of the graph.
struct PingSecond {
    /// The second's start, capture time.
    Micros ts{0};
    /// The ping as that second closed; negative for none.
    float ping_ms{-1.0f};
    /// How late that second's latest heartbeat came, at worst (PingTracker, lateness).
    float late_ms{0.0f};
};

/// What the ping overlay draws.
struct PingSnapshot {
    PingState state{PingState::Waiting};
    /// The latest pong's round trip, and when it came (capture time); 0 for none.
    float ping_ms{0.0f};
    Micros ping_at{0};
    /// Over the pongs of the last PingTracker::kStatsWindow.
    uint32_t samples{0};
    float min_ms{0.0f};
    float avg_ms{0.0f};
    float max_ms{0.0f};
    /// The heartbeats' jitter; false before two of them.
    bool has_jitter{false};
    float jitter_ms{0.0f};
    /// Capture time of the latest heartbeat followed; 0 for none.
    Micros heartbeat_at{0};
    /// The last PingTracker::kGraphWindow, oldest first. A second with no heartbeat
    /// has no entry.
    std::vector<PingSecond> seconds;
    uint64_t pongs{0};
    uint64_t rejected{0};
    /// What the latest rejected pong read as, in ms: how far off the assumed clock is.
    double last_rejected_ms{0.0};
};

/**
 * @brief The ping and its jitter, from what the server sends anyway.
 *
 * - **Ping** is a pong's round trip: the client's clock it echoes, held against the
 *   same clock (the machine's performance counter) at the pong's arrival. The
 *   capture stamps packets on its own clock, so the caller says how far that sits
 *   from the counter (set_counter_offset). One comes every 10 s.
 * - **Jitter** is RFC 3550's, over the heartbeats: how much the gap between two
 *   arrivals differs from the gap between their server times, smoothed over 16.
 * - **Lateness** is a heartbeat's arrival less its server time, above the least
 *   that was over the last kBaselineWindow: the delay added to the usual one. It
 *   needs no counter, and shows a spike between two pongs.
 *
 * It follows one connection, the newest: entering a dungeon opens another, to
 * another server, and the old one's last frames are ignored.
 */
class PingTracker {
public:
    /// A round trip longer than this, or negative, is no ping.
    static constexpr Micros kMaxPing = 5 * kMicrosPerSecond;
    static constexpr Micros kBaselineWindow = 30 * kMicrosPerSecond;
    static constexpr Micros kGraphWindow = 120 * kMicrosPerSecond;
    static constexpr Micros kStatsWindow = 300 * kMicrosPerSecond;
    /// A ping older than this is not the ping now: two pongs were missed.
    static constexpr Micros kPingStale = 25 * kMicrosPerSecond;
    /// A heartbeat whose server time is further than this from the capture's is not
    /// one (the decoder's own rule).
    static constexpr Micros kMaxClockGap = 86'400 * kMicrosPerSecond;

    /// Capture time less the performance counter's; nothing when there is no counter.
    void set_counter_offset(std::optional<Micros> offset) noexcept { m_offset = offset; }
    void on_pong(const proto::Pong& pong, uint32_t connection, Micros ts);
    void on_heartbeat(uint64_t server_ms, uint32_t connection, Micros ts);
    /// Drops what has left the windows.
    void tick(Micros now);
    void clear();

    [[nodiscard]] PingSnapshot snapshot(Micros now) const;
    /// The jitter as the latest heartbeat left it; nothing before two of them.
    [[nodiscard]] std::optional<double> jitter_ms() const noexcept {
        return m_has_jitter ? std::optional<double>(m_jitter_ms) : std::nullopt;
    }

private:
    struct Sample {
        Micros ts{0};
        Micros rtt{0};
    };

    /// Whether an event of `connection` counts: a newer one takes over, an older
    /// one is done with.
    [[nodiscard]] bool follow(uint32_t connection);
    /// The ping to draw at `at`: the latest, unless it is stale.
    [[nodiscard]] float ping_at(Micros at) const noexcept;

    std::optional<Micros> m_offset;
    uint32_t m_connection{0};
    PingState m_state{PingState::Waiting};
    std::deque<Sample> m_samples;
    uint64_t m_pongs{0};
    uint64_t m_rejected{0};
    double m_last_rejected_ms{0.0};

    // The connection's heartbeats.
    Micros m_prev_ts{0};
    uint64_t m_prev_server_ms{0};
    bool m_has_jitter{false};
    double m_jitter_ms{0.0};
    /// Arrival less server time, in µs: the least of the current half of the
    /// baseline window and of the one before.
    Micros m_half_start{0};
    std::optional<int64_t> m_least;
    std::optional<int64_t> m_least_before;

    /// The second being filled, and those done.
    PingSecond m_open;
    std::deque<PingSecond> m_seconds;
};

} // namespace aion2::ping
