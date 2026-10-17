#include "test_framework.hpp"
#include "wire_builder.hpp"

#include "capture/capture_file.hpp"
#include "capture/packet_headers.hpp"
#include "capture/pcapng_writer.hpp"
#include "capture/tcp_reassembler.hpp"

#include <string>
#include <vector>

using namespace aion2;
using namespace aion2::capture;
using namespace aion2::test::wire;

namespace {

const Bytes kPayload = bytes_of("hello daeva");

} // namespace

TEST_CASE(PacketHeaders, EthernetIpv4Tcp) {
    TcpSpec spec;
    spec.seq = 0xDEADBEEF;
    const Bytes frame = ethernet_tcp(spec, kPayload);
    const auto seg = parse_tcp_segment(link_type::Ethernet, frame, 42);
    TEST_ASSERT(seg.has_value());
    TEST_ASSERT_EQ(seg->flow.src.to_string(), std::string("104.18.0.10"));
    TEST_ASSERT_EQ(seg->flow.dst.to_string(), std::string("192.168.1.50"));
    TEST_ASSERT_EQ(seg->flow.src_port, kServerPort);
    TEST_ASSERT_EQ(seg->flow.dst_port, kClientPort);
    TEST_ASSERT_EQ(seg->seq, 0xDEADBEEFu);
    TEST_ASSERT(seg->has(tcp_flag::Psh));
    TEST_ASSERT_EQ(seg->ts, Micros{42});
    TEST_ASSERT_EQ(std::string(seg->payload.begin(), seg->payload.end()), std::string("hello daeva"));
    TEST_ASSERT_EQ(seg->flow.to_string(), std::string("104.18.0.10:13328 -> 192.168.1.50:51000"));
}

TEST_CASE(PacketHeaders, NpcapLoopbackFrame) {
    TcpSpec spec;
    spec.src_ip = ip4(127, 0, 0, 1);
    spec.dst_ip = ip4(127, 0, 0, 1);
    const Bytes frame = loopback_tcp(spec, kPayload);
    const auto seg = parse_tcp_segment(link_type::Null, frame, 0);
    TEST_ASSERT(seg.has_value());
    TEST_ASSERT(seg->flow.src.is_loopback());
    TEST_ASSERT_EQ(seg->payload.size(), kPayload.size());
}

TEST_CASE(PacketHeaders, VlanTaggedFrame) {
    const Bytes ip = ipv4_tcp(TcpSpec{}, kPayload);
    Bytes frame = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0x81, 0x00, 0x00, 0x2A, 0x08, 0x00};
    put_bytes(frame, ip);
    const auto seg = parse_tcp_segment(link_type::Ethernet, frame, 0);
    TEST_ASSERT(seg.has_value());
    TEST_ASSERT_EQ(seg->payload.size(), kPayload.size());
}

TEST_CASE(PacketHeaders, EthernetPaddingIsTrimmed) {
    const Bytes one = {0xAB};
    Bytes frame = ethernet_tcp(TcpSpec{}, one);
    frame.resize(frame.size() + 5, 0x00);  // a short frame padded to the Ethernet minimum
    const auto seg = parse_tcp_segment(link_type::Ethernet, frame, 0);
    TEST_ASSERT(seg.has_value());
    TEST_ASSERT_EQ(seg->payload.size(), size_t{1});
}

TEST_CASE(PacketHeaders, Ipv4FragmentIsSkipped) {
    Bytes ip = ipv4_tcp(TcpSpec{}, kPayload);
    ip[6] = 0x20;  // more fragments
    TEST_ASSERT_FALSE(parse_tcp_segment(link_type::Raw, ip, 0).has_value());
}

namespace {

/// An IPv6 packet from 2020:...:2001 to ::1 carrying a TCP segment.
Bytes ipv6_tcp(uint16_t payload_length) {
    Bytes ip = {0x60, 0, 0, 0};
    put_be16(ip, payload_length);
    put_bytes(ip, {6, 64});
    for (int i = 0; i < 15; ++i) ip.push_back(0x20);
    ip.push_back(0x01);  // src 2020:...:2001
    for (int i = 0; i < 15; ++i) ip.push_back(0);
    ip.push_back(0x01);  // dst ::1
    const Bytes v4 = ipv4_tcp(TcpSpec{}, kPayload);
    const Bytes tcp_only(v4.begin() + 20, v4.end());
    put_bytes(ip, tcp_only);
    return ip;
}

} // namespace

