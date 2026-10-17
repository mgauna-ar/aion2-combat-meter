#include "test_framework.hpp"
#include "wire_builder.hpp"

#include "capture/capture_file.hpp"
#include "capture/pcapng_writer.hpp"
#include "pipeline/stream_demux.hpp"
#include "protocol/opcodes.hpp"

#include <algorithm>
#include <vector>

using namespace aion2;
using namespace aion2::pipeline;
using namespace aion2::test::wire;

namespace {

constexpr uint64_t kServerMs = 1'792'152'000'000ULL;
constexpr Micros kTs = kServerMs * kMicrosPerMilli;

struct OpcodeSink final : proto::FrameSink {
    std::vector<uint16_t> opcodes;
    std::vector<uint32_t> connections;
    void on_frame(const proto::Frame& f) override {
        opcodes.push_back(f.opcode);
        connections.push_back(f.connection);
    }
};

/// Sends `stream` as segments of `chunk` bytes from `spec`, starting at spec.seq.
void send(StreamDemux& demux, OpcodeSink& sink, TcpSpec spec, const Bytes& stream, size_t chunk, Micros ts,
          uint32_t link = capture::link_type::Ethernet) {
    for (size_t at = 0; at < stream.size(); at += chunk) {
        const size_t n = std::min(chunk, stream.size() - at);
        const std::span<const uint8_t> part(stream.data() + at, n);
        const Bytes frame = link == capture::link_type::Null ? loopback_tcp(spec, part) : ethernet_tcp(spec, part);
        demux.on_packet(link, frame, ts, sink);
        spec.seq += static_cast<uint32_t>(n);
    }
}

} // namespace

TEST_CASE(StreamDemux, ServerPortFlowIsGameFromSyn) {
    StreamDemux demux;
    OpcodeSink sink;
    TcpSpec syn;
    syn.seq = 999;
    syn.flags = capture::tcp_flag::Syn | capture::tcp_flag::Ack;
    demux.on_packet(capture::link_type::Ethernet, ethernet_tcp(syn, Bytes{}), kTs, sink);

    // Own character before the first heartbeat: kept, since the flow began at a SYN.
    const Bytes stream = concat({opcode_frame(proto::opcode::OwnCharacter, {1, 2, 3}), heartbeat(kServerMs),
                                 opcode_frame(proto::opcode::Damage, {4})});
    TcpSpec data;
    data.seq = 1000;
    send(demux, sink, data, stream, 5, kTs);

    TEST_ASSERT((sink.opcodes == std::vector<uint16_t>{proto::opcode::OwnCharacter, proto::opcode::Heartbeat,
                                                        proto::opcode::Damage}));
    const auto flows = demux.flows();
    TEST_ASSERT_EQ(flows.size(), size_t{1});
    TEST_ASSERT(flows[0].role == FlowRole::Game);
}

TEST_CASE(StreamDemux, RelayFlowIsHeldUntilHeartbeatsConfirmIt) {
    StreamDemux demux;
    OpcodeSink sink;
    TcpSpec relay;
    relay.src_ip = ip4(127, 0, 0, 1);
    relay.dst_ip = ip4(127, 0, 0, 1);
    relay.src_port = 49001;
    relay.dst_port = 52000;
    relay.seq = 5000;

    const Bytes first = concat({heartbeat(kServerMs), opcode_frame(proto::opcode::Damage, {1})});
    send(demux, sink, relay, first, 64, kTs, capture::link_type::Null);
    TEST_ASSERT(sink.opcodes.empty());  // one heartbeat: still a candidate

    relay.seq += static_cast<uint32_t>(first.size());
    const Bytes more = concat({heartbeat(kServerMs + 50), heartbeat(kServerMs + 100)});
    send(demux, sink, relay, more, 64, kTs, capture::link_type::Null);
    TEST_ASSERT((sink.opcodes == std::vector<uint16_t>{proto::opcode::Heartbeat, proto::opcode::Damage,
                                                        proto::opcode::Heartbeat, proto::opcode::Heartbeat}));
    TEST_ASSERT(demux.flows()[0].role == FlowRole::Game);
    // The held frames say which connection they came on, like the rest.
    TEST_ASSERT(sink.connections[0] != 0);
    TEST_ASSERT(std::all_of(sink.connections.begin(), sink.connections.end(),
                            [&](uint32_t c) { return c == sink.connections[0]; }));
}

