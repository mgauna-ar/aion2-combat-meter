#pragma once

#include "common/micros.hpp"

#include <mutex>
#include <optional>

namespace aion2::capture {

/**
 * @brief How far the capture's clock sits from the machine's performance counter.
 *
 * The driver stamps each packet; a capture thread reads it a moment later and reads
 * the counter then. `stamp - counter` is therefore the true offset less however long
 * the packet waited, and never more than it: the largest one seen lately is the
 * offset, to within the quickest delivery (well under a millisecond, with a
 * heartbeat arriving every 50 ms).
 *
 * Measured per packet, not from the wall clock: Npcap stamps from the counter, set
 * against the wall clock once, and the two drift apart by tens of milliseconds an
 * hour.
 *
 * Capture threads sample; the engine thread reads.
 */
class ClockOffsetEstimator {
public:
    /// How long a sample counts, on the counter's time. Two halves are kept, so the
    /// offset rests on the last 15 to 30 s.
    static constexpr Micros kWindow = 30 * kMicrosPerSecond;

    /// A packet stamped `packet_ts`, read as the counter said `counter`.
    void sample(Micros packet_ts, Micros counter) noexcept;
    /// Capture time less counter time; nothing before the first sample.
    [[nodiscard]] std::optional<Micros> offset() const noexcept;

private:
    mutable std::mutex m_mutex;
    /// Counter time the current half began.
    Micros m_half_start{0};
    /// The largest offset of the current half and of the one before; 0 for none.
    Micros m_current{0};
    Micros m_previous{0};
};

} // namespace aion2::capture
