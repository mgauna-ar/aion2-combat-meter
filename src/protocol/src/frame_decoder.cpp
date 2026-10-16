#include "protocol/frame_decoder.hpp"

#include "protocol/byte_reader.hpp"
#include "protocol/lz4_block.hpp"
#include "protocol/opcodes.hpp"

#include <algorithm>

namespace aion2::proto {

namespace {

/// Length, opcode and the 8-byte server time.
constexpr size_t kHeartbeatFrameBytes = 11;
/// len counts 4 more than the bytes after the varint, which hold at least the opcode.
constexpr uint64_t kMinFrameLen = 6;
/// Unix milliseconds of 2020-01-01 and 2100-01-01: a heartbeat in a capture with no
/// timestamps must still read as a date.
constexpr uint64_t kEarliestServerMs = 1'577'836'800'000ULL;
constexpr uint64_t kLatestServerMs = 4'102'444'800'000ULL;
/// Bytes kept after a failed search, in case a heartbeat straddles the next feed.
constexpr size_t kSearchKeep = kHeartbeatFrameBytes - 1;
/// Buffer consumed before the front is erased.
constexpr size_t kCompactAfter = 64u << 10;

[[nodiscard]] bool is_flag_byte(uint8_t b) noexcept { return b >= 0xF0 && b <= 0xFE; }

/// The frame at the front of `bytes`: its wire size, and its content after the
/// varint. nullopt when `bytes` does not hold a whole valid one; `lost` then says
/// whether that is because it is invalid rather than incomplete.
struct FrameSpan {
    size_t wire{0};
    size_t varint_size{0};
};

std::optional<FrameSpan> frame_at(std::span<const uint8_t> bytes, bool& lost) noexcept {
    lost = false;
    bool overlong = false;
    const auto v = read_varint(bytes, &overlong);
    if (!v) {
        lost = overlong;
        return std::nullopt;
    }
    if (v->value < kMinFrameLen || v->value > FrameDecoder::kMaxFrameBytes) {
        lost = true;
        return std::nullopt;
    }
    const size_t wire = static_cast<size_t>(v->value) + v->size - 4;
    if (bytes.size() < wire) return std::nullopt;
    return FrameSpan{wire, v->size};
}

} // namespace

bool FrameDecoder::is_heartbeat_at(std::span<const uint8_t> bytes, Micros ts) noexcept {
    if (bytes.size() < kHeartbeatFrameBytes) return false;
    if (bytes[0] != 0x0E || bytes[1] != 0x00 || bytes[2] != 0x36) return false;
    ByteReader r(bytes.subspan(3, 8));
    const uint64_t ms = r.u64le();
    if (ts == 0) return ms >= kEarliestServerMs && ms <= kLatestServerMs;
    const uint64_t capture_ms = ts / kMicrosPerMilli;
    const uint64_t diff = ms > capture_ms ? ms - capture_ms : capture_ms - ms;
    return diff <= kClockToleranceMs;
}

void FrameDecoder::start(bool at_boundary, Micros ts) {
    m_buf.clear();
    m_pos = 0;
    m_buf_offset = 0;
    m_aligned = at_boundary;
    m_aligned_since = ts;
    m_last_heartbeat_ts = 0;
}

void FrameDecoder::on_gap() {
    ++m_stats.gaps;
    m_buf_offset += m_buf.size();
    m_buf.clear();
    m_pos = 0;
    lose_alignment();
}

void FrameDecoder::lose_alignment() {
    if (m_aligned) ++m_stats.resyncs;
    m_aligned = false;
}

void FrameDecoder::search(Micros ts) {
    const std::span<const uint8_t> buf(m_buf);
    for (size_t i = m_pos; i + kHeartbeatFrameBytes <= buf.size(); ++i) {
        if (buf[i] == 0x0E && is_heartbeat_at(buf.subspan(i), ts)) {
            m_stats.skipped_bytes += i - m_pos;
            m_pos = i;
            m_aligned = true;
            m_aligned_since = ts;
            return;
        }
    }
    const size_t keep_from = buf.size() > kSearchKeep ? buf.size() - kSearchKeep : 0;
    if (keep_from > m_pos) {
        m_stats.skipped_bytes += keep_from - m_pos;
        m_pos = keep_from;
    }
}

void FrameDecoder::feed(std::span<const uint8_t> bytes, Micros ts, FrameSink& sink) {
    m_buf.insert(m_buf.end(), bytes.begin(), bytes.end());

    for (;;) {
        if (!m_aligned) {
            search(ts);
            if (!m_aligned) break;
        }
        const Step step = decode_one(ts, sink);
        if (step == Step::NeedMore) break;
        if (step == Step::Lost) lose_alignment();
    }

    // Aligned but no heartbeat for a while as bytes keep coming: the lengths are
    // being read from the wrong places, even if each looked plausible.
    if (m_aligned) {
        const Micros since = std::max(m_last_heartbeat_ts, m_aligned_since);
        if (ts > since && ts - since > kHeartbeatSilence) {
            lose_alignment();
            search(ts);
        }
    }

    compact();
}

FrameDecoder::Step FrameDecoder::decode_one(Micros ts, FrameSink& sink) {
    while (m_pos < m_buf.size() && m_buf[m_pos] == 0x00) ++m_pos;
    if (m_pos >= m_buf.size()) return Step::NeedMore;

    const std::span<const uint8_t> avail = std::span<const uint8_t>(m_buf).subspan(m_pos);
    bool lost = false;
    const auto frame = frame_at(avail, lost);
    if (!frame) return lost ? Step::Lost : Step::NeedMore;

    const auto raw = avail.first(frame->wire);
    auto content = raw.subspan(frame->varint_size);
    uint8_t flag = 0;
    if (is_flag_byte(content[0])) {
        flag = content[0];
        content = content.subspan(1);
    }
    if (content.size() < 2) return Step::Lost;

    const uint64_t offset = m_buf_offset + m_pos;
    m_pos += frame->wire;
    emit(raw, content, flag, ts, 0, offset, sink);
    return Step::Frame;
}

void FrameDecoder::emit(std::span<const uint8_t> raw, std::span<const uint8_t> payload, uint8_t flag, Micros ts,
                        uint8_t depth, uint64_t offset, FrameSink& sink) {
    const auto op = static_cast<uint16_t>((payload[0] << 8) | payload[1]);

    if (op == opcode::Bundle) {
        ++m_stats.bundles;
        ByteReader r(payload.subspan(2));
        const uint32_t raw_size = r.u32le();
        if (!r.ok() || depth >= kMaxBundleDepth) {
            ++m_stats.bundle_errors;
            return;
        }
        const auto inflated = lz4_decompress_block(r.rest(), raw_size);
        if (!inflated || !decode_bundle(*inflated, ts, static_cast<uint8_t>(depth + 1), offset, sink)) {
            ++m_stats.bundle_errors;
        }
        return;
    }

    ++m_stats.frames;
    Frame frame;
    frame.opcode = op;
    frame.flag = flag;
    frame.body = payload.subspan(2);
    frame.raw = raw;
    frame.ts = ts;
    frame.depth = depth;
    frame.stream_offset = offset;

    if (op == opcode::Heartbeat && frame.body.size() >= 8) {
        ByteReader r(frame.body);
        m_stats.last_server_ms = r.u64le();
        ++m_stats.heartbeats;
        m_last_heartbeat_ts = ts;
    }
    sink.on_frame(frame);
}

bool FrameDecoder::decode_bundle(std::span<const uint8_t> bytes, Micros ts, uint8_t depth, uint64_t offset,
                                 FrameSink& sink) {
    size_t pos = 0;
    while (pos < bytes.size()) {
        if (bytes[pos] == 0x00) {
            ++pos;
            continue;
        }
        bool lost = false;
        const auto frame = frame_at(bytes.subspan(pos), lost);
        if (!frame) return false;
        const auto raw = bytes.subspan(pos, frame->wire);
        auto content = raw.subspan(frame->varint_size);
        uint8_t flag = 0;
        if (is_flag_byte(content[0])) {
            flag = content[0];
            content = content.subspan(1);
        }
        if (content.size() < 2) return false;
        emit(raw, content, flag, ts, depth, offset, sink);
        pos += frame->wire;
    }
    return true;
}

void FrameDecoder::compact() {
    if (m_pos == 0) return;
    if (m_pos < kCompactAfter && m_pos < m_buf.size()) return;
    m_buf.erase(m_buf.begin(), m_buf.begin() + static_cast<std::ptrdiff_t>(m_pos));
    m_buf_offset += m_pos;
    m_pos = 0;
}

} // namespace aion2::proto
