#include "pipeline/stream_demux.hpp"

#include <deque>

namespace aion2::pipeline {

/// One direction of one connection: reassembler and decoder, and the frames held
/// while it is a candidate.
class StreamDemux::Flow final : public capture::StreamSink, public proto::FrameSink {
public:
    Flow(capture::FlowKey key, FlowRole role) : m_role(role) { m_summary.key = key; }

    void push(const capture::TcpSegment& segment, proto::FrameSink& out) {
        ++m_summary.segments;
        if (m_summary.first_ts == 0) m_summary.first_ts = segment.ts;
        m_summary.last_ts = segment.ts;
        if (m_role == FlowRole::Rejected) {
            // Freed here rather than in settle(), which runs inside the reassembler.
            if (m_reassembler.started()) m_reassembler = capture::TcpReassembler();
            return;
        }
        m_out = &out;
        m_reassembler.push(segment, *this);
        m_out = nullptr;
    }

    void expire(Micros now, proto::FrameSink& out) {
        if (m_role == FlowRole::Rejected) return;
        m_out = &out;
        m_reassembler.expire(now, *this);
        m_out = nullptr;
    }

    [[nodiscard]] FlowRole role() const noexcept { return m_role; }

    [[nodiscard]] FlowSummary summary() const {
        FlowSummary s = m_summary;
        s.role = m_role;
        s.gap_bytes = m_reassembler.gap_bytes();
        s.decoder = m_decoder.stats();
        return s;
    }

    // capture::StreamSink
    void on_stream_start(bool at_boundary, Micros ts) override { m_decoder.start(at_boundary, ts); }

    void on_stream_data(std::span<const uint8_t> bytes, Micros ts) override {
        m_summary.bytes += bytes.size();
        m_decoder.feed(bytes, ts, *this);
        if (m_role == FlowRole::Candidate) settle();
    }

    void on_stream_gap(uint64_t, Micros) override { m_decoder.on_gap(); }

    void on_stream_closed(Micros) override {}

    // proto::FrameSink
    void on_frame(const proto::Frame& frame) override {
        if (m_role == FlowRole::Game) {
            if (m_out != nullptr) m_out->on_frame(frame);
            return;
        }
        if (m_role != FlowRole::Candidate) return;
        Held held;
        held.raw.assign(frame.raw.begin(), frame.raw.end());
        held.body.assign(frame.body.begin(), frame.body.end());
        held.opcode = frame.opcode;
        held.flag = frame.flag;
        held.ts = frame.ts;
        held.depth = frame.depth;
        held.offset = frame.stream_offset;
        m_held_bytes += held.raw.size() + held.body.size();
        m_held.push_back(std::move(held));
        while (m_held_bytes > kMaxHeldBytes && !m_held.empty()) {
            m_held_bytes -= m_held.front().raw.size() + m_held.front().body.size();
            m_held.pop_front();
        }
    }

private:
    struct Held {
        std::vector<uint8_t> raw;
        std::vector<uint8_t> body;
        uint16_t opcode{0};
        uint8_t flag{0};
        Micros ts{0};
        uint8_t depth{0};
        uint64_t offset{0};
    };

    /// A candidate becomes the game's on enough heartbeats, or is given up on.
    void settle() {
        if (m_decoder.stats().heartbeats >= kConfirmHeartbeats) {
            m_role = FlowRole::Game;
            if (m_out != nullptr) {
                for (const Held& h : m_held) {
                    proto::Frame f;
                    f.opcode = h.opcode;
                    f.flag = h.flag;
                    f.body = h.body;
                    f.raw = h.raw;
                    f.ts = h.ts;
                    f.depth = h.depth;
                    f.stream_offset = h.offset;
                    m_out->on_frame(f);
                }
            }
            m_held.clear();
            m_held_bytes = 0;
        } else if (m_summary.bytes >= kRejectAfterBytes && m_decoder.stats().heartbeats == 0) {
            m_role = FlowRole::Rejected;
            m_held.clear();
            m_held_bytes = 0;
            m_decoder = proto::FrameDecoder();
        }
    }

    FlowRole m_role;
    FlowSummary m_summary;
    capture::TcpReassembler m_reassembler;
    proto::FrameDecoder m_decoder;
    std::deque<Held> m_held;
    size_t m_held_bytes{0};
    proto::FrameSink* m_out{nullptr};
};

StreamDemux::StreamDemux(DemuxOptions options) : m_options(options) {}

StreamDemux::~StreamDemux() = default;

void StreamDemux::on_packet(uint32_t link, std::span<const uint8_t> data, Micros ts, proto::FrameSink& sink) {
    ++m_packets;
    const auto segment = capture::parse_tcp_segment(link, data, ts);
    if (segment) on_segment(*segment, sink);
}

void StreamDemux::on_segment(const capture::TcpSegment& segment, proto::FrameSink& sink) {
    ++m_segments;
    auto it = m_flows.find(segment.flow);
    if (it == m_flows.end()) {
        const bool server_side = segment.flow.src_port == m_options.server_port;
        if (!server_side && !m_options.any_port) return;
        // The client's direction of the game connection is encrypted and never
        // heartbeats, so it is rejected without decoding.
        const bool client_side = segment.flow.dst_port == m_options.server_port;
        const FlowRole role = server_side ? FlowRole::Game : client_side ? FlowRole::Rejected : FlowRole::Candidate;
        it = m_flows.emplace(segment.flow, std::make_unique<Flow>(segment.flow, role)).first;
    }
    Flow& flow = *it->second;
    flow.push(segment, sink);
    if (flow.role() == FlowRole::Game) m_last_game_ts = segment.ts;
}

void StreamDemux::tick(Micros now, proto::FrameSink& sink) {
    for (auto& [key, flow] : m_flows) flow->expire(now, sink);
}

std::vector<FlowSummary> StreamDemux::flows() const {
    std::vector<FlowSummary> out;
    out.reserve(m_flows.size());
    for (const auto& [key, flow] : m_flows) out.push_back(flow->summary());
    return out;
}

uint64_t StreamDemux::game_gap_bytes() const {
    uint64_t total = 0;
    for (const auto& [key, flow] : m_flows) {
        if (flow->role() == FlowRole::Game) total += flow->summary().gap_bytes;
    }
    return total;
}

uint64_t StreamDemux::game_resyncs() const {
    uint64_t total = 0;
    for (const auto& [key, flow] : m_flows) {
        if (flow->role() == FlowRole::Game) total += flow->summary().decoder.resyncs;
    }
    return total;
}

uint64_t StreamDemux::game_bundle_errors() const {
    uint64_t total = 0;
    for (const auto& [key, flow] : m_flows) {
        if (flow->role() == FlowRole::Game) total += flow->summary().decoder.bundle_errors;
    }
    return total;
}

} // namespace aion2::pipeline
