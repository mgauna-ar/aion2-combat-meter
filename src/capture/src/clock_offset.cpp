#include "capture/clock_offset.hpp"

#include <algorithm>

namespace aion2::capture {

void ClockOffsetEstimator::sample(Micros packet_ts, Micros counter) noexcept {
    // A capture clock is Unix time and the counter runs from boot.
    if (packet_ts <= counter) return;
    const Micros value = packet_ts - counter;
    const Micros half = kWindow / 2;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_current == 0 || counter < m_half_start) {
        m_half_start = counter;
        m_current = value;
        m_previous = 0;
    } else if (counter - m_half_start >= half) {
        // After a silence longer than the window the half before is too old to keep.
        m_previous = counter - m_half_start >= kWindow ? 0 : m_current;
        m_half_start = counter;
        m_current = value;
    } else {
        m_current = std::max(m_current, value);
    }
}

std::optional<Micros> ClockOffsetEstimator::offset() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_current == 0) return std::nullopt;
    return std::max(m_current, m_previous);
}

} // namespace aion2::capture
