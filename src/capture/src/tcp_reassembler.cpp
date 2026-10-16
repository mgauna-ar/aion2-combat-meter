#include "capture/tcp_reassembler.hpp"

namespace aion2::capture {

void TcpReassembler::reset() noexcept {
    m_started = false;
    m_base_seq = 0;
    m_next = 0;
    m_pending.clear();
    m_buffered = 0;
    m_hole_since = 0;
    m_fin_seen = false;
    m_fin_at = 0;
}

void TcpReassembler::start(uint32_t isn, bool at_boundary, Micros ts, StreamSink& sink) {
    reset();
    m_started = true;
    m_base_seq = isn;
    sink.on_stream_start(at_boundary, ts);
}

int64_t TcpReassembler::offset_of(uint32_t seq) const noexcept {
    const auto expected = static_cast<uint32_t>(m_base_seq + static_cast<uint32_t>(m_next));
    const auto diff = static_cast<int32_t>(seq - expected);
    return static_cast<int64_t>(m_next) + diff;
}

void TcpReassembler::deliver(uint64_t start, std::span<const uint8_t> bytes, Micros ts, StreamSink& sink) {
    const uint64_t skip = m_next - start;
    if (skip >= bytes.size()) return;
    const auto fresh = bytes.subspan(static_cast<size_t>(skip));
    m_next += fresh.size();
    sink.on_stream_data(fresh, ts);
}

void TcpReassembler::drain(Micros ts, StreamSink& sink) {
    while (!m_pending.empty() && m_pending.begin()->first <= m_next) {
        auto node = m_pending.extract(m_pending.begin());
        m_buffered -= node.mapped().size();
        deliver(node.key(), node.mapped(), ts, sink);
    }
    // What is still queued sits behind a new hole, timed from now.
    m_hole_since = m_pending.empty() ? 0 : ts;
}

void TcpReassembler::check_fin(Micros ts, StreamSink& sink) {
    if (m_fin_seen && m_pending.empty() && m_next >= m_fin_at) {
        sink.on_stream_closed(ts);
        reset();
    }
}

void TcpReassembler::push(const TcpSegment& segment, StreamSink& sink) {
    const Micros ts = segment.ts;

    if (segment.has(tcp_flag::Rst)) {
        if (m_started) sink.on_stream_closed(ts);
        reset();
        return;
    }

    uint32_t data_seq = segment.seq;
    if (segment.has(tcp_flag::Syn)) {
        const uint32_t isn = segment.seq + 1;
        // A retransmitted SYN for the stream we already have changes nothing.
        const bool same_stream = m_started && m_base_seq == isn && m_next == 0;
        if (!same_stream) {
            if (m_started) sink.on_stream_closed(ts);
            start(isn, true, ts, sink);
        }
        data_seq = isn;
    } else if (!m_started) {
        start(segment.seq, false, ts, sink);
    }

    if (!segment.payload.empty()) {
        const int64_t first = offset_of(data_seq);
        const int64_t end = first + static_cast<int64_t>(segment.payload.size());
        if (end > static_cast<int64_t>(m_next)) {
            if (first <= static_cast<int64_t>(m_next)) {
                // Bytes from before the stream's start (first < 0) are cut by deliver's skip.
                deliver(first < 0 ? 0 : static_cast<uint64_t>(first),
                        first < 0 ? segment.payload.subspan(static_cast<size_t>(-first)) : segment.payload, ts,
                        sink);
                drain(ts, sink);
            } else {
                auto& slot = m_pending[static_cast<uint64_t>(first)];
                if (segment.payload.size() > slot.size()) {
                    m_buffered += segment.payload.size() - slot.size();
                    slot.assign(segment.payload.begin(), segment.payload.end());
                }
                if (m_hole_since == 0) m_hole_since = ts;
            }
        }
    }

    if (segment.has(tcp_flag::Fin) && m_started) {
        const int64_t fin = offset_of(data_seq) + static_cast<int64_t>(segment.payload.size());
        m_fin_seen = true;
        m_fin_at = fin < 0 ? 0 : static_cast<uint64_t>(fin);
    }

    expire(ts, sink);
    check_fin(ts, sink);
}

void TcpReassembler::expire(Micros now, StreamSink& sink) {
    while (!m_pending.empty()) {
        const bool timed_out = m_hole_since != 0 && now > m_hole_since && now - m_hole_since >= kHoleTimeout;
        if (!timed_out && m_buffered <= kMaxBuffered) break;
        const uint64_t resume = m_pending.begin()->first;
        const uint64_t lost = resume - m_next;
        m_gap_bytes += lost;
        m_next = resume;
        sink.on_stream_gap(lost, now);
        drain(now, sink);
    }
    check_fin(now, sink);
}

} // namespace aion2::capture
