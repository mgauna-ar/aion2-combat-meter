#pragma once

#include "capture/packet_headers.hpp"
#include "common/micros.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace aion2::capture {

/// Where a TcpReassembler delivers one direction's byte stream.
class StreamSink {
public:
    virtual ~StreamSink() = default;
    /// A new stream. `at_boundary` is true when it began with a SYN, so its first
    /// byte is the first the server sent; false when the capture joined a live
    /// connection mid-stream.
    virtual void on_stream_start(bool at_boundary, Micros ts) = 0;
    /// The next bytes, in order and without duplicates.
    virtual void on_stream_data(std::span<const uint8_t> bytes, Micros ts) = 0;
    /// `lost_bytes` never arrived and the stream continues after them.
    virtual void on_stream_gap(uint64_t lost_bytes, Micros ts) = 0;
    /// FIN or RST. A later SYN on the same flow starts a new stream.
    virtual void on_stream_closed(Micros ts) = 0;
};

/**
 * @brief Puts one direction of a TCP connection back in order.
 *
 * Sequence numbers are widened to 64-bit offsets from the stream's start, so
 * wrap-around is invisible past this class. Retransmits and overlaps are trimmed.
 * A hole is waited on until it has been open kHoleTimeout of capture time or the
 * bytes queued behind it pass kMaxBuffered; then it is reported as a gap and the
 * stream goes on. A capture that drops a packet never sees it retransmitted, so
 * waiting longer would only delay everything behind it.
 *
 * A segment more than kMaxAhead past the next byte is not queued: one stray (a
 * corrupt packet, a reused port) would otherwise become a gap that leaves every
 * real segment behind it, read as old. Only when nothing but such segments has
 * arrived for kHoleTimeout has the stream really moved on (the capture lost that
 * much), and it skips there.
 */
class TcpReassembler {
public:
    static constexpr Micros kHoleTimeout = 300 * kMicrosPerMilli;
    static constexpr size_t kMaxBuffered = 1u << 20;
    static constexpr uint64_t kMaxAhead = 4u << 20;

    void push(const TcpSegment& segment, StreamSink& sink);

    /// Reports a hole that has waited too long. push() calls it too; the owner
    /// calls it on a quiet stream so a hole at the very end is not held forever.
    void expire(Micros now, StreamSink& sink);

    [[nodiscard]] bool started() const noexcept { return m_started; }
    [[nodiscard]] uint64_t delivered_bytes() const noexcept { return m_next; }
    [[nodiscard]] uint64_t gap_bytes() const noexcept { return m_gap_bytes; }
    [[nodiscard]] size_t buffered_bytes() const noexcept { return m_buffered; }

private:
    void start(uint32_t isn, bool at_boundary, Micros ts, StreamSink& sink);
    void reset() noexcept;
    /// The offset of `seq`, taken as the one nearest m_next. Negative for bytes
    /// from before the stream's start.
    [[nodiscard]] int64_t offset_of(uint32_t seq) const noexcept;
    void deliver(uint64_t start, std::span<const uint8_t> bytes, Micros ts, StreamSink& sink);
    void drain(Micros ts, StreamSink& sink);
    /// Gives up on the bytes before `offset`: a gap, and delivery resumes there.
    void skip_to(uint64_t offset, Micros ts, StreamSink& sink);
    /// A segment within kMaxAhead: delivered, or queued behind a hole.
    void push_near(int64_t first, std::span<const uint8_t> payload, Micros ts, StreamSink& sink);
    /// A segment beyond kMaxAhead: ignored, unless only such have come for a while.
    void push_far(uint64_t first, std::span<const uint8_t> payload, Micros ts, StreamSink& sink);
    void check_fin(Micros ts, StreamSink& sink);

    bool m_started{false};
    /// The sequence number at offset 0.
    uint32_t m_base_seq{0};
    /// Offset of the next byte to deliver.
    uint64_t m_next{0};
    struct Pending {
        std::vector<uint8_t> bytes;
        Micros arrived{0};
    };
    /// Segments ahead of m_next, by start offset.
    std::map<uint64_t, Pending> m_pending;
    size_t m_buffered{0};
    /// When the hole in front of m_pending opened; 0 with none.
    Micros m_hole_since{0};
    /// Since when only segments beyond kMaxAhead have arrived; 0 when not.
    Micros m_far_since{0};
    uint64_t m_gap_bytes{0};
    /// The FIN was seen, at m_fin_at: the stream closes once delivery reaches it.
    bool m_fin_seen{false};
    uint64_t m_fin_at{0};
};

} // namespace aion2::capture
