#include "capture/tcp_reassembler.hpp"

namespace aion2::capture {

void TcpReassembler::reset() noexcept {
    m_started = false;
    m_base_seq = 0;
    m_next = 0;
    m_pending.clear();
    m_buffered = 0;
    m_hole_since = 0;
    m_far_since = 0;
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
        m_buffered -= node.mapped().bytes.size();
        deliver(node.key(), node.mapped().bytes, ts, sink);
    }
    // What is still queued sits behind a hole that has been open since its first
    // segment came, not since now: holes behind one another expire together.
    m_hole_since = m_pending.empty() ? 0 : m_pending.begin()->second.arrived;
}

void TcpReassembler::skip_to(uint64_t offset, Micros ts, StreamSink& sink) {
    const uint64_t lost = offset - m_next;
    m_gap_bytes += lost;
    m_next = offset;
    sink.on_stream_gap(lost, ts);
    drain(ts, sink);
}

void TcpReassembler::push_near(int64_t first, std::span<const uint8_t> payload, Micros ts, StreamSink& sink) {
    const int64_t end = first + static_cast<int64_t>(payload.size());
    if (end <= static_cast<int64_t>(m_next)) return;  // a retransmit
    if (first <= static_cast<int64_t>(m_next)) {
        // Bytes from before the stream's start (first < 0) are cut by deliver's skip.
        deliver(first < 0 ? 0 : static_cast<uint64_t>(first),
                first < 0 ? payload.subspan(static_cast<size_t>(-first)) : payload, ts, sink);
        drain(ts, sink);
        return;
    }
    auto [slot, queued] = m_pending.try_emplace(static_cast<uint64_t>(first));
    if (queued) slot->second.arrived = ts;
    std::vector<uint8_t>& held = slot->second.bytes;
    if (payload.size() > held.size()) {
        m_buffered += payload.size() - held.size();
        held.assign(payload.begin(), payload.end());
    }
    if (m_hole_since == 0) m_hole_since = ts;
}

void TcpReassembler::push_far(uint64_t first, std::span<const uint8_t> payload, Micros ts, StreamSink& sink) {
    if (m_far_since == 0) m_far_since = ts;
    if (ts < m_far_since + kHoleTimeout) return;
    // Nothing nearer for a while: the capture lost all of that, and the stream
    // goes on from here. Everything queued was before it.
    m_far_since = 0;
    skip_to(first, ts, sink);
    deliver(first, payload, ts, sink);
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
        const bool same_stream = m_started && m_base_seq == isn;
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
        if (first > static_cast<int64_t>(m_next + kMaxAhead)) {
            push_far(static_cast<uint64_t>(first), segment.payload, ts, sink);
        } else {
            m_far_since = 0;
            push_near(first, segment.payload, ts, sink);
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
        skip_to(m_pending.begin()->first, now, sink);
    }
    check_fin(now, sink);
}

} // namespace aion2::capture
