#include "ping/ping_tracker.hpp"

#include <algorithm>
#include <cmath>

namespace aion2::ping {

namespace {

/// Ten years of uptime: beyond it the echoed clock is not a counter's.
constexpr uint64_t kMaxUptimeMs = 10ull * 365 * 86'400 * 1000;

constexpr float to_ms(Micros us) noexcept { return static_cast<float>(static_cast<double>(us) / 1000.0); }

} // namespace

bool PingTracker::follow(uint32_t connection) {
    if (connection < m_connection) return false;
    if (connection > m_connection) {
        // Another server: its clock, its route, its heartbeats.
        m_connection = connection;
        m_prev_ts = 0;
        m_prev_server_ms = 0;
        m_has_jitter = false;
        m_jitter_ms = 0.0;
        m_half_start = 0;
        m_least.reset();
        m_least_before.reset();
    }
    return true;
}

void PingTracker::on_pong(const proto::Pong& pong, uint32_t connection, Micros ts) {
    if (!follow(connection)) return;
    ++m_pongs;
    if (!m_offset) {
        m_state = PingState::NoCounter;
        return;
    }
    // In doubles first: a clock that is not the one assumed can read as anything.
    const double counter_ms = (static_cast<double>(ts) - static_cast<double>(*m_offset)) / 1000.0;
    const double sent_ms = static_cast<double>(pong.client_ms) - static_cast<double>(kClientClockBaseMs);
    const double read_ms = counter_ms - sent_ms;
    const bool counter_ok = ts >= *m_offset;
    const bool clock_ok = pong.client_ms >= kClientClockBaseMs && pong.client_ms - kClientClockBaseMs <= kMaxUptimeMs;
    Micros rtt = 0;
    bool ok = counter_ok && clock_ok;
    if (ok) {
        const Micros counter = ts - *m_offset;
        const Micros sent = (pong.client_ms - kClientClockBaseMs) * kMicrosPerMilli;
        ok = counter >= sent && counter - sent <= kMaxPing;
        if (ok) rtt = counter - sent;
    }
    if (!ok) {
        ++m_rejected;
        m_last_rejected_ms = read_ms;
        m_state = PingState::ClockRejected;
        return;
    }
    m_samples.push_back(Sample{ts, rtt});
    m_state = PingState::Measured;
}

void PingTracker::on_heartbeat(uint64_t server_ms, uint32_t connection, Micros ts) {
    if (!follow(connection)) return;
    const uint64_t capture_ms = ts / kMicrosPerMilli;
    const uint64_t gap_ms = capture_ms > server_ms ? capture_ms - server_ms : server_ms - capture_ms;
    if (gap_ms > kMaxClockGap / kMicrosPerMilli) return;

    // Jitter: the arrivals' gap against the server's own.
    if (m_prev_ts != 0 && ts >= m_prev_ts) {
        const auto arrived = static_cast<int64_t>(ts - m_prev_ts);
        const int64_t sent =
            (static_cast<int64_t>(server_ms) - static_cast<int64_t>(m_prev_server_ms)) * static_cast<int64_t>(kMicrosPerMilli);
        const double off_ms = std::abs(static_cast<double>(arrived - sent)) / 1000.0;
        m_jitter_ms += (off_ms - m_jitter_ms) / 16.0;
        m_has_jitter = true;
    }
    m_prev_ts = ts;
    m_prev_server_ms = server_ms;

    // Lateness: this delay above the least of the window. The two clocks are not
    // set alike, so the delay itself means nothing, and may be negative.
    const int64_t delay = static_cast<int64_t>(ts) - static_cast<int64_t>(server_ms * kMicrosPerMilli);
    const Micros half = kBaselineWindow / 2;
    if (!m_least || ts < m_half_start) {
        m_half_start = ts;
        m_least = delay;
        m_least_before.reset();
    } else if (ts - m_half_start >= half) {
        if (ts - m_half_start >= kBaselineWindow) m_least_before.reset();
        else m_least_before = m_least;
        m_half_start = ts;
        m_least = delay;
    } else {
        m_least = std::min(*m_least, delay);
    }
    const int64_t least = m_least_before ? std::min(*m_least, *m_least_before) : *m_least;
    const float late_ms = static_cast<float>(static_cast<double>(delay - least) / 1000.0);

    const Micros second = ts - ts % kMicrosPerSecond;
    if (m_open.ts != second) {
        if (m_open.ts != 0) {
            m_open.ping_ms = ping_at(m_open.ts + kMicrosPerSecond);
            m_seconds.push_back(m_open);
        }
        m_open = PingSecond{second, -1.0f, 0.0f};
    }
    m_open.late_ms = std::max(m_open.late_ms, late_ms);
}

float PingTracker::ping_at(Micros at) const noexcept {
    if (m_samples.empty()) return -1.0f;
    const Sample& last = m_samples.back();
    if (at > last.ts && at - last.ts > kPingStale) return -1.0f;
    return to_ms(last.rtt);
}

void PingTracker::tick(Micros now) {
    while (!m_samples.empty() && now > m_samples.front().ts && now - m_samples.front().ts > kStatsWindow) {
        m_samples.pop_front();
    }
    while (!m_seconds.empty() && now > m_seconds.front().ts && now - m_seconds.front().ts > kGraphWindow) {
        m_seconds.pop_front();
    }
}

void PingTracker::clear() {
    const std::optional<Micros> offset = m_offset;
    *this = PingTracker();
    m_offset = offset;
}

PingSnapshot PingTracker::snapshot(Micros now) const {
    PingSnapshot s;
    s.state = m_state;
    s.pongs = m_pongs;
    s.rejected = m_rejected;
    s.last_rejected_ms = m_last_rejected_ms;
    s.has_jitter = m_has_jitter;
    s.jitter_ms = static_cast<float>(m_jitter_ms);
    s.heartbeat_at = m_prev_ts;

    double sum = 0.0;
    for (const Sample& sample : m_samples) {
        if (now > sample.ts && now - sample.ts > kStatsWindow) continue;
        const float ms = to_ms(sample.rtt);
        s.min_ms = s.samples == 0 ? ms : std::min(s.min_ms, ms);
        s.max_ms = s.samples == 0 ? ms : std::max(s.max_ms, ms);
        sum += ms;
        ++s.samples;
    }
    if (s.samples > 0) s.avg_ms = static_cast<float>(sum / s.samples);
    if (!m_samples.empty()) {
        s.ping_ms = to_ms(m_samples.back().rtt);
        s.ping_at = m_samples.back().ts;
    }

    const auto in_window = [&](const PingSecond& second) { return now <= second.ts || now - second.ts <= kGraphWindow; };
    s.seconds.reserve(m_seconds.size() + 1);
    for (const PingSecond& second : m_seconds) {
        if (in_window(second)) s.seconds.push_back(second);
    }
    if (m_open.ts != 0 && in_window(m_open)) {
        PingSecond open = m_open;
        open.ping_ms = ping_at(now);
        s.seconds.push_back(open);
    }
    return s;
}

} // namespace aion2::ping
