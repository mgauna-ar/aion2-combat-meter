#include "ping/ping_tracker.hpp"

#include <algorithm>
#include <cmath>

namespace aion2::ping {

namespace {

/// Ten years of uptime: beyond it the echoed clock is not a counter's.
constexpr uint64_t kMaxUptimeMs = 10ull * 365 * 86'400 * 1000;

constexpr float to_ms(Micros us) noexcept { return static_cast<float>(static_cast<double>(us) / 1000.0); }
constexpr float to_ms(int64_t us) noexcept { return static_cast<float>(static_cast<double>(us) / 1000.0); }

} // namespace

bool PingTracker::follow(uint32_t connection) {
    if (connection < m_connection) return false;
    if (connection > m_connection) {
        // Another server: its clock, its route, its heartbeats. Its silence before
        // its first heartbeat is no stall of the old one's.
        m_connection = connection;
        m_ended = false;
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
        m_samples.push_back(Sample{ts, 0, false});
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
        m_samples.push_back(Sample{ts, 0, false});
        return;
    }
    m_samples.push_back(Sample{ts, rtt, true});
    m_state = PingState::Measured;
}

void PingTracker::on_heartbeat(uint64_t server_ms, uint32_t connection, Micros ts) {
    if (!follow(connection)) return;
    const uint64_t capture_ms = ts / kMicrosPerMilli;
    const uint64_t gap_ms = capture_ms > server_ms ? capture_ms - server_ms : server_ms - capture_ms;
    if (gap_ms > kMaxClockGap / kMicrosPerMilli) return;

    // The seconds before this one's are done: closed as they stood at their ends,
    // a stall that passed through them included, before this heartbeat ends it.
    const Micros second = ts - ts % kMicrosPerSecond;
    if (m_open.ts != 0 && m_open.ts < second) {
        std::vector<PingSecond> done;
        seconds_until(second, second, done);
        m_seconds.insert(m_seconds.end(), done.begin(), done.end());
        m_open = PingSecond{};
    } else if (m_open.ts > second) {
        // Time went back: the open second is done as it is.
        m_open.ping_ms = ping_at(m_open.ts + kMicrosPerSecond - 1);
        m_seconds.push_back(m_open);
        m_open = PingSecond{};
    }

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
    // set alike, so the delay itself means nothing, and may be negative. The
    // window's halves run on the server's time: heartbeats held up by a stall were
    // sent in the half they were sent in, however late they come.
    const int64_t delay = static_cast<int64_t>(ts) - static_cast<int64_t>(server_ms * kMicrosPerMilli);
    const Micros sent_at = server_ms * kMicrosPerMilli;
    const Micros half = kBaselineWindow / 2;
    if (!m_least || sent_at < m_half_start) {
        m_half_start = sent_at;
        m_least = delay;
        m_least_before.reset();
    } else if (sent_at - m_half_start >= half) {
        // After the server sent nothing for longer than the window, the half before
        // is too old to keep.
        if (sent_at - m_half_start >= kBaselineWindow) m_least_before.reset();
        else m_least_before = m_least;
        m_half_start = sent_at;
        m_least = delay;
    } else {
        m_least = std::min(*m_least, delay);
    }
    const float late_ms = to_ms(delay - *least());

    if (m_open.ts != second) m_open = PingSecond{second, -1.0f, 0.0f};
    m_open.late_ms = std::max(m_open.late_ms, late_ms);
}

void PingTracker::on_connection_closed(uint32_t connection, Micros) {
    if (connection == m_connection) m_ended = true;
}

void PingTracker::on_connection_seen(uint32_t connection) noexcept {
    // A zone's new connection opens 0.10 to 0.18 s after the old one's last
    // heartbeat, and its own come 2.4 to 2.7 s later: that wait is a load, not a
    // stall (AGENTS.md, Transport).
    if (m_connection != 0 && connection > m_connection) m_ended = true;
}

std::optional<int64_t> PingTracker::least() const noexcept {
    if (!m_least) return std::nullopt;
    return m_least_before ? std::min(*m_least, *m_least_before) : *m_least;
}

std::optional<int64_t> PingTracker::stall_late(Micros at) const noexcept {
    const std::optional<int64_t> usual = least();
    if (m_prev_ts == 0 || m_ended || !usual || at <= m_prev_ts || at - m_prev_ts > kHeartbeatsGone) {
        return std::nullopt;
    }
    // The next heartbeat's server time, and its delay were it to come at `at`: it
    // comes later still, so it is at least that late.
    const auto due = static_cast<int64_t>((m_prev_server_ms + kHeartbeatEveryMs) * kMicrosPerMilli);
    const int64_t late = static_cast<int64_t>(at) - due - *usual;
    if (late < static_cast<int64_t>(kStallAfter)) return std::nullopt;
    return late;
}

void PingTracker::seconds_until(Micros until, Micros at, std::vector<PingSecond>& out) const {
    if (m_open.ts == 0) return;
    // A stall shows no further than kHeartbeatsGone after the latest heartbeat.
    const Micros stall_end = m_prev_ts + kHeartbeatsGone + kMicrosPerSecond;
    for (Micros start = m_open.ts; start < until; start += kMicrosPerSecond) {
        if (start != m_open.ts && start > stall_end) break;
        const Micros end = std::min(start + kMicrosPerSecond, std::max(at, start));
        const std::optional<int64_t> late = stall_late(end);
        PingSecond second = start == m_open.ts ? m_open : PingSecond{start, -1.0f, 0.0f};
        // A second with no heartbeat has an entry only where a stall passed through.
        if (start != m_open.ts && !late) continue;
        if (late) second.late_ms = std::max(second.late_ms, to_ms(*late));
        // A pong right on the second's end is the next second's.
        second.ping_ms = ping_at(std::min(end, start + kMicrosPerSecond - 1));
        out.push_back(second);
    }
}

float PingTracker::ping_at(Micros at) const noexcept {
    // The latest pong by `at`: a later one is not the ping that second had.
    for (auto it = m_samples.rbegin(); it != m_samples.rend(); ++it) {
        if (it->ts > at) continue;
        if (!it->measured) return -1.0f;
        if (at - it->ts > kPingStale) return -1.0f;
        return to_ms(it->rtt);
    }
    return -1.0f;
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
    s.heartbeat_at = m_prev_ts;

    // The jitter while the heartbeats come; in a stall, at least what the next one
    // will make it: its arrival gap is already this much over the server's.
    const bool beating = m_prev_ts != 0 && !m_ended && (now <= m_prev_ts || now - m_prev_ts <= kHeartbeatsGone);
    s.has_jitter = m_has_jitter && beating;
    if (s.has_jitter) {
        double jitter = m_jitter_ms;
        if (stall_late(now)) {
            const double off_ms = static_cast<double>(now - m_prev_ts) / 1000.0 - static_cast<double>(kHeartbeatEveryMs);
            if (off_ms > jitter) jitter += (off_ms - jitter) / 16.0;
        }
        s.jitter_ms = static_cast<float>(jitter);
    }

    double sum = 0.0;
    for (const Sample& sample : m_samples) {
        if (!sample.measured) continue;
        if (now > sample.ts && now - sample.ts > kStatsWindow) continue;
        const float ms = to_ms(sample.rtt);
        s.min_ms = s.samples == 0 ? ms : std::min(s.min_ms, ms);
        s.max_ms = s.samples == 0 ? ms : std::max(s.max_ms, ms);
        sum += ms;
        ++s.samples;
    }
    if (s.samples > 0) s.avg_ms = static_cast<float>(sum / s.samples);
    for (auto it = m_samples.rbegin(); it != m_samples.rend(); ++it) {
        if (!it->measured) continue;
        s.ping_ms = to_ms(it->rtt);
        s.ping_at = it->ts;
        break;
    }

    const auto in_window = [&](const PingSecond& second) { return now <= second.ts || now - second.ts <= kGraphWindow; };
    s.seconds.reserve(m_seconds.size() + 2);
    for (const PingSecond& second : m_seconds) {
        if (in_window(second)) s.seconds.push_back(second);
    }
    // The open second as it stands now, and the seconds a stall has passed through
    // since, up to the one `now` is in.
    std::vector<PingSecond> open;
    const Micros now_second = now - now % kMicrosPerSecond;
    seconds_until(std::max(now_second, m_open.ts) + kMicrosPerSecond, now, open);
    for (const PingSecond& second : open) {
        if (in_window(second)) s.seconds.push_back(second);
    }
    return s;
}

} // namespace aion2::ping
