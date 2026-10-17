#include "test_framework.hpp"
#include "lz4_test_encoder.hpp"
#include "wire_builder.hpp"

#include "protocol/byte_reader.hpp"
#include "protocol/frame_decoder.hpp"
#include "protocol/lz4_block.hpp"
#include "protocol/opcodes.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace aion2;
using namespace aion2::proto;
using namespace aion2::test::wire;

namespace {

/// 2026-10-16 12:00:00 UTC, as the capture clock and the server clock.
constexpr uint64_t kServerMs = 1'792'152'000'000ULL;
constexpr Micros kTs = kServerMs * kMicrosPerMilli;

struct Captured {
    uint16_t opcode;
    uint8_t flag;
    Bytes body;
    uint8_t depth;
};

struct CollectingSink final : FrameSink {
    std::vector<Captured> frames;
    void on_frame(const Frame& f) override {
        frames.push_back(Captured{f.opcode, f.flag, Bytes(f.body.begin(), f.body.end()), f.depth});
    }
    [[nodiscard]] std::vector<uint16_t> opcodes() const {
        std::vector<uint16_t> out;
        for (const auto& f : frames) out.push_back(f.opcode);
        return out;
    }
};

} // namespace

TEST_CASE(Varint, ReadsLeb128) {
    const Bytes one = {0x0E};
    const Bytes two = {0xAC, 0x02};
    const auto a = read_varint(one);
    const auto b = read_varint(two);
    TEST_ASSERT(a.has_value() && a->value == 14 && a->size == 1);
    TEST_ASSERT(b.has_value() && b->value == 300 && b->size == 2);
}

TEST_CASE(Varint, IncompleteAndOverlong) {
    const Bytes incomplete = {0x80, 0x80};
    bool overlong = true;
    TEST_ASSERT_FALSE(read_varint(incomplete, &overlong).has_value());
    TEST_ASSERT_FALSE(overlong);
    const Bytes junk(11, 0xFF);
    TEST_ASSERT_FALSE(read_varint(junk, &overlong).has_value());
    TEST_ASSERT(overlong);

    // Ten bytes reach bit 63 at most: a 10th byte of 1 is the top bit, more is not a value.
    Bytes top(9, 0xFF);
    top.push_back(0x01);
    const auto max = read_varint(top, &overlong);
    TEST_ASSERT(max.has_value() && max->value == UINT64_MAX);
    top.back() = 0x02;
    TEST_ASSERT_FALSE(read_varint(top, &overlong).has_value());
    TEST_ASSERT(overlong);
}

TEST_CASE(ByteReader, FailureSticks) {
    const Bytes data = {0x01, 0x02, 0x03};
    ByteReader r(data);
    TEST_ASSERT_EQ(r.u16le(), uint16_t{0x0201});
    TEST_ASSERT_EQ(r.u32le(), 0u);  // past the end
    TEST_ASSERT_FALSE(r.ok());
    TEST_ASSERT_EQ(r.u8(), uint8_t{0});  // even though one byte remained
    TEST_ASSERT_EQ(r.remaining(), size_t{0});
}

TEST_CASE(ByteReader, MixedFields) {
    Bytes data;
    put_varint(data, 123456);
    put_le32(data, 0xCAFEBABE);
    put_bytes(data, {0x00, 0x00, 0x80, 0x3F});  // 1.0f
    put_bytes(data, {0x04, 0x38});
    ByteReader r(data);
    TEST_ASSERT_EQ(r.varint(), uint64_t{123456});
    TEST_ASSERT_EQ(r.u32le(), 0xCAFEBABEu);
    TEST_ASSERT_NEAR(r.f32le(), 1.0f, 1e-6f);
    TEST_ASSERT_EQ(r.u16be(), uint16_t{0x0438});
    TEST_ASSERT(r.ok());
    TEST_ASSERT(r.at_end());
}

TEST_CASE(Lz4, LiteralOnlyBlock) {
    const Bytes text = bytes_of("abc");
    const Bytes block = {0x30, 'a', 'b', 'c'};
    const auto out = lz4_decompress_block(block, 3);
    TEST_ASSERT(out.has_value());
    TEST_ASSERT(*out == text);
}