TEST_CASE(PacketHeaders, Ipv6Tcp) {
    const Bytes ip = ipv6_tcp(static_cast<uint16_t>(20 + kPayload.size()));
    const auto seg = parse_tcp_segment(link_type::Raw, ip, 0);
    TEST_ASSERT(seg.has_value());
    TEST_ASSERT_FALSE(seg->flow.src.is_v4());
    TEST_ASSERT(seg->flow.dst.is_loopback());
    TEST_ASSERT_EQ(seg->flow.src_port, kServerPort);
    TEST_ASSERT_EQ(seg->payload.size(), kPayload.size());
}

TEST_CASE(PacketHeaders, Ipv6ZeroPayloadLengthKeepsTheCapture) {
    const auto seg = parse_tcp_segment(link_type::Raw, ipv6_tcp(0), 0);
    TEST_ASSERT(seg.has_value());
    TEST_ASSERT_EQ(seg->payload.size(), kPayload.size());
}

TEST_CASE(PacketHeaders, OffloadZeroLengthKeepsTheCapture) {
    Bytes ip = ipv4_tcp(TcpSpec{}, kPayload);
    ip[2] = 0;  // total length 0, as LSO/RSC leaves it
    ip[3] = 0;
    const auto seg = parse_tcp_segment(link_type::Raw, ip, 0);
    TEST_ASSERT(seg.has_value());
    TEST_ASSERT_EQ(seg->payload.size(), kPayload.size());
}

TEST_CASE(PacketHeaders, NonTcpIsSkipped) {
    Bytes ip = ipv4_tcp(TcpSpec{}, kPayload);
    ip[9] = 17;  // UDP
    TEST_ASSERT_FALSE(parse_tcp_segment(link_type::Raw, ip, 0).has_value());
    TEST_ASSERT_FALSE(parse_tcp_segment(9999, ip, 0).has_value());
}