TEST_CASE(StreamDemux, EachConnectionHasItsOwnNumber) {
    // Entering a dungeon opens a connection to its server: the engine tells a new
    // run from a reload within one by it.
    StreamDemux demux;
    OpcodeSink sink;
    TcpSpec town;
    send(demux, sink, town, concat({heartbeat(kServerMs), opcode_frame(proto::opcode::MapLoad, {1})}), 64, kTs);
    TcpSpec dungeon;
    dungeon.src_ip = ip4(10, 0, 0, 9);
    dungeon.dst_port = static_cast<uint16_t>(kClientPort + 1);
    send(demux, sink, dungeon, concat({heartbeat(kServerMs + 50), opcode_frame(proto::opcode::MapLoad, {2})}), 64,
         kTs);
    TEST_ASSERT_EQ(sink.connections.size(), size_t{4});
    TEST_ASSERT(sink.connections[0] != 0);
    TEST_ASSERT_EQ(sink.connections[1], sink.connections[0]);
    TEST_ASSERT(sink.connections[2] != 0 && sink.connections[2] != sink.connections[0]);
    TEST_ASSERT_EQ(sink.connections[3], sink.connections[2]);
}

TEST_CASE(StreamDemux, NoisyFlowIsRejected) {
    StreamDemux demux;
    OpcodeSink sink;
    TcpSpec web;
    web.src_port = 443;
    web.dst_port = 50123;
    const Bytes noise(StreamDemux::kRejectAfterBytes + 10, 0x17);
    send(demux, sink, web, noise, 1400, kTs);
    TEST_ASSERT(sink.opcodes.empty());
    TEST_ASSERT(demux.flows()[0].role == FlowRole::Rejected);
}

TEST_CASE(StreamDemux, ClientDirectionIsIgnored) {
    StreamDemux demux;
    OpcodeSink sink;
    TcpSpec up;
    up.src_ip = kClientIp;
    up.dst_ip = kServerIp;
    up.src_port = kClientPort;
    up.dst_port = kServerPort;
    send(demux, sink, up, heartbeat(kServerMs), 64, kTs);
    TEST_ASSERT(sink.opcodes.empty());
    TEST_ASSERT(demux.flows()[0].role == FlowRole::Rejected);
}

TEST_CASE(StreamDemux, PortOnlyModeSkipsOtherFlows) {
    DemuxOptions opts;
    opts.any_port = false;
    StreamDemux demux(opts);
    OpcodeSink sink;
    TcpSpec relay;
    relay.src_port = 49001;
    send(demux, sink, relay, concat({heartbeat(kServerMs), heartbeat(kServerMs), heartbeat(kServerMs)}), 64, kTs);
    TEST_ASSERT(sink.opcodes.empty());
    TEST_ASSERT(demux.flows().empty());
}

TEST_CASE(StreamDemux, ThroughAPcapngFile) {
    // A whole session written as a capture file and read back, out-of-order
    // segment included: what aion2meter-replay does.
    capture::PcapngBuilder file;
    const Bytes stream = concat({heartbeat(kServerMs), opcode_frame(proto::opcode::Spawn, Bytes(30, 1)),
                                 bundle_frame(concat({opcode_frame(proto::opcode::Damage, {9}),
                                                      opcode_frame(proto::opcode::Tick, {8})})),
                                 heartbeat(kServerMs + 50)});
    const std::span<const uint8_t> all(stream);
    const size_t third = stream.size() / 3;
    TcpSpec a;
    a.seq = 10;
    TcpSpec b = a;
    b.seq = a.seq + static_cast<uint32_t>(third);
    TcpSpec c = a;
    c.seq = a.seq + static_cast<uint32_t>(2 * third);
    // The last third overtakes the middle one.
    file.add_packet(capture::link_type::Ethernet, kTs, ethernet_tcp(a, all.first(third)));
    file.add_packet(capture::link_type::Ethernet, kTs + 1000, ethernet_tcp(c, all.subspan(2 * third)));
    file.add_packet(capture::link_type::Ethernet, kTs + 2000, ethernet_tcp(b, all.subspan(third, third)));

    std::string error;
    auto reader = capture::CaptureFileReader::from_bytes(file.bytes(), error);
    TEST_ASSERT(reader.has_value());
    StreamDemux demux;
    OpcodeSink sink;
    capture::CapturedPacket pkt;
    while (reader->next(pkt)) demux.on_packet(pkt.link_type, pkt.data, pkt.ts, sink);

    // Joined mid-stream (no SYN), so decoding starts at the first heartbeat, which
    // here is the stream's first byte.
    TEST_ASSERT((sink.opcodes == std::vector<uint16_t>{proto::opcode::Heartbeat, proto::opcode::Spawn,
                                                        proto::opcode::Damage, proto::opcode::Tick,
                                                        proto::opcode::Heartbeat}));
    TEST_ASSERT_EQ(demux.game_gap_bytes(), uint64_t{0});
    TEST_ASSERT_EQ(demux.game_resyncs(), uint64_t{0});
}