TEST_CASE(Lz4, OverlappingMatchMakesARun) {
    // "a", then a 9-byte match at offset 1: ten 'a's in all, then "b".
    const Bytes block = {0x15, 'a', 0x01, 0x00, 0x10, 'b'};
    const auto out = lz4_decompress_block(block, 11);
    TEST_ASSERT(out.has_value());
    TEST_ASSERT_EQ(std::string(out->begin(), out->end()), std::string("aaaaaaaaaab"));
}

TEST_CASE(Lz4, RoundTripsTheTestEncoder) {
    Bytes src;
    for (int i = 0; i < 4000; ++i) {
        src.push_back(static_cast<uint8_t>((i * 7) % 13));
        if (i % 50 == 0) put_bytes(src, bytes_of("Daeva of light and shadow"));
    }
    const Bytes block = test::lz4::compress(src);
    TEST_ASSERT(block.size() < src.size());
    const auto out = lz4_decompress_block(block, src.size());
    TEST_ASSERT(out.has_value());
    TEST_ASSERT(*out == src);
}

TEST_CASE(Lz4, RejectsCorruptBlocks) {
    const Bytes text(64, 'z');
    const Bytes block = test::lz4::compress(text);
    TEST_ASSERT_FALSE(lz4_decompress_block(block, text.size() + 1).has_value());  // size mismatch
    const Bytes cut(block.begin(), block.end() - 2);
    TEST_ASSERT_FALSE(lz4_decompress_block(cut, text.size()).has_value());
    const Bytes bad_offset = {0x10, 'a', 0x05, 0x00, 0x00};  // offset 5 with 1 byte out
    TEST_ASSERT_FALSE(lz4_decompress_block(bad_offset, 6).has_value());
    TEST_ASSERT_FALSE(lz4_decompress_block(block, kMaxLz4RawSize + 1).has_value());
}

TEST_CASE(FrameDecoder, HeartbeatFrameIsElevenBytes) {
    const Bytes hb = heartbeat(kServerMs);
    TEST_ASSERT_EQ(hb.size(), size_t{11});
    TEST_ASSERT_EQ(hb[0], uint8_t{0x0E});
    TEST_ASSERT(FrameDecoder::is_heartbeat_at(hb, kTs));
    TEST_ASSERT(FrameDecoder::is_heartbeat_at(hb, 0));
    TEST_ASSERT_FALSE(FrameDecoder::is_heartbeat_at(hb, kTs + 3 * 24 * 3600 * kMicrosPerSecond));
}

TEST_CASE(FrameDecoder, DecodesFramesFlagsAndPadding) {
    const Bytes stream = concat({
        heartbeat(kServerMs),
        opcode_frame(opcode::Damage, {1, 2, 3}),
        Bytes{0x00, 0x00},
        opcode_frame(opcode::Death, {9}, 0xF3),
    });
    FrameDecoder d;
    CollectingSink sink;
    d.start(true, kTs);
    d.feed(stream, kTs, sink);
    TEST_ASSERT_EQ(sink.frames.size(), size_t{3});
    TEST_ASSERT_EQ(sink.frames[0].opcode, opcode::Heartbeat);
    TEST_ASSERT_EQ(sink.frames[1].opcode, opcode::Damage);
    TEST_ASSERT((sink.frames[1].body == Bytes{1, 2, 3}));
    TEST_ASSERT_EQ(sink.frames[2].opcode, opcode::Death);
    TEST_ASSERT_EQ(sink.frames[2].flag, uint8_t{0xF3});
    TEST_ASSERT((sink.frames[2].body == Bytes{9}));
    TEST_ASSERT_EQ(d.stats().heartbeats, uint64_t{1});
    TEST_ASSERT_EQ(d.stats().last_server_ms, kServerMs);
    TEST_ASSERT_EQ(d.stats().resyncs, uint64_t{0});
}

TEST_CASE(FrameDecoder, ByteByByteMatchesWhole) {
    Bytes big_body(300, 0x5A);
    const Bytes stream = concat({
        heartbeat(kServerMs),
        opcode_frame(opcode::Spawn, big_body),
        opcode_frame(opcode::Damage, {1}),
    });
    FrameDecoder whole;
    CollectingSink a;
    whole.start(true, kTs);
    whole.feed(stream, kTs, a);

    FrameDecoder split;
    CollectingSink b;
    split.start(true, kTs);
    for (const uint8_t byte : stream) split.feed(std::span<const uint8_t>(&byte, 1), kTs, b);

    TEST_ASSERT_EQ(a.frames.size(), size_t{3});
    TEST_ASSERT(a.opcodes() == b.opcodes());
    TEST_ASSERT(a.frames[1].body == b.frames[1].body);
}