TEST_CASE(CaptureFile, PcapngRoundTripAcrossInterfaces) {
    PcapngBuilder builder;
    const Bytes a = ethernet_tcp(TcpSpec{}, kPayload);
    const Bytes b = loopback_tcp(TcpSpec{}, kPayload);
    builder.add_packet(link_type::Ethernet, 1'700'000'000'123'456ULL, a);
    builder.add_packet(link_type::Null, 1'700'000'000'223'456ULL, b);
    builder.add_packet(link_type::Ethernet, 1'700'000'001'000'000ULL, a);

    std::string error;
    auto reader = CaptureFileReader::from_bytes(builder.bytes(), error);
    TEST_ASSERT(reader.has_value());
    TEST_ASSERT(reader->format() == CaptureFileReader::Format::Pcapng);

    CapturedPacket pkt;
    TEST_ASSERT(reader->next(pkt));
    TEST_ASSERT_EQ(pkt.ts, Micros{1'700'000'000'123'456ULL});
    TEST_ASSERT_EQ(pkt.link_type, link_type::Ethernet);
    TEST_ASSERT_EQ(pkt.data.size(), a.size());
    TEST_ASSERT(reader->next(pkt));
    TEST_ASSERT_EQ(pkt.link_type, link_type::Null);
    TEST_ASSERT_EQ(pkt.interface_id, 1u);
    TEST_ASSERT(reader->next(pkt));
    TEST_ASSERT_EQ(pkt.ts, Micros{1'700'000'001'000'000ULL});
    TEST_ASSERT_FALSE(reader->next(pkt));
    TEST_ASSERT(reader->error().empty());
}

TEST_CASE(CaptureFile, PcapngTruncatedTailKeepsWholePackets) {
    PcapngBuilder builder;
    const Bytes a = ethernet_tcp(TcpSpec{}, kPayload);
    builder.add_packet(link_type::Ethernet, 1, a);
    builder.add_packet(link_type::Ethernet, 2, a);
    Bytes bytes = builder.bytes();
    bytes.resize(bytes.size() - 10);

    std::string error;
    auto reader = CaptureFileReader::from_bytes(bytes, error);
    TEST_ASSERT(reader.has_value());
    CapturedPacket pkt;
    TEST_ASSERT(reader->next(pkt));
    TEST_ASSERT_FALSE(reader->next(pkt));
    TEST_ASSERT_FALSE(reader->error().empty());
}

TEST_CASE(CaptureFile, PcapngNanosecondResolution) {
    // SHB, then an IDB with if_tsresol = 9 (nanoseconds), then one EPB.
    Bytes f;
    put_le32(f, 0x0A0D0D0A);
    put_le32(f, 28);
    put_le32(f, 0x1A2B3C4D);
    put_le16(f, 1);
    put_le16(f, 0);
    put_le32(f, 0xFFFFFFFF);
    put_le32(f, 0xFFFFFFFF);
    put_le32(f, 28);

    put_le32(f, 1);
    put_le32(f, 32);
    put_le16(f, 1);
    put_le16(f, 0);
    put_le32(f, 65535);
    put_le16(f, 9);
    put_le16(f, 1);
    put_bytes(f, {9, 0, 0, 0});
    put_le16(f, 0);
    put_le16(f, 0);
    put_le32(f, 32);

    const Bytes data = {1, 2, 3, 4};
    const uint64_t ns = 1'700'000'000'123'456'789ULL;
    put_le32(f, 6);
    put_le32(f, 32 + 4);
    put_le32(f, 0);
    put_le32(f, static_cast<uint32_t>(ns >> 32));
    put_le32(f, static_cast<uint32_t>(ns));
    put_le32(f, 4);
    put_le32(f, 4);
    put_bytes(f, data);
    put_le32(f, 32 + 4);

    std::string error;
    auto reader = CaptureFileReader::from_bytes(f, error);
    TEST_ASSERT(reader.has_value());
    CapturedPacket pkt;
    TEST_ASSERT(reader->next(pkt));
    TEST_ASSERT_EQ(pkt.ts, Micros{1'700'000'000'123'456ULL});
    TEST_ASSERT_EQ(pkt.data.size(), size_t{4});
}

TEST_CASE(CaptureFile, ClassicPcapBothByteOrders) {
    const Bytes data = {9, 8, 7};
    // Little-endian, microseconds.
    Bytes le;
    put_le32(le, 0xA1B2C3D4);
    put_le16(le, 2);
    put_le16(le, 4);
    put_le32(le, 0);
    put_le32(le, 0);
    put_le32(le, 65535);
    put_le32(le, link_type::Raw);
    put_le32(le, 100);
    put_le32(le, 250);
    put_le32(le, 3);
    put_le32(le, 3);
    put_bytes(le, data);

    std::string error;
    auto r1 = CaptureFileReader::from_bytes(le, error);
    TEST_ASSERT(r1.has_value());
    CapturedPacket pkt;
    TEST_ASSERT(r1->next(pkt));
    TEST_ASSERT_EQ(pkt.ts, Micros{100'000'250});
    TEST_ASSERT_EQ(pkt.link_type, link_type::Raw);
    TEST_ASSERT_FALSE(r1->next(pkt));
    TEST_ASSERT(r1->error().empty());

    // Big-endian, nanoseconds.
    Bytes be;
    put_be32(be, 0xA1B23C4D);
    put_be16(be, 2);
    put_be16(be, 4);
    put_be32(be, 0);
    put_be32(be, 0);
    put_be32(be, 65535);
    put_be32(be, link_type::Ethernet);
    put_be32(be, 5);
    put_be32(be, 7'000);
    put_be32(be, 3);
    put_be32(be, 3);
    put_bytes(be, data);

    auto r2 = CaptureFileReader::from_bytes(be, error);
    TEST_ASSERT(r2.has_value());
    TEST_ASSERT(r2->next(pkt));
    TEST_ASSERT_EQ(pkt.ts, Micros{5'000'007});
    TEST_ASSERT_EQ(pkt.link_type, link_type::Ethernet);
}

TEST_CASE(CaptureFile, RejectsOtherFiles) {
    std::string error;
    TEST_ASSERT_FALSE(CaptureFileReader::from_bytes(bytes_of("PK\x03\x04 zip"), error).has_value());
    TEST_ASSERT_FALSE(error.empty());
}

namespace {

/// Records what the reassembler delivered.
struct RecordingSink final : StreamSink {
    std::string data;
    std::vector<uint64_t> gaps;
    int starts{0};
    int closes{0};
    bool last_at_boundary{false};

    void on_stream_start(bool at_boundary, Micros) override {
        ++starts;
        last_at_boundary = at_boundary;
    }
    void on_stream_data(std::span<const uint8_t> bytes, Micros) override { data.append(bytes.begin(), bytes.end()); }
    void on_stream_gap(uint64_t lost, Micros) override { gaps.push_back(lost); }
    void on_stream_closed(Micros) override { ++closes; }
};

/// A server segment carrying `text` at `seq`.
struct Seg {
    Bytes frame;
    TcpSegment segment;
};

Seg seg(uint32_t seq, std::string_view text, Micros ts = 0,
        uint8_t flags = static_cast<uint8_t>(tcp_flag::Ack | tcp_flag::Psh)) {
    Seg s;
    TcpSpec spec;
    spec.seq = seq;
    spec.flags = flags;
    s.frame = ipv4_tcp(spec, bytes_of(text));
    s.segment = *parse_tcp_segment(link_type::Raw, s.frame, ts);
    return s;
}

} // namespace

TEST_CASE(TcpReassembler, InOrderAndOutOfOrder) {
    TcpReassembler r;
    RecordingSink sink;
    r.push(seg(100, "abc").segment, sink);
    TEST_ASSERT_EQ(sink.starts, 1);
    TEST_ASSERT_FALSE(sink.last_at_boundary);
    r.push(seg(106, "ghi").segment, sink);  // ahead of a hole
    TEST_ASSERT_EQ(sink.data, std::string("abc"));
    r.push(seg(103, "def").segment, sink);  // fills it
    TEST_ASSERT_EQ(sink.data, std::string("abcdefghi"));
    TEST_ASSERT(sink.gaps.empty());
}

TEST_CASE(TcpReassembler, RetransmitAndOverlapAreTrimmed) {
    TcpReassembler r;
    RecordingSink sink;
    r.push(seg(100, "abcd").segment, sink);
    r.push(seg(100, "abcd").segment, sink);   // retransmit
    r.push(seg(102, "cdefg").segment, sink);  // overlaps two bytes
    TEST_ASSERT_EQ(sink.data, std::string("abcdefg"));
}

TEST_CASE(TcpReassembler, HoleTimesOutAsGap) {
    TcpReassembler r;
    RecordingSink sink;
    r.push(seg(100, "ab", 0).segment, sink);
    r.push(seg(110, "zz", 1'000).segment, sink);
    TEST_ASSERT(sink.gaps.empty());
    r.expire(1'000 + TcpReassembler::kHoleTimeout - 1, sink);
    TEST_ASSERT(sink.gaps.empty());
    r.expire(1'000 + TcpReassembler::kHoleTimeout, sink);
    TEST_ASSERT_EQ(sink.gaps.size(), size_t{1});
    TEST_ASSERT_EQ(sink.gaps[0], uint64_t{8});
    TEST_ASSERT_EQ(sink.data, std::string("abzz"));
    TEST_ASSERT_EQ(r.gap_bytes(), uint64_t{8});
}

TEST_CASE(TcpReassembler, SynMarksBoundaryAndFinCloses) {
    TcpReassembler r;
    RecordingSink sink;
    r.push(seg(499, "", 0, tcp_flag::Syn | tcp_flag::Ack).segment, sink);
    TEST_ASSERT(sink.last_at_boundary);
    r.push(seg(500, "xy", 1, tcp_flag::Ack | tcp_flag::Fin).segment, sink);
    TEST_ASSERT_EQ(sink.data, std::string("xy"));
    TEST_ASSERT_EQ(sink.closes, 1);
    TEST_ASSERT_FALSE(r.started());
}

TEST_CASE(TcpReassembler, FinWaitsForEarlierBytes) {
    TcpReassembler r;
    RecordingSink sink;
    r.push(seg(100, "ab").segment, sink);
    r.push(seg(104, "ef", 0, tcp_flag::Ack | tcp_flag::Fin).segment, sink);
    TEST_ASSERT_EQ(sink.closes, 0);
    r.push(seg(102, "cd").segment, sink);
    TEST_ASSERT_EQ(sink.data, std::string("abcdef"));
    TEST_ASSERT_EQ(sink.closes, 1);
}

TEST_CASE(TcpReassembler, RstClosesAtOnce) {
    TcpReassembler r;
    RecordingSink sink;
    r.push(seg(100, "ab").segment, sink);
    r.push(seg(102, "", 0, tcp_flag::Rst).segment, sink);
    TEST_ASSERT_EQ(sink.closes, 1);
    TEST_ASSERT_FALSE(r.started());
}

TEST_CASE(TcpReassembler, SequenceWrapAround) {
    TcpReassembler r;
    RecordingSink sink;
    r.push(seg(0xFFFFFFFE, "ab").segment, sink);
    r.push(seg(0x00000000, "cd").segment, sink);
    r.push(seg(0x00000002, "ef").segment, sink);
    TEST_ASSERT_EQ(sink.data, std::string("abcdef"));
    TEST_ASSERT_EQ(r.delivered_bytes(), uint64_t{6});
}

TEST_CASE(TcpReassembler, BufferCapForcesGap) {
    TcpReassembler r;
    RecordingSink sink;
    r.push(seg(0, "a").segment, sink);
    // Chunks that fit an IPv4 packet, queued behind a 9-byte hole until the cap.
    const std::string chunk(60'000, 'x');
    uint32_t seq = 10;
    size_t queued = 0;
    while (queued + chunk.size() <= TcpReassembler::kMaxBuffered) {
        r.push(seg(seq, chunk).segment, sink);
        seq += static_cast<uint32_t>(chunk.size());
        queued += chunk.size();
    }
    TEST_ASSERT(sink.gaps.empty());
    r.push(seg(seq, chunk).segment, sink);
    queued += chunk.size();
    TEST_ASSERT_EQ(sink.gaps.size(), size_t{1});
    TEST_ASSERT_EQ(sink.gaps[0], uint64_t{9});
    TEST_ASSERT_EQ(sink.data.size(), 1 + queued);
    TEST_ASSERT_EQ(r.buffered_bytes(), size_t{0});
}

TEST_CASE(TcpReassembler, StrayFarAheadSegmentIsIgnored) {
    TcpReassembler r;
    RecordingSink sink;
    r.push(seg(100, "ab", 1).segment, sink);
    r.push(seg(100 + (3u << 29), "zz", 2).segment, sink);  // 1.5 GiB ahead
    r.push(seg(102, "cd", 2 + TcpReassembler::kHoleTimeout).segment, sink);
    r.expire(2 + 3 * TcpReassembler::kHoleTimeout, sink);
    r.push(seg(104, "ef", 3 + 3 * TcpReassembler::kHoleTimeout).segment, sink);
    TEST_ASSERT_EQ(sink.data, std::string("abcdef"));
    TEST_ASSERT(sink.gaps.empty());
}

TEST_CASE(TcpReassembler, StreamThatMovedFarAheadSkipsThere) {
    TcpReassembler r;
    RecordingSink sink;
    r.push(seg(100, "ab", 1).segment, sink);
    const uint32_t far = 102 + static_cast<uint32_t>(TcpReassembler::kMaxAhead) + 1000;
    r.push(seg(far, "xy", 2).segment, sink);
    TEST_ASSERT(sink.gaps.empty());
    r.push(seg(far + 2, "zw", 2 + TcpReassembler::kHoleTimeout).segment, sink);
    TEST_ASSERT_EQ(sink.gaps.size(), size_t{1});
    TEST_ASSERT_EQ(sink.gaps[0], uint64_t{far + 2 - 102});
    TEST_ASSERT_EQ(sink.data, std::string("abzw"));
    r.push(seg(far + 4, "!", 3 + TcpReassembler::kHoleTimeout).segment, sink);
    TEST_ASSERT_EQ(sink.data, std::string("abzw!"));
}

TEST_CASE(TcpReassembler, RetransmittedSynAfterDataKeepsTheStream) {
    TcpReassembler r;
    RecordingSink sink;
    r.push(seg(499, "", 1, tcp_flag::Syn | tcp_flag::Ack).segment, sink);
    r.push(seg(500, "xy", 2).segment, sink);
    r.push(seg(499, "", 3, tcp_flag::Syn | tcp_flag::Ack).segment, sink);
    r.push(seg(502, "zz", 4).segment, sink);
    TEST_ASSERT_EQ(sink.starts, 1);
    TEST_ASSERT_EQ(sink.closes, 0);
    TEST_ASSERT_EQ(sink.data, std::string("xyzz"));
}

TEST_CASE(TcpReassembler, HolesBehindOneAnotherExpireTogether) {
    TcpReassembler r;
    RecordingSink sink;
    r.push(seg(100, "a", 1).segment, sink);
    r.push(seg(105, "bb", 1).segment, sink);  // behind a hole at 101..104
    r.push(seg(110, "cc", 1).segment, sink);  // behind another at 107..109
    r.expire(1 + TcpReassembler::kHoleTimeout, sink);
    TEST_ASSERT_EQ(sink.gaps.size(), size_t{2});
    TEST_ASSERT_EQ(sink.data, std::string("abbcc"));
}