TEST_CASE(FrameDecoder, BundleExpandsIntoFrames) {
    const Bytes inner = concat({
        opcode_frame(opcode::Damage, {1, 1}),
        opcode_frame(opcode::Tick, {2, 2}),
        opcode_frame(opcode::OtherPlayer, Bytes(40, 0x41)),
    });
    const Bytes stream = concat({heartbeat(kServerMs), bundle_frame(inner), opcode_frame(opcode::Death, {7})});
    FrameDecoder d;
    CollectingSink sink;
    d.start(true, kTs);
    d.feed(stream, kTs, sink);
    TEST_ASSERT((sink.opcodes() == std::vector<uint16_t>{opcode::Heartbeat, opcode::Damage, opcode::Tick,
                                                          opcode::OtherPlayer, opcode::Death}));
    TEST_ASSERT_EQ(sink.frames[1].depth, uint8_t{1});
    TEST_ASSERT_EQ(sink.frames[4].depth, uint8_t{0});
    TEST_ASSERT_EQ(d.stats().bundles, uint64_t{1});
    TEST_ASSERT_EQ(d.stats().bundle_errors, uint64_t{0});
}

TEST_CASE(FrameDecoder, NestedBundle) {
    const Bytes innermost = opcode_frame(opcode::Damage, {5});
    const Bytes middle = concat({bundle_frame(innermost), opcode_frame(opcode::Tick, {6})});
    FrameDecoder d;
    CollectingSink sink;
    d.start(true, kTs);
    d.feed(bundle_frame(middle), kTs, sink);
    TEST_ASSERT((sink.opcodes() == std::vector<uint16_t>{opcode::Damage, opcode::Tick}));
    TEST_ASSERT_EQ(sink.frames[0].depth, uint8_t{2});
    TEST_ASSERT_EQ(d.stats().bundles, uint64_t{2});
}

TEST_CASE(FrameDecoder, CorruptBundleIsCountedNotFatal) {
    Bytes payload = {0xFF, 0xFF};
    put_le32(payload, 100);
    put_bytes(payload, {0x10, 'a', 0x09, 0x00});  // offset beyond the output
    const Bytes stream = concat({game_frame(payload), opcode_frame(opcode::Death, {1})});
    FrameDecoder d;
    CollectingSink sink;
    d.start(true, kTs);
    d.feed(stream, kTs, sink);
    TEST_ASSERT_EQ(d.stats().bundle_errors, uint64_t{1});
    TEST_ASSERT((sink.opcodes() == std::vector<uint16_t>{opcode::Death}));
}

TEST_CASE(FrameDecoder, MidStreamJoinAlignsOnHeartbeat) {
    // The tail of some frame, then a heartbeat and real frames.
    const Bytes stream = concat({
        Bytes{0x37, 0x99, 0x04, 0x38, 0x11, 0x22},
        heartbeat(kServerMs),
        opcode_frame(opcode::Damage, {3}),
    });
    FrameDecoder d;
    CollectingSink sink;
    d.start(false, kTs);
    d.feed(stream, kTs, sink);
    TEST_ASSERT((sink.opcodes() == std::vector<uint16_t>{opcode::Heartbeat, opcode::Damage}));
    TEST_ASSERT_EQ(d.stats().skipped_bytes, uint64_t{6});
    TEST_ASSERT(d.aligned());
}

TEST_CASE(FrameDecoder, HeartbeatSplitAcrossFeedsIsFound) {
    const Bytes stream = concat({Bytes{0x99, 0x98}, heartbeat(kServerMs), opcode_frame(opcode::Death, {1})});
    FrameDecoder d;
    CollectingSink sink;
    d.start(false, kTs);
    const std::span<const uint8_t> all(stream);
    d.feed(all.first(7), kTs, sink);
    d.feed(all.subspan(7), kTs, sink);
    TEST_ASSERT((sink.opcodes() == std::vector<uint16_t>{opcode::Heartbeat, opcode::Death}));
}

TEST_CASE(FrameDecoder, GapResyncsAtNextHeartbeat) {
    FrameDecoder d;
    CollectingSink sink;
    d.start(true, kTs);
    const Bytes damage = opcode_frame(opcode::Damage, Bytes(20, 1));
    d.feed(std::span<const uint8_t>(damage).first(8), kTs, sink);  // a frame cut by the gap
    d.on_gap();
    TEST_ASSERT_FALSE(d.aligned());
    const Bytes after = concat({Bytes{0x01, 0x01, 0x01}, heartbeat(kServerMs + 50), opcode_frame(opcode::Tick, {2})});
    d.feed(after, kTs, sink);
    TEST_ASSERT((sink.opcodes() == std::vector<uint16_t>{opcode::Heartbeat, opcode::Tick}));
    TEST_ASSERT_EQ(d.stats().gaps, uint64_t{1});
    TEST_ASSERT_EQ(d.stats().resyncs, uint64_t{1});
}

TEST_CASE(FrameDecoder, ImpossibleLengthLosesAlignment) {
    Bytes bad;
    put_varint(bad, FrameDecoder::kMaxFrameBytes + 1);
    const Bytes stream = concat({bad, Bytes{1, 2, 3}, heartbeat(kServerMs), opcode_frame(opcode::Death, {1})});
    FrameDecoder d;
    CollectingSink sink;
    d.start(true, kTs);
    d.feed(stream, kTs, sink);
    TEST_ASSERT_EQ(d.stats().resyncs, uint64_t{1});
    TEST_ASSERT((sink.opcodes() == std::vector<uint16_t>{opcode::Heartbeat, opcode::Death}));
}

TEST_CASE(FrameDecoder, SilenceWatchdogResyncs) {
    FrameDecoder d;
    CollectingSink sink;
    d.start(true, kTs);
    d.feed(heartbeat(kServerMs), kTs, sink);
    // Plausible frames but no heartbeat for longer than the watchdog allows.
    d.feed(opcode_frame(opcode::Damage, {1}), kTs + FrameDecoder::kHeartbeatSilence + 1, sink);
    TEST_ASSERT_FALSE(d.aligned());
    TEST_ASSERT_EQ(d.stats().resyncs, uint64_t{1});
}

TEST_CASE(FrameDecoder, OnlyARealHeartbeatCounts) {
    FrameDecoder d;
    CollectingSink sink;
    d.start(true, kTs);
    d.feed(heartbeat(kServerMs), kTs, sink);
    Bytes far_off;
    put_le64(far_off, kServerMs + 10ULL * 24 * 3600 * 1000);
    Bytes long_body;
    put_le64(long_body, kServerMs);
    put_le32(long_body, 0);
    Bytes in_time;
    put_le64(in_time, kServerMs + 50);
    d.feed(concat({opcode_frame(opcode::Heartbeat, far_off), opcode_frame(opcode::Heartbeat, long_body),
                   opcode_frame(opcode::Heartbeat, in_time, 0xF1)}),
           kTs, sink);
    // Delivered as frames, but none is the server's clock.
    TEST_ASSERT_EQ(sink.frames.size(), size_t{4});
    TEST_ASSERT_EQ(d.stats().heartbeats, uint64_t{1});
    TEST_ASSERT_EQ(d.stats().last_server_ms, kServerMs);
}

TEST_CASE(FrameDecoder, WatchdogDecodesWhatFollowsTheHeartbeatAtOnce) {
    FrameDecoder d;
    CollectingSink sink;
    d.start(true, kTs);
    d.feed(heartbeat(kServerMs), kTs, sink);
    // Read as aligned, a length that wants more bytes than will come, holding the
    // next heartbeat and a frame.
    Bytes misread;
    put_varint(misread, 1000);
    put_bytes(misread, {0x04, 0x38});
    const Micros late = kTs + FrameDecoder::kHeartbeatSilence + 1;
    d.feed(concat({misread, heartbeat(kServerMs + 3001), opcode_frame(opcode::Death, {1})}), late, sink);
    TEST_ASSERT((sink.opcodes() == std::vector<uint16_t>{opcode::Heartbeat, opcode::Heartbeat, opcode::Death}));
    TEST_ASSERT(d.aligned());
    TEST_ASSERT_EQ(d.stats().resyncs, uint64_t{1});
}
